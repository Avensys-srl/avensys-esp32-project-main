#include "ble.h"
#include "main.h"
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

extern bool  write_eeprom_data;
extern bool is_bluetooth;
extern uint16_t Read_Eeprom_Request_Index;
extern S_EEPROM gRDEeprom;

uint16_t ble_handle_table[HRS_IDX_NB];

typedef struct {
    uint8_t *prepare_buf;
    int      prepare_len;
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
static TimerHandle_t s_ble_provision_timer = NULL;
static uint8_t s_prov_status_value = 0;
static bool s_prov_status_notify = false;
static uint16_t s_conn_id = 0;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint8_t s_eeprom_stream_buf[PREPARE_BUF_MAX_SIZE];
static size_t s_eeprom_stream_len = 0;
static TickType_t s_eeprom_stream_last_tick = 0;

#define BLE_PROVISION_TIMEOUT_MS 90000

#define PROV_STATE_IDLE          0
#define PROV_STATE_WAIT_SSID     1
#define PROV_STATE_WAIT_PASSWORD 2
#define PROV_STATE_READY         3
#define PROV_STATE_APPLYING      4
#define PROV_STATE_DONE          5
#define PROV_STATE_ERROR         6
#define EEPROM_STREAM_TIMEOUT_MS 1500

static void ble_reset_credential_packets(void);
static bool ble_packet_append(ble_packet_t *packet, const uint8_t *data, size_t len, size_t max_len, const char *label);
static void ble_packet_copy_to_string(const ble_packet_t *packet, char *out, size_t out_size);
static void ble_provision_timeout_cb(TimerHandle_t xTimer);
static void ble_restart_provision_timer(void);
static void ble_stop_provision_timer(void);
static void ble_security_init(void);
static void ble_set_provision_status(uint8_t status);
static bool ble_apply_eeprom_payload(const uint8_t *payload, size_t payload_len, const char *source);

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

static void ble_reset_credential_packets(void) {
    if (ssid_packet.data != NULL) {
        free(ssid_packet.data);
        ssid_packet.data = NULL;
    }
    ssid_packet.len = 0;

    if (password_packet.data != NULL) {
        free(password_packet.data);
        password_packet.data = NULL;
    }
    password_packet.len = 0;
}

static void ble_set_provision_status(uint8_t status) {
    s_prov_status_value = status;

    if (ble_handle_table[IDX_CHAR_VAL_PROV_STATUS] != 0) {
        esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_PROV_STATUS], sizeof(s_prov_status_value), &s_prov_status_value);
    }

    if (s_prov_status_notify && s_gatts_if != ESP_GATT_IF_NONE && ble_handle_table[IDX_CHAR_VAL_PROV_STATUS] != 0) {
        (void)esp_ble_gatts_send_indicate(s_gatts_if,
                                          s_conn_id,
                                          ble_handle_table[IDX_CHAR_VAL_PROV_STATUS],
                                          sizeof(s_prov_status_value),
                                          &s_prov_status_value,
                                          false);
    }
}

static void ble_provision_timeout_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    ESP_LOGW(GATTS_TABLE_TAG, "BLE provisioning timeout, resetting pending SSID/password");
    ble_reset_credential_packets();
    wifi_is_ssid_send = false;
    wifi_is_pass_send = false;
    connect_to_wifi = false;
    ble_set_provision_status(PROV_STATE_ERROR);
}

static void ble_restart_provision_timer(void) {
    if (s_ble_provision_timer == NULL) {
        s_ble_provision_timer = xTimerCreate("ble_prov_to",
                                             pdMS_TO_TICKS(BLE_PROVISION_TIMEOUT_MS),
                                             pdFALSE,
                                             NULL,
                                             ble_provision_timeout_cb);
        if (s_ble_provision_timer == NULL) {
            ESP_LOGW(GATTS_TABLE_TAG, "Failed to create BLE provisioning timer");
            return;
        }
    }

    (void)xTimerStop(s_ble_provision_timer, 0);
    (void)xTimerStart(s_ble_provision_timer, 0);
}

