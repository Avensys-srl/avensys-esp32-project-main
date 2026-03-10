#include "ble.h"
#include "main.h"
#include "CL_WBM.h"
#include "mqtt_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* The max length of characteristic value. When the GATT client performs a write or prepare write operation,
 *  the data length must be less than ESP_BLE_CHAR_VAL_LEN_MAX.
 */
#define ESP_BLE_CHAR_VAL_LEN_MAX 500
#define PREPARE_BUF_MAX_SIZE     1024
#define CHAR_DECLARATION_SIZE    (sizeof(uint8_t))


#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)
static uint8_t adv_config_done = 0;

extern uint16_t Read_Eeprom_Request_Index;
extern S_EEPROM gRDEeprom;
extern CLKTSData gKTSData;
extern CLKTSDebugData gKTSDebugData;

uint16_t ble_handle_table[HRS_IDX_NB];

typedef struct {
    uint8_t *prepare_buf;
    int      prepare_len;
    uint16_t prepare_handle;
} prepare_type_env_t;

// Struttura per memorizzare i dati del pacchetto BLE
typedef struct {
    uint8_t *data; // Puntatore ai dati del pacchetto
    size_t len;    // Lunghezza dei dati del pacchetto
} ble_packet_t;

// Variabili globali per memorizzare i dati dei pacchetti BLE
ble_packet_t ssid_packet = {0};
ble_packet_t password_packet = {0};

static prepare_type_env_t prepare_write_env;

typedef enum {
    PROV_STATE_IDLE = 0,
    PROV_STATE_WAIT_SSID,
    PROV_STATE_WAIT_PASSWORD,
    PROV_STATE_READY,
    PROV_STATE_APPLYING,
    PROV_STATE_DONE,
    PROV_STATE_ERROR,
    PROV_STATE_BOOTING,
    PROV_STATE_RUNTIME_READY,
} prov_state_t;

static prov_state_t s_prov_state = PROV_STATE_IDLE;
static uint8_t s_prov_status_value = PROV_STATE_IDLE;
static bool s_prov_status_notify = false;
static uint16_t s_conn_id = 0;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static TimerHandle_t s_prov_timer = NULL;
static bool s_runtime_ready = false;

#define PROV_TIMEOUT_MS 90000
#define BLE_POLLING_MIN_LEN 40
#define BLE_DEBUG_MIN_LEN 48

static void ble_set_prov_status(uint8_t status);
static void ble_init_runtime_placeholders(void);
static void ble_schedule_full_eeprom_write(const char *source);
static bool ble_apply_legacy242_eeprom(const uint8_t *legacy, size_t legacy_len);

static bool ble_packet_append(ble_packet_t *packet, const uint8_t *data, size_t len, size_t max_len, const char *label) {
    if (packet->data == NULL) {
        packet->data = malloc(len);
        if (packet->data == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "Failed to allocate memory for %s packet", label);
            return false;
        }
        memcpy(packet->data, data, len);
        packet->len = len;
        return true;
    }

    size_t new_len = packet->len + len;
    if (new_len > max_len) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s packet length exceeds maximum length", label);
        free(packet->data);
        packet->data = NULL;
        packet->len = 0;
        return false;
    }

    uint8_t *new_data = realloc(packet->data, new_len);
    if (new_data == NULL) {
        ESP_LOGE(GATTS_TABLE_TAG, "Failed to reallocate memory for %s packet", label);
        free(packet->data);
        packet->data = NULL;
        packet->len = 0;
        return false;
    }

    memcpy(new_data + packet->len, data, len);
    packet->data = new_data;
    packet->len = new_len;
    return true;
}

static void ble_packet_reset(ble_packet_t *packet) {
    if (packet->data != NULL) {
        free(packet->data);
        packet->data = NULL;
    }
    packet->len = 0;
}

static void ble_packet_copy_to_string(const ble_packet_t *packet, char *out, size_t out_size) {
    size_t copy_len = packet->len;
    if (out_size == 0) {
        return;
    }
    if (packet->data == NULL) {
        out[0] = '\0';
        return;
    }
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, packet->data, copy_len);
    out[copy_len] = '\0';
}