static void ble_stop_provision_timer(void) {
    if (s_ble_provision_timer != NULL) {
        (void)xTimerStop(s_ble_provision_timer, 0);
    }
}

static void ble_security_init(void) {
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t resp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &resp_key, sizeof(resp_key));
}

static bool ble_apply_eeprom_payload(const uint8_t *payload, size_t payload_len, const char *source) {
    size_t required_len = sizeof(gRDEeprom);
    if (payload == NULL || payload_len < required_len) {
        ESP_LOGW(GATTS_TABLE_TAG, "EEPROM write ignored (%s): payload too short (%d < %d)",
                 source, (int)payload_len, (int)required_len);
        return false;
    }

    memcpy(&gRDEeprom, payload, required_len);
    ESP_LOGI(GATTS_TABLE_TAG, "EEPROM write accepted from %s, len=%d", source, (int)payload_len);

    Read_Eeprom_Request_Index |= 0x800;
    Read_Eeprom_Request_Index |= 0x1000;
    Read_Eeprom_Request_Index |= 0x2000;
    Read_Eeprom_Request_Index |= 0x4000;
    Read_Eeprom_Request_Index |= 0x8000;
    ESP_LOGI(GATTS_TABLE_TAG, "EEPROM write request queued");
    return true;
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


// Funzione per scrivere una stringa nella NVS
esp_err_t nvs_write_string(const char* key, const char* value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, key, value);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }

    err = nvs_commit(my_handle);
    nvs_close(my_handle);
    return err;
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
                ESP_LOGI(GATTS_TABLE_TAG, "BLE auth complete");
            } else {
                ESP_LOGW(GATTS_TABLE_TAG, "BLE auth failed, reason=%d", param->ble_security.auth_cmpl.fail_reason);
            }
            break;
        default:
            break;
    }
}

void prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
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