static void ble_schedule_full_eeprom_write(const char *source) {
    Read_Eeprom_Request_Index |= 0x800;
    Read_Eeprom_Request_Index |= 0x1000;
    Read_Eeprom_Request_Index |= 0x2000;
    Read_Eeprom_Request_Index |= 0x4000;
    Read_Eeprom_Request_Index |= 0x8000;
    ESP_LOGI(GATTS_TABLE_TAG, "EEPROM write scheduled from %s, flags=0x%04X", source, Read_Eeprom_Request_Index);
}

// Convert legacy 242-byte EEPROM payload to firmware 241-byte layout.
static bool ble_apply_legacy242_eeprom(const uint8_t *legacy, size_t legacy_len) {
    // Legacy mapping is only needed for the old 241-byte internal layout.
    if (legacy == NULL || sizeof(gRDEeprom) != 241 || legacy_len != 242) {
        return false;
    }

    uint8_t *dst = (uint8_t *)&gRDEeprom;

    memset(dst, 0, sizeof(gRDEeprom));
    memcpy(dst, legacy, 126);
    dst[126] = legacy[128];
    memcpy(dst + 127, legacy + 129, 6);
    memcpy(dst + 133, legacy + 135, 19);
    dst[152] = legacy[154];
    memcpy(dst + 153, legacy + 155, 84);
    dst[237] = legacy[239];
    dst[238] = legacy[241];
    dst[239] = legacy[240];
    dst[240] = 0;

    return true;
}

static bool ble_apply_eeprom_payload(const uint8_t *payload, size_t payload_len, const char *source) {
    if (payload == NULL || payload_len < 241) {
        ESP_LOGW(GATTS_TABLE_TAG, "EEPROM %s rejected: payload too short (%u)", source ? source : "write",
                 (unsigned)payload_len);
        return false;
    }

    // Special conversion path for old internal 241-byte map.
    if (ble_apply_legacy242_eeprom(payload, payload_len)) {
        ESP_LOGI(GATTS_TABLE_TAG, "EEPROM %s accepted with legacy242->241 mapping", source ? source : "write");
        return true;
    }

    memset(&gRDEeprom, 0, sizeof(gRDEeprom));
    size_t copy_len = payload_len;
    if (copy_len > sizeof(gRDEeprom)) {
        copy_len = sizeof(gRDEeprom);
    }
    memcpy(&gRDEeprom, payload, copy_len);
    ESP_LOGI(GATTS_TABLE_TAG, "EEPROM %s accepted len=%u copied=%u struct=%u",
             source ? source : "write", (unsigned)payload_len, (unsigned)copy_len, (unsigned)sizeof(gRDEeprom));
    return true;
}

static void ble_prov_timeout_callback(TimerHandle_t xTimer);

static void ble_prov_timer_reset(void) {
    if (s_prov_timer == NULL) {
        s_prov_timer = xTimerCreate("prov_timer", pdMS_TO_TICKS(PROV_TIMEOUT_MS), pdFALSE, NULL, ble_prov_timeout_callback);
        if (s_prov_timer == NULL) {
            return;
        }
    }
    xTimerStop(s_prov_timer, 0);
    xTimerStart(s_prov_timer, 0);
}

static void ble_prov_timeout_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    ble_packet_reset(&ssid_packet);
    ble_packet_reset(&password_packet);
    ble_set_prov_status(PROV_STATE_ERROR);
}

static void ble_prov_reset(void) {
    ble_packet_reset(&ssid_packet);
    ble_packet_reset(&password_packet);
    ble_set_prov_status(PROV_STATE_WAIT_SSID);
}

static void ble_set_prov_status(uint8_t status) {
    s_prov_status_value = status;
    s_prov_state = (prov_state_t)status;

    esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_PROV_STATUS], sizeof(s_prov_status_value), &s_prov_status_value);
    if (s_prov_status_notify && s_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, ble_handle_table[IDX_CHAR_VAL_PROV_STATUS],
                                    sizeof(s_prov_status_value), &s_prov_status_value, false);
    }

    if (s_prov_state == PROV_STATE_WAIT_SSID || s_prov_state == PROV_STATE_WAIT_PASSWORD || s_prov_state == PROV_STATE_READY) {
        ble_prov_timer_reset();
    } else if (s_prov_timer != NULL) {
        xTimerStop(s_prov_timer, 0);
    }
}

void ble_set_runtime_ready(bool ready) {
    s_runtime_ready = ready;
    if (ready) {
        ble_set_prov_status(PROV_STATE_RUNTIME_READY);
    } else {
        ble_set_prov_status(PROV_STATE_BOOTING);
    }
}