void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf) {
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
         ESP_LOGI(GATTS_TABLE_TAG, "Lenght : %d", prepare_write_env->prepare_len);
         (void)ble_apply_eeprom_payload(prepare_write_env->prepare_buf, prepare_write_env->prepare_len, "prepare-write");
        
    } else {
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
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
            // ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_READ_EVT");
            break;

case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep) {
                ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d", param->write.handle, param->write.len);
                esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);

                if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA]) {
                    TickType_t now = xTaskGetTickCount();
                    if (s_eeprom_stream_len > 0 &&
                        (now - s_eeprom_stream_last_tick) > pdMS_TO_TICKS(EEPROM_STREAM_TIMEOUT_MS)) {
                        ESP_LOGW(GATTS_TABLE_TAG, "EEPROM stream timeout, resetting partial buffer (%d bytes)", (int)s_eeprom_stream_len);
                        s_eeprom_stream_len = 0;
                    }

                    s_eeprom_stream_last_tick = now;

                    if (param->write.len >= sizeof(gRDEeprom)) {
                        (void)ble_apply_eeprom_payload(param->write.value, param->write.len, "write-direct");
                        s_eeprom_stream_len = 0;
                    } else {
                        if ((s_eeprom_stream_len + param->write.len) > sizeof(s_eeprom_stream_buf)) {
                            ESP_LOGE(GATTS_TABLE_TAG, "EEPROM stream overflow (%d + %d), dropping stream",
                                     (int)s_eeprom_stream_len, param->write.len);
                            s_eeprom_stream_len = 0;
                        } else {
                            memcpy(s_eeprom_stream_buf + s_eeprom_stream_len, param->write.value, param->write.len);
                            s_eeprom_stream_len += param->write.len;
                            ESP_LOGI(GATTS_TABLE_TAG, "EEPROM stream chunk received, total=%d", (int)s_eeprom_stream_len);

                            if (s_eeprom_stream_len >= sizeof(gRDEeprom)) {
                                (void)ble_apply_eeprom_payload(s_eeprom_stream_buf, s_eeprom_stream_len, "write-stream");
                                s_eeprom_stream_len = 0;
                            }
                        }
                    }
                } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_WIFI_SSID]) {
                    ESP_LOGI(GATTS_TABLE_TAG, "WIFI SSID");
                    ble_restart_provision_timer();

                    if (!ble_packet_append(&ssid_packet, param->write.value, param->write.len, MAX_SSID_LENGTH, "SSID")) {
                        ble_reset_credential_packets();
                        wifi_is_ssid_send = false;
                        connect_to_wifi = false;
                        break;
                    }

                    ble_packet_copy_to_string(&ssid_packet, WIFI_SSID, sizeof(WIFI_SSID));
                    wifi_is_ssid_send = true;
                    if (wifi_is_pass_send) {
                        ble_set_provision_status(PROV_STATE_READY);
                    } else {
                        ble_set_provision_status(PROV_STATE_WAIT_PASSWORD);
                    }

                    // Scrivi SSID nella NVS
                    esp_err_t err = nvs_write_string("wifi_ssid", WIFI_SSID);
                    if (err != ESP_OK) {
                        ESP_LOGE(GATTS_TABLE_TAG, "Error (%s) writing SSID to NVS!", esp_err_to_name(err));
                    } else {
                        ESP_LOGI(GATTS_TABLE_TAG, "SSID written to NVS successfully");
                    }
                } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_WIFI_PASSWORD]) {
                    ESP_LOGI(GATTS_TABLE_TAG, "WIFI Password");
                    ble_restart_provision_timer();

                    if (!ble_packet_append(&password_packet, param->write.value, param->write.len, MAX_PASSWORD_LENGTH, "password")) {
                        ble_reset_credential_packets();
                        wifi_is_pass_send = false;
                        connect_to_wifi = false;
                        break;
                    }

                    ble_packet_copy_to_string(&password_packet, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
                    wifi_is_pass_send = true;
                    if (wifi_is_ssid_send) {
                        ble_set_provision_status(PROV_STATE_READY);
                    } else {
                        ble_set_provision_status(PROV_STATE_WAIT_SSID);
                    }

                    // Scrivi Password nella NVS
                    esp_err_t err = nvs_write_string("wifi_pass", WIFI_PASSWORD);
                    if (err != ESP_OK) {
                        ESP_LOGE(GATTS_TABLE_TAG, "Error (%s) writing password to NVS!", esp_err_to_name(err));
                    } else {
                        ESP_LOGI(GATTS_TABLE_TAG, "Password written to NVS successfully");
                    }
                } else if (param->write.handle == ble_handle_table[IDX_CHAR_VAL_CONNECT_TO_CLOUD]) {
                    if (wifi_is_ssid_send && wifi_is_pass_send) {
                        ble_set_provision_status(PROV_STATE_APPLYING);
                    } else {
                        ble_set_provision_status(PROV_STATE_ERROR);
                    }
                } else if (param->write.handle == ble_handle_table[IDX_CHAR_PROV_STATUS_CFG] && param->write.len == 2) {
                    uint16_t notify_en = ((uint16_t)param->write.value[1] << 8) | param->write.value[0];
                    s_prov_status_notify = (notify_en == 0x0001);
                }

                connect_to_wifi = (wifi_is_ssid_send && wifi_is_pass_send);
                if (connect_to_wifi) {
                    ble_stop_provision_timer();
                    ble_reset_credential_packets();
                    ble_set_provision_status(PROV_STATE_DONE);
                }
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
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
            esp_log_buffer_hex(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
            s_conn_id = param->connect.conn_id;
            s_gatts_if = gatts_if;
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
            ble_restart_provision_timer();
            ble_set_provision_status(PROV_STATE_WAIT_SSID);
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
            ble_stop_provision_timer();
            ble_reset_credential_packets();
            s_eeprom_stream_len = 0;
            wifi_is_ssid_send = false;
            wifi_is_pass_send = false;
            connect_to_wifi = false;
            s_prov_status_notify = false;
            s_gatts_if = ESP_GATT_IF_NONE;
            ble_set_provision_status(PROV_STATE_IDLE);
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
                ble_set_provision_status(PROV_STATE_IDLE);
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