bool ble_is_runtime_ready(void) {
    return s_runtime_ready;
}

static void ble_init_runtime_placeholders(void) {
    static uint8_t s_polling_placeholder[BLE_POLLING_MIN_LEN] = {0};
    static uint8_t s_debug_placeholder[BLE_DEBUG_MIN_LEN] = {0};

    memset(&gRDEeprom, 0, sizeof(gRDEeprom));
    memset(&gKTSData, 0, sizeof(gKTSData));
    memset(&gKTSDebugData, 0, sizeof(gKTSDebugData));

    esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (uint8_t *)&gRDEeprom);
    // Keep BLE payloads compatible with app parser minimum lengths from first read.
    esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_POLLIING], sizeof(s_polling_placeholder), s_polling_placeholder);
    esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_DEBUG_DATA], sizeof(s_debug_placeholder), s_debug_placeholder);
}

static uint8_t service_uuid[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

/* The length of adv data must be less than 31 bytes */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x0006,  // slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval        = 0x0010,  // slave connection max interval, Time = max_interval * 1.25 msec
    .appearance          = 0x00,
    .manufacturer_len    = 0,     // TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL,  // test_manufacturer,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0,     // TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL,  //&test_manufacturer[0],
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst {
    esp_gatts_cb_t       gatts_cb;
    uint16_t             gatts_if;
    uint16_t             app_id;
    uint16_t             conn_id;
    uint16_t             service_handle;
    esp_gatt_srvc_id_t   service_id;
    uint16_t             char_handle;
    esp_bt_uuid_t        char_uuid;
    esp_gatt_perm_t      perm;
    esp_gatt_char_prop_t property;
    uint16_t             descr_handle;
    esp_bt_uuid_t        descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst data_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] =
        {
            .gatts_cb = gatts_profile_event_handler, .gatts_if = ESP_GATT_IF_NONE, /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        },
};

/* Service */
static const uint16_t GATTS_SERVICE_UUID_DATA = 0x00FF;

/* Characteristics */
static const uint16_t GATTS_CHAR_UUID_DATA_A = 0xFF01;
static const uint16_t GATTS_CHAR_UUID_DATA_B = 0xFF02;
static const uint16_t GATTS_CHAR_UUID_DATA_C = 0xFF03;

static const uint16_t GATTS_CHAR_UUID_WIFI_SSID        = 0xFF05;
static const uint16_t GATTS_CHAR_UUID_WIFI_PASSWORD    = 0xFF06;
static const uint16_t GATTS_CHAR_UUID_CONNECT_TO_CLOUD = 0xFF07;
static const uint16_t GATTS_CHAR_UUID_OTA_URL          = 0xFF08;
static const uint16_t GATTS_CHAR_UUID_UPDATE_FIRMWARE  = 0xFF09;
static const uint16_t GATTS_CHAR_UUID_DEVICE_ID        = 0xFF0A;
static const uint16_t GATTS_CHAR_UUID_PROV_STATUS      = 0xFF0B;

static const uint16_t primary_service_uuid         = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid   = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
// static const uint8_t char_prop_read                = ESP_GATT_CHAR_PROP_BIT_READ;
// static const uint8_t char_prop_write               = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_read_write_notify = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static const uint8_t a_ccc[2] = {0x00, 0x00};
static const uint8_t b_ccc[2] = {0x00, 0x00};
static const uint8_t c_ccc[2] = {0x00, 0x00};
static const uint8_t status_ccc[2] = {0x00, 0x00};

static const uint8_t char_value[4] = {0x11, 0x22, 0x33, 0x44};

/* Full Database Description - Used to add attributes into the database */
static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(GATTS_SERVICE_UUID_DATA), (uint8_t *)&GATTS_SERVICE_UUID_DATA}},

    /****************************** DATA A ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_EEPROM_DATA] = {{ESP_GATT_AUTO_RSP},
                              {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                               (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_EEPROM_DATA] = {{ESP_GATT_AUTO_RSP},
                                  {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_DATA_A, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(char_value),
                                   (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_EEPROM_DATA_CFG] = {{ESP_GATT_AUTO_RSP},
                                  {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(a_ccc),
                                   (uint8_t *)a_ccc}},

    /****************************** DATA B ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_DEBUG_DATA] = {{ESP_GATT_AUTO_RSP},
                             {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                              (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_DEBUG_DATA] = {{ESP_GATT_AUTO_RSP},
                                 {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_DATA_B, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(char_value),
                                  (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_DEBUG_DATA_CFG] = {{ESP_GATT_AUTO_RSP},
                                 {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(b_ccc),
                                  (uint8_t *)b_ccc}},

    /****************************** DATA C ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_POLLIING] = {{ESP_GATT_AUTO_RSP},
                           {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                            (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_POLLIING] = {{ESP_GATT_AUTO_RSP},
                               {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_DATA_C, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(char_value),
                                (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_POLLIING_CFG] = {{ESP_GATT_AUTO_RSP},
                               {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                (uint8_t *)c_ccc}},

    /****************************** WIFI SSID ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_WIFI_SSID] = {{ESP_GATT_AUTO_RSP},
                            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                             (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_WIFI_SSID] = {{ESP_GATT_AUTO_RSP},
                                {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_WIFI_SSID, ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(WIFI_SSID),
                                 (uint8_t *)WIFI_SSID}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_WIFI_SSID_CFG] = {{ESP_GATT_AUTO_RSP},
                                {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                 (uint8_t *)c_ccc}},

    /****************************** WIFI PASSWORD ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_WIFI_PASSWORD] = {{ESP_GATT_AUTO_RSP},
                                {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                 (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_WIFI_PASSWORD] = {{ESP_GATT_AUTO_RSP},
                                    {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_WIFI_PASSWORD, ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED, ESP_BLE_CHAR_VAL_LEN_MAX,
                                     sizeof(WIFI_PASSWORD), (uint8_t *)WIFI_PASSWORD}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_WIFI_PASSWORD_CFG] = {{ESP_GATT_AUTO_RSP},
                                    {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                     (uint8_t *)c_ccc}},

    /****************************** OTA URL ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_OTA_URL] = {{ESP_GATT_AUTO_RSP},
                          {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                           (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_OTA_URL] = {{ESP_GATT_AUTO_RSP},
                              {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_OTA_URL, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(char_value),
                               (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_OTA_URL_CFG] = {{ESP_GATT_AUTO_RSP},
                              {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                               (uint8_t *)c_ccc}},

    /****************************** UPDATE FIRMWARE ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_UPDATE_FIRMWARE] = {{ESP_GATT_AUTO_RSP},
                                  {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                   (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_UPDATE_FIRMWARE] = {{ESP_GATT_AUTO_RSP},
                                      {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_UPDATE_FIRMWARE, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX,
                                       sizeof(char_value), (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_UPDATE_FIRMWARE_CFG] = {{ESP_GATT_AUTO_RSP},
                                      {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                       (uint8_t *)c_ccc}},

    /****************************** DEVICE ID ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_DEVICE_ID] = {{ESP_GATT_AUTO_RSP},
                            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                             (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_DEVICE_ID] = {{ESP_GATT_AUTO_RSP},
                                {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_DEVICE_ID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX, sizeof(char_value),
                                 (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_DEVICE_ID_CFG] = {{ESP_GATT_AUTO_RSP},
                                {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                 (uint8_t *)c_ccc}},

    /****************************** CONNECT_TO_CLOUD ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_CONNECT_TO_CLOUD] = {{ESP_GATT_AUTO_RSP},
                                   {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                    (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_CONNECT_TO_CLOUD] = {{ESP_GATT_AUTO_RSP},
                                       {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_CONNECT_TO_CLOUD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_BLE_CHAR_VAL_LEN_MAX,
                                        sizeof(char_value), (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_CONNECT_TO_CLOUD_CFG] = {{ESP_GATT_AUTO_RSP},
                                       {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(c_ccc),
                                        (uint8_t *)c_ccc}},

    /****************************** PROVISION STATUS ******************************/
    /* Characteristic Declaration */
    [IDX_CHAR_PROV_STATUS] = {{ESP_GATT_AUTO_RSP},
                               {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_PROV_STATUS] = {{ESP_GATT_AUTO_RSP},
                                   {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_PROV_STATUS, ESP_GATT_PERM_READ, sizeof(s_prov_status_value),
                                    sizeof(s_prov_status_value), (uint8_t *)&s_prov_status_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_PROV_STATUS_CFG] = {{ESP_GATT_AUTO_RSP},
                                   {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t),
                                    sizeof(status_ccc), (uint8_t *)status_ccc}},
};


static void ble_security_init(void) {
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t resp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &resp_key, sizeof(uint8_t));
}

static void handle_write_event(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    ble_prov_timer_reset();
    if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA]) {
        if (ble_apply_eeprom_payload(param->write.value, param->write.len, "write-direct")) {
            esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (uint8_t *)&gRDEeprom);
            ble_schedule_full_eeprom_write("write-direct");
        } else {
            ESP_LOGW(GATTS_TABLE_TAG, "Unsupported EEPROM write len=%u", (unsigned)param->write.len);
        }
    } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_WIFI_SSID]) {
        ESP_LOGI(GATTS_TABLE_TAG, "WIFI SSID");

        if (!ble_packet_append(&ssid_packet, param->write.value, param->write.len, MAX_SSID_LENGTH, "SSID")) {
            ble_set_prov_status(PROV_STATE_ERROR);
            return;
        }

        ble_packet_copy_to_string(&ssid_packet, WIFI_SSID, sizeof(WIFI_SSID));
        if (password_packet.len > 0) {
            ble_set_prov_status(PROV_STATE_READY);
            (void)mqtt_enqueue_wifi_credentials(WIFI_SSID, WIFI_PASSWORD, true, "ble-auto");
        } else {
            ble_set_prov_status(PROV_STATE_WAIT_PASSWORD);
        }
    } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_WIFI_PASSWORD]) {
        ESP_LOGI(GATTS_TABLE_TAG, "WIFI Password");

        if (!ble_packet_append(&password_packet, param->write.value, param->write.len, MAX_PASSWORD_LENGTH, "password")) {
            ble_set_prov_status(PROV_STATE_ERROR);
            return;
        }

        ble_packet_copy_to_string(&password_packet, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
        if (ssid_packet.len == 0) {
            ble_set_prov_status(PROV_STATE_WAIT_SSID);
        } else {
            ble_set_prov_status(PROV_STATE_READY);
            (void)mqtt_enqueue_wifi_credentials(WIFI_SSID, WIFI_PASSWORD, true, "ble-auto");
        }
    } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_CONNECT_TO_CLOUD]) {
        if (ssid_packet.len == 0 || password_packet.len == 0) {
            ESP_LOGE(GATTS_TABLE_TAG, "Provisioning incomplete");
            ble_set_prov_status(PROV_STATE_ERROR);
        } else {
            ble_set_prov_status(PROV_STATE_APPLYING);
            bool queued = mqtt_enqueue_wifi_credentials(WIFI_SSID, WIFI_PASSWORD, true, "ble");
            if (queued) {
                ble_set_prov_status(PROV_STATE_DONE);
                ble_packet_reset(&ssid_packet);
                ble_packet_reset(&password_packet);
            } else {
                ble_set_prov_status(PROV_STATE_ERROR);
            }
        }
    }

    if (param->write.need_rsp) {
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
    }
}


void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            /* advertising start complete event to indicate advertising start successfully or failed */
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed");
            } else {
                ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
            } else {
                ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d", param->update_conn_params.status,
                     param->update_conn_params.min_int, param->update_conn_params.max_int, param->update_conn_params.conn_int, param->update_conn_params.latency,
                     param->update_conn_params.timeout);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(GATTS_TABLE_TAG, "BLE auth complete, bonded");
            } else {
                ESP_LOGE(GATTS_TABLE_TAG, "BLE auth failed, status=%d", param->ble_security.auth_cmpl.fail_reason);
            }
            break;
        default:
            break;
    }
}

static void prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        prepare_write_env->prepare_handle = param->write.handle;
        if (prepare_write_env->prepare_buf == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    } else {
        if (param->write.offset > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_OFFSET;
        } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_ATTR_LEN;
        }
    }
    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp) {
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL) {
            gatt_rsp->attr_value.len      = param->write.len;
            gatt_rsp->attr_value.handle   = param->write.handle;
            gatt_rsp->attr_value.offset   = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK) {
                ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK) {
        return;
    }

    memcpy(prepare_write_env->prepare_buf + param->write.offset, param->write.value, param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}

static void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf) {
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
         ESP_LOGI(GATTS_TABLE_TAG, "Lenght : %d", prepare_write_env->prepare_len);
         if (prepare_write_env->prepare_handle == ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA]) {
             if (ble_apply_eeprom_payload(prepare_write_env->prepare_buf, (size_t)prepare_write_env->prepare_len, "prepare-write")) {
                 esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (uint8_t *)&gRDEeprom);
                 ESP_LOGI(GATTS_TABLE_TAG, "Speed : %d", gRDEeprom.sel_idxStepMotors);
                 ble_schedule_full_eeprom_write("prepare-write");
                 ESP_LOGI(GATTS_TABLE_TAG, "Invio scrittura completata");
             } else {
                 ESP_LOGW(GATTS_TABLE_TAG, "Prepare EEPROM len too short: %d", prepare_write_env->prepare_len);
             }
         } else {
             ESP_LOGI(GATTS_TABLE_TAG, "Prepare/exec ignored for handle=%u", prepare_write_env->prepare_handle);
         }
    } else {
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
    prepare_write_env->prepare_handle = 0;
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {

            const uint8_t* mac = esp_bt_dev_get_address();
            char mac_str[19];
            snprintf(mac_str, sizeof(mac_str), "_%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            char device_name[30];
            snprintf(device_name, sizeof(device_name), "%s%s", DEVICE_NAME, mac_str);


            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(device_name);
            if (set_dev_name_ret) {
                ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
            }

            ble_security_init();

            // config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
            if (ret) {
                ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
            }
            adv_config_done |= ADV_CONFIG_FLAG;
            // config scan response data
            ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (ret) {
                ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
            }
            adv_config_done |= SCAN_RSP_CONFIG_FLAG;

            esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, SVC_INST_ID);
            if (create_attr_ret) {
                ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
            }
        } break;
        case ESP_GATTS_READ_EVT:
            comm_led_mark_activity_source("BLE_READ");
            if (!s_runtime_ready &&
                (param->read.handle == ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA] ||
                 param->read.handle == ble_handle_table[IDX_CHAR_VAL_POLLIING] ||
                 param->read.handle == ble_handle_table[IDX_CHAR_VAL_DEBUG_DATA])) {
                ESP_LOGI(GATTS_TABLE_TAG, "Read while booting: handle=%d", param->read.handle);
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            comm_led_mark_activity_source("BLE_WRITE");
            if (!param->write.is_prep) {
                ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d", param->write.handle, param->write.len);
                esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
                if (param->write.handle == ble_handle_table[IDX_CHAR_PROV_STATUS_CFG] && param->write.len == 2) {
                    uint16_t notify_en = param->write.value[1] << 8 | param->write.value[0];
                    s_prov_status_notify = (notify_en == 0x0001);
                } else {
                    handle_write_event(gatts_if, param);
                }
            } else {
                prepare_write_event_env(gatts_if, &prepare_write_env, param);
            }
            break;

        case ESP_GATTS_EXEC_WRITE_EVT:
            // the length of gattc prepare write data must be less than ESP_BLE_CHAR_VAL_LEN_MAX.
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            exec_write_event_env(&prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d", param->conf.status, param->conf.handle);
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
            comm_led_mark_activity_source("BLE_CONNECT");
            esp_log_buffer_hex(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
            s_conn_id = param->connect.conn_id;
            s_gatts_if = gatts_if;
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
            ble_prov_timer_reset();
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the iOS system, please refer to Apple official documents about the BLE connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20;  // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10;  // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;   // timeout = 400*10ms = 4000ms
            // start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
            comm_led_mark_activity_source("BLE_DISCONNECT");
            s_prov_status_notify = false;
            if (s_prov_timer != NULL) {
                xTimerStop(s_prov_timer, 0);
            }
            ble_prov_reset();
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            } else if (param->add_attr_tab.num_handle != HRS_IDX_NB) {
                ESP_LOGE(GATTS_TABLE_TAG,
                         "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)",
                         param->add_attr_tab.num_handle, HRS_IDX_NB);
            } else {
                ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d\n", param->add_attr_tab.num_handle);
                memcpy(ble_handle_table, param->add_attr_tab.handles, sizeof(ble_handle_table));
                esp_ble_gatts_start_service(ble_handle_table[IDX_SVC]);
                ble_init_runtime_placeholders();
                ble_set_runtime_ready(false);
            }
            break;
        }
        case ESP_GATTS_STOP_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        case ESP_GATTS_UNREG_EVT:
        case ESP_GATTS_DELETE_EVT:
        default:
            break;
    }
}

void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            data_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == data_profile_tab[idx].gatts_if) {
                if (data_profile_tab[idx].gatts_cb) {
                    data_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}
