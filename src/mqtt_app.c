#include "mqtt_app.h"

#include <ctype.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "main.h"

static const char *DISCOVERY_URL = "https://www.avensys-srl.com/mqtt-endpoint/index.php";
static const char *TAG_WIFI_MANAGER = "wifi_manager";
static const char *TAG_MQTT_ENDPOINT = "mqtt_endpoint";

#define MQTT_ENDPOINT_NVS_VERSION 1
#define MQTT_ENDPOINT_NVS_VER_KEY "mqtt_ep_ver"
#define MQTT_ENDPOINT_NVS_VALID_KEY "mqtt_ep_valid"
#define MQTT_ENDPOINT_NVS_HOST_KEY "mqtt_ep_host"
#define MQTT_ENDPOINT_NVS_PORT_KEY "mqtt_ep_port"
#define MQTT_ENDPOINT_NVS_TLS_KEY "mqtt_ep_tls"
#define MQTT_ENDPOINT_NVS_USER_KEY "mqtt_ep_user"
#define MQTT_ENDPOINT_NVS_PASS_KEY "mqtt_ep_pass"

#define MQTT_DEFAULT_HOST "3d89c9f5ced049bdae273a2e53b3441b.s1.eu.hivemq.cloud"
#define MQTT_DEFAULT_PORT 8883
#define MQTT_DEFAULT_TLS true
#define MQTT_DEFAULT_USERNAME ""
#define MQTT_DEFAULT_PASSWORD ""

#define MQTT_DISCOVERY_HTTP_TIMEOUT_MS 8000
#define MQTT_DISCOVERY_RESPONSE_MAX 512
#define MQTT_DISCOVERY_INTERVAL_MS (60 * 60 * 1000)
#define MQTT_DISCOVERY_FAIL_THRESHOLD 3
#define MQTT_RECONNECT_BASE_MS 1000
#define MQTT_RECONNECT_MAX_MS 60000
#define MQTT_HW_ID_LEN 12
#define EEPROM_SERIAL_MAX_LEN 18
#define MQTT_STATUS_INTERVAL_MS (60 * 1000)

#define WIFI_REQ_QUEUE_LEN 8
#define WIFI_MAX_RETRY 3
#define WIFI_RETRY_BASE_MS 500
#define WIFI_APPLY_COOLDOWN_MS 800

typedef enum {
    WIFI_MANAGER_IDLE = 0,
    WIFI_MANAGER_APPLYING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_FAILED,
} wifi_manager_state_t;

typedef struct {
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
    bool persist_to_nvs;
    uint32_t request_id;
    TickType_t enqueue_tick;
    char source[16];
} wifi_request_t;

esp_mqtt_client_handle_t client;
bool is_mqtt_ready = false;

static uint8_t s_subscribe_counter = 0;
static QueueHandle_t s_wifi_req_queue = NULL;
static wifi_manager_state_t s_wifi_manager_state = WIFI_MANAGER_IDLE;
static uint32_t s_wifi_request_counter = 0;
static TickType_t s_last_apply_tick = 0;
static bool s_mqtt_connected = false;
static bool s_mqtt_started = false;
static uint32_t s_mqtt_consecutive_failures = 0;
static uint32_t s_mqtt_reconnect_attempts = 0;
static TickType_t s_next_mqtt_retry_tick = 0;
static TickType_t s_last_discovery_tick = 0;
static bool s_endpoint_initialized = false;
static MqttEndpoint s_selected_endpoint;
static MqttEndpoint s_active_endpoint;
static char s_mqtt_uri[192];
static char s_device_hw_id[MQTT_HW_ID_LEN + 1] = {0};
static char s_active_serial[EEPROM_SERIAL_MAX_LEN + 1] = {0};
static char s_last_serial_subscribed[EEPROM_SERIAL_MAX_LEN + 1] = {0};
static bool s_hw_id_subscribed = false;
static TickType_t s_last_status_publish_tick = 0;
static uint32_t s_log_seq = 0;
static char s_last_error[128] = "none";

extern S_EEPROM gRDEeprom;
extern uint16_t Read_Eeprom_Request_Index;
extern bool Unit_Partition_State;
extern bool Unit_Update_task_Flag;
extern bool Bootloader_State_Flag;
extern TaskHandle_t Unit_Update_Task_xHandle;
extern bool Wifi_Connected_Flag;
extern void Unit_Update_task(void *pvParameters);

extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");

static void mqtt_publish_status(bool force);
static void mqtt_publish_log_event(const char *level, const char *code, const char *message);

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT_ENDPOINT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_set_last_error(const char *message) {
    if (message == NULL || message[0] == '\0') {
        strlcpy(s_last_error, "none", sizeof(s_last_error));
        return;
    }
    strlcpy(s_last_error, message, sizeof(s_last_error));
}

static bool mqtt_apply_eeprom_payload(const uint8_t *payload, size_t payload_len) {
    const size_t struct_len = sizeof(gRDEeprom);
    if (payload == NULL || payload_len < 241) {
        return false;
    }

    memset(&gRDEeprom, 0, struct_len);
    size_t copy_len = payload_len;
    if (copy_len > struct_len) {
        copy_len = struct_len;
    }
    memcpy(&gRDEeprom, payload, copy_len);
    ESP_LOGI(TAG_MQTT_ENDPOINT, "EEPROM write accepted len=%u copied=%u struct=%u",
             (unsigned)payload_len, (unsigned)copy_len, (unsigned)struct_len);
    return true;
}

static bool mqtt_extract_serial_from_eeprom(char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }

    size_t j = 0;
    for (size_t i = 0; i < EEPROM_SERIAL_MAX_LEN && j < (out_size - 1); ++i) {
        char c = (char)gRDEeprom.SerialString[i];
        if (c == '\0') {
            break;
        }
        if (!isprint((unsigned char)c)) {
            break;
        }
        out[j++] = c;
    }
    out[j] = '\0';

    while (j > 0 && isspace((unsigned char)out[j - 1])) {
        out[--j] = '\0';
    }

    return j > 0;
}

static bool mqtt_is_serial_valid(const char *serial) {
    if (serial == NULL || serial[0] == '\0') {
        return false;
    }

    bool has_meaningful = false;
    for (size_t i = 0; serial[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)serial[i];
        if (isspace(c)) {
            continue;
        }
        if (serial[i] != '0') {
            has_meaningful = true;
            break;
        }
    }
    return has_meaningful;
}

static bool mqtt_update_active_serial_from_eeprom(void) {
    char new_serial[EEPROM_SERIAL_MAX_LEN + 1] = {0};
    bool has_serial = mqtt_extract_serial_from_eeprom(new_serial, sizeof(new_serial));
    bool valid = has_serial && mqtt_is_serial_valid(new_serial);

    if (!valid) {
        if (s_active_serial[0] != '\0') {
            s_active_serial[0] = '\0';
            ESP_LOGI(TAG_MQTT_ENDPOINT, "Serial became null/invalid, legacy serial channel disabled");
            return true;
        }
        return false;
    }

    if (strcmp(s_active_serial, new_serial) != 0) {
        strlcpy(s_active_serial, new_serial, sizeof(s_active_serial));
        ESP_LOGI(TAG_MQTT_ENDPOINT, "Active serial updated: %s", s_active_serial);
        return true;
    }
    return false;
}

static void mqtt_init_hw_id_once(void) {
    if (s_device_hw_id[0] != '\0') {
        return;
    }

    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(s_device_hw_id, sizeof(s_device_hw_id), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG_MQTT_ENDPOINT, "Device HW ID (MAC/eFuse): %s", s_device_hw_id);
    } else {
        strlcpy(s_device_hw_id, "UNKNOWNHWID", sizeof(s_device_hw_id));
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Unable to read MAC/eFuse, fallback HW ID: %s", s_device_hw_id);
    }
}

typedef struct {
    char *buffer;
    size_t len;
    size_t capacity;
    bool overflow;
} discovery_http_ctx_t;

static void mqtt_endpoint_apply_default(MqttEndpoint *endpoint) {
    if (endpoint == NULL) {
        return;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    strlcpy(endpoint->host, MQTT_DEFAULT_HOST, sizeof(endpoint->host));
    endpoint->port = MQTT_DEFAULT_PORT;
    endpoint->tls = MQTT_DEFAULT_TLS;
    strlcpy(endpoint->username, MQTT_DEFAULT_USERNAME, sizeof(endpoint->username));
    strlcpy(endpoint->password, MQTT_DEFAULT_PASSWORD, sizeof(endpoint->password));
}

static bool mqtt_endpoint_is_valid(const MqttEndpoint *endpoint) {
    return endpoint != NULL && endpoint->host[0] != '\0' && endpoint->port != 0;
}

static bool mqtt_endpoint_equals(const MqttEndpoint *a, const MqttEndpoint *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    return a->port == b->port &&
           a->tls == b->tls &&
           strcmp(a->host, b->host) == 0 &&
           strcmp(a->username, b->username) == 0 &&
           strcmp(a->password, b->password) == 0;
}

static void mqtt_endpoint_log_sanitized(const char *prefix, const MqttEndpoint *endpoint) {
    if (!mqtt_endpoint_is_valid(endpoint)) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "%s invalid endpoint", prefix);
        return;
    }
    ESP_LOGI(TAG_MQTT_ENDPOINT, "%s host=%s port=%u tls=%d user=%s pass=%s",
             prefix,
             endpoint->host,
             endpoint->port,
             endpoint->tls ? 1 : 0,
             endpoint->username[0] ? endpoint->username : "<empty>",
             endpoint->password[0] ? "***" : "<empty>");
}

static esp_err_t discovery_http_event_handler(esp_http_client_event_t *evt) {
    discovery_http_ctx_t *ctx = (discovery_http_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx != NULL && evt->data != NULL && evt->data_len > 0) {
        size_t remaining = (ctx->capacity > ctx->len) ? (ctx->capacity - ctx->len - 1) : 0;
        size_t to_copy = (evt->data_len < remaining) ? (size_t)evt->data_len : remaining;

        if (to_copy > 0) {
            memcpy(ctx->buffer + ctx->len, evt->data, to_copy);
            ctx->len += to_copy;
            ctx->buffer[ctx->len] = '\0';
        }
        if ((size_t)evt->data_len > to_copy) {
            ctx->overflow = true;
        }
    }

    return ESP_OK;
}

bool LoadMqttEndpointFromNvs(MqttEndpoint *out) {
    if (out == NULL) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "NVS open failed for endpoint load: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t version = 0;
    uint8_t valid = 0;
    uint8_t tls_value = 0;
    uint16_t port = 0;
    MqttEndpoint tmp;
    memset(&tmp, 0, sizeof(tmp));

    err = nvs_get_u8(nvs_handle, MQTT_ENDPOINT_NVS_VER_KEY, &version);
    if (err != ESP_OK || version != MQTT_ENDPOINT_NVS_VERSION) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_u8(nvs_handle, MQTT_ENDPOINT_NVS_VALID_KEY, &valid);
    if (err != ESP_OK || valid != 1) {
        nvs_close(nvs_handle);
        return false;
    }

    size_t size = sizeof(tmp.host);
    err = nvs_get_str(nvs_handle, MQTT_ENDPOINT_NVS_HOST_KEY, tmp.host, &size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_u16(nvs_handle, MQTT_ENDPOINT_NVS_PORT_KEY, &port);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    tmp.port = port;

    err = nvs_get_u8(nvs_handle, MQTT_ENDPOINT_NVS_TLS_KEY, &tls_value);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    tmp.tls = (tls_value == 1);

    size = sizeof(tmp.username);
    err = nvs_get_str(nvs_handle, MQTT_ENDPOINT_NVS_USER_KEY, tmp.username, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        tmp.username[0] = '\0';
    } else if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    size = sizeof(tmp.password);
    err = nvs_get_str(nvs_handle, MQTT_ENDPOINT_NVS_PASS_KEY, tmp.password, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        tmp.password[0] = '\0';
    } else if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    if (!mqtt_endpoint_is_valid(&tmp)) {
        return false;
    }

    *out = tmp;
    return true;
}

bool SaveMqttEndpointToNvs(const MqttEndpoint *in) {
    if (!mqtt_endpoint_is_valid(in)) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "NVS open failed for endpoint save: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(nvs_handle, MQTT_ENDPOINT_NVS_VER_KEY, MQTT_ENDPOINT_NVS_VERSION);
    if (err == ESP_OK) err = nvs_set_u8(nvs_handle, MQTT_ENDPOINT_NVS_VALID_KEY, 1);
    if (err == ESP_OK) err = nvs_set_str(nvs_handle, MQTT_ENDPOINT_NVS_HOST_KEY, in->host);
    if (err == ESP_OK) err = nvs_set_u16(nvs_handle, MQTT_ENDPOINT_NVS_PORT_KEY, in->port);
    if (err == ESP_OK) err = nvs_set_u8(nvs_handle, MQTT_ENDPOINT_NVS_TLS_KEY, in->tls ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(nvs_handle, MQTT_ENDPOINT_NVS_USER_KEY, in->username);
    if (err == ESP_OK) err = nvs_set_str(nvs_handle, MQTT_ENDPOINT_NVS_PASS_KEY, in->password);
    if (err == ESP_OK) err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "NVS save endpoint failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool FetchMqttEndpointFromHttps(MqttEndpoint *out) {
    if (out == NULL) {
        return false;
    }

    char response_buffer[MQTT_DISCOVERY_RESPONSE_MAX];
    discovery_http_ctx_t ctx = {
        .buffer = response_buffer,
        .len = 0,
        .capacity = sizeof(response_buffer),
        .overflow = false,
    };
    response_buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = DISCOVERY_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = MQTT_DISCOVERY_HTTP_TIMEOUT_MS,
        .event_handler = discovery_http_event_handler,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (http_client == NULL) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(http_client);
    int status_code = esp_http_client_get_status_code(http_client);
    esp_http_client_cleanup(http_client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery HTTPS error: %s", esp_err_to_name(err));
        return false;
    }
    if (status_code != 200) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery HTTP status=%d", status_code);
        return false;
    }
    if (ctx.overflow) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery response truncated");
        return false;
    }

    cJSON *root = cJSON_Parse(response_buffer);
    if (root == NULL) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery JSON parse failed");
        return false;
    }

    MqttEndpoint parsed;
    memset(&parsed, 0, sizeof(parsed));

    cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "host");
    cJSON *port = cJSON_GetObjectItemCaseSensitive(root, "port");
    cJSON *tls = cJSON_GetObjectItemCaseSensitive(root, "tls");
    cJSON *username = cJSON_GetObjectItemCaseSensitive(root, "username");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");

    bool valid = cJSON_IsString(host) && host->valuestring != NULL &&
                 cJSON_IsNumber(port) &&
                 cJSON_IsBool(tls);
    if (valid) {
        strlcpy(parsed.host, host->valuestring, sizeof(parsed.host));
        parsed.port = (uint16_t)port->valueint;
        parsed.tls = cJSON_IsTrue(tls);

        if (cJSON_IsString(username) && username->valuestring != NULL) {
            strlcpy(parsed.username, username->valuestring, sizeof(parsed.username));
        }
        if (cJSON_IsString(password) && password->valuestring != NULL) {
            strlcpy(parsed.password, password->valuestring, sizeof(parsed.password));
        }
        valid = mqtt_endpoint_is_valid(&parsed);
    }

    cJSON_Delete(root);
    if (!valid) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery JSON missing/invalid fields");
        return false;
    }

    *out = parsed;
    return true;
}

static void mqtt_endpoint_init_once(void) {
    if (s_endpoint_initialized) {
        return;
    }

    mqtt_endpoint_apply_default(&s_selected_endpoint);
    if (LoadMqttEndpointFromNvs(&s_selected_endpoint)) {
        mqtt_endpoint_log_sanitized("Loaded endpoint from NVS:", &s_selected_endpoint);
    } else {
        mqtt_endpoint_log_sanitized("NVS endpoint not available, using hardcoded default:", &s_selected_endpoint);
    }
    s_endpoint_initialized = true;
}

static bool mqtt_try_endpoint_discovery(bool force, const char *reason) {
    TickType_t now = xTaskGetTickCount();
    bool periodic_due = (now - s_last_discovery_tick) >= pdMS_TO_TICKS(MQTT_DISCOVERY_INTERVAL_MS);
    bool should_attempt = force || periodic_due || (s_mqtt_consecutive_failures >= MQTT_DISCOVERY_FAIL_THRESHOLD);

    if (!should_attempt) {
        return false;
    }
    if (!Wifi_Connected_Flag) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Skip discovery (%s): Wi-Fi disconnected", reason);
        mqtt_set_last_error("discovery skipped: wifi disconnected");
        return false;
    }

    s_last_discovery_tick = now;

    MqttEndpoint fetched;
    if (FetchMqttEndpointFromHttps(&fetched)) {
        if (fetched.username[0] == '\0') {
            strlcpy(fetched.username, s_selected_endpoint.username, sizeof(fetched.username));
        }
        if (fetched.password[0] == '\0') {
            strlcpy(fetched.password, s_selected_endpoint.password, sizeof(fetched.password));
        }
        s_selected_endpoint = fetched;
        mqtt_endpoint_log_sanitized("Discovery success, selected endpoint:", &s_selected_endpoint);
        mqtt_set_last_error("none");
        mqtt_publish_log_event("INFO", "DISCOVERY_OK", "endpoint discovery success");
        mqtt_publish_status(true);
        if (!SaveMqttEndpointToNvs(&s_selected_endpoint)) {
            ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery success but NVS save failed");
            mqtt_set_last_error("discovery nvs save failed");
            mqtt_publish_log_event("WARN", "DISCOVERY_SAVE_NVS_FAIL", "endpoint discovery save to nvs failed");
        }
        return true;
    }

    if (LoadMqttEndpointFromNvs(&s_selected_endpoint)) {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery failed (%s), fallback to NVS endpoint", reason);
        mqtt_endpoint_log_sanitized("Fallback endpoint:", &s_selected_endpoint);
        mqtt_set_last_error("discovery failed fallback nvs");
        mqtt_publish_log_event("WARN", "DISCOVERY_FALLBACK_NVS", "endpoint discovery failed fallback nvs");
    } else {
        ESP_LOGW(TAG_MQTT_ENDPOINT, "Discovery failed (%s), fallback to hardcoded endpoint", reason);
        mqtt_endpoint_apply_default(&s_selected_endpoint);
        mqtt_endpoint_log_sanitized("Fallback endpoint:", &s_selected_endpoint);
        mqtt_set_last_error("discovery failed fallback default");
        mqtt_publish_log_event("WARN", "DISCOVERY_FALLBACK_DEFAULT", "endpoint discovery failed fallback default");
    }
    mqtt_publish_status(true);
    return false;
}

static void mqtt_compute_next_retry(void) {
    uint32_t shift = (s_mqtt_consecutive_failures > 0) ? (s_mqtt_consecutive_failures - 1) : 0;
    if (shift > 6) {
        shift = 6;
    }
    uint32_t delay_ms = MQTT_RECONNECT_BASE_MS << shift;
    if (delay_ms > MQTT_RECONNECT_MAX_MS) {
        delay_ms = MQTT_RECONNECT_MAX_MS;
    }
    s_next_mqtt_retry_tick = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT reconnect backoff=%lu ms failures=%lu",
             (unsigned long)delay_ms, (unsigned long)s_mqtt_consecutive_failures);
}

bool mqtt_enqueue_wifi_credentials(const char *ssid, const char *password, bool persist_to_nvs, const char *source_tag) {
    if (s_wifi_req_queue == NULL) {
        s_wifi_req_queue = xQueueCreate(WIFI_REQ_QUEUE_LEN, sizeof(wifi_request_t));
    }

    if (s_wifi_req_queue == NULL || ssid == NULL || password == NULL) {
        return false;
    }

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG_WIFI_MANAGER, "Rejected Wi-Fi request: empty SSID");
        return false;
    }

    wifi_request_t request;
    memset(&request, 0, sizeof(request));
    strlcpy(request.ssid, ssid, sizeof(request.ssid));
    strlcpy(request.password, password, sizeof(request.password));
    request.persist_to_nvs = persist_to_nvs;
    request.enqueue_tick = xTaskGetTickCount();
    request.request_id = ++s_wifi_request_counter;
    strlcpy(request.source, source_tag ? source_tag : "unknown", sizeof(request.source));

    if (xQueueSend(s_wifi_req_queue, &request, 0) != pdTRUE) {
        // Queue full: drop oldest and keep the latest request.
        wifi_request_t discarded;
        (void)xQueueReceive(s_wifi_req_queue, &discarded, 0);
        if (xQueueSend(s_wifi_req_queue, &request, 0) != pdTRUE) {
            ESP_LOGW(TAG_WIFI_MANAGER, "Unable to enqueue Wi-Fi request id=%lu from %s", (unsigned long)request.request_id, request.source);
            return false;
        }
    }

    ESP_LOGI(TAG_WIFI_MANAGER, "Queued Wi-Fi request id=%lu source=%s ssidLen=%u passLen=%u",
             (unsigned long)request.request_id, request.source, (unsigned)strlen(request.ssid), (unsigned)strlen(request.password));
    return true;
}

static void mqtt_build_topic(char *buffer, size_t buffer_size, const char *address, const char *suffix) {
    if (address == NULL || suffix == NULL) {
        if (buffer_size > 0) {
            buffer[0] = '\0';
        }
        return;
    }
    snprintf(buffer, buffer_size, "/%s/%s", address, suffix);
}

static void mqtt_subscribe_identity_topics(const char *identity) {
    char topic_buffer[64];
    int msg_id;

    if (identity == NULL || strlen(identity) == 0 || client == NULL) {
        return;
    }

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), identity, "app/eeprom");
    msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
    ESP_LOGI(TAG_MQTT_ENDPOINT, "sent subscribe successful, msg_id=%d", msg_id);

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), identity, "app/request");
    msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
    ESP_LOGI(TAG_MQTT_ENDPOINT, "sent subscribe successful, msg_id=%d", msg_id);
}

static void mqtt_unsubscribe_identity_topics(const char *identity) {
    char topic_buffer[64];

    if (identity == NULL || strlen(identity) == 0 || client == NULL) {
        return;
    }

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), identity, "app/eeprom");
    (void)esp_mqtt_client_unsubscribe(client, topic_buffer);
    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), identity, "app/request");
    (void)esp_mqtt_client_unsubscribe(client, topic_buffer);
}

static void mqtt_refresh_app_subscriptions(bool force) {
    mqtt_init_hw_id_once();
    (void)mqtt_update_active_serial_from_eeprom();

    if (client == NULL) {
        return;
    }

    if (force || !s_hw_id_subscribed) {
        mqtt_subscribe_identity_topics(s_device_hw_id);
        s_hw_id_subscribed = true;
        ESP_LOGI(TAG_MQTT_ENDPOINT, "Subscribed app topics on HW ID channel: %s", s_device_hw_id);
    }

    if (s_active_serial[0] == '\0') {
        if (s_last_serial_subscribed[0] != '\0') {
            mqtt_unsubscribe_identity_topics(s_last_serial_subscribed);
            ESP_LOGI(TAG_MQTT_ENDPOINT, "Unsubscribed legacy serial channel: %s", s_last_serial_subscribed);
            s_last_serial_subscribed[0] = '\0';
        }
        return;
    }

    if (force || strcmp(s_active_serial, s_last_serial_subscribed) != 0) {
        if (s_last_serial_subscribed[0] != '\0') {
            mqtt_unsubscribe_identity_topics(s_last_serial_subscribed);
            ESP_LOGI(TAG_MQTT_ENDPOINT, "Unsubscribed old legacy serial channel: %s", s_last_serial_subscribed);
        }
        mqtt_subscribe_identity_topics(s_active_serial);
        strlcpy(s_last_serial_subscribed, s_active_serial, sizeof(s_last_serial_subscribed));
        ESP_LOGI(TAG_MQTT_ENDPOINT, "Subscribed legacy serial channel: %s", s_last_serial_subscribed);
    }
}

void mqtt_subscribe_app_topics(const char *address) {
    (void)address;
    mqtt_refresh_app_subscriptions(false);
}

static bool mqtt_topic_equals(const esp_mqtt_event_handle_t event, const char *expected) {
    size_t expected_len;

    if (event == NULL || expected == NULL) {
        return false;
    }

    expected_len = strlen(expected);
    return event->topic_len == (int)expected_len && memcmp(event->topic, expected, expected_len) == 0;
}

static void mqtt_publish_json_identity(const char *identity, const char *suffix, const char *payload) {
    char topic_buffer[96];

    if (identity == NULL || identity[0] == '\0' || suffix == NULL || payload == NULL) {
        return;
    }
    if (client == NULL || !s_mqtt_connected) {
        return;
    }

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), identity, suffix);
    (void)esp_mqtt_client_publish(client, topic_buffer, payload, 0, 1, 0);
}

static void mqtt_publish_json_dual_channel(const char *suffix, const char *payload) {
    mqtt_publish_json_identity(s_device_hw_id, suffix, payload);
    if (s_active_serial[0] != '\0' && strcmp(s_active_serial, s_device_hw_id) != 0) {
        mqtt_publish_json_identity(s_active_serial, suffix, payload);
    }
}

static void mqtt_publish_status(bool force) {
    TickType_t now = xTaskGetTickCount();
    char json_buffer[384];

    if (client == NULL || !s_mqtt_connected) {
        return;
    }
    if (!force && s_last_status_publish_tick != 0 &&
        (now - s_last_status_publish_tick) < pdMS_TO_TICKS(MQTT_STATUS_INTERVAL_MS)) {
        return;
    }

    snprintf(json_buffer, sizeof(json_buffer),
             "{\"online\":true,\"wifi\":%d,\"mqtt\":%d,\"hw_id\":\"%s\",\"serial\":\"%s\",\"uptime_s\":%lu,\"reconnect_attempts\":%lu,\"consecutive_failures\":%lu,\"last_error\":\"%s\"}",
             Wifi_Connected_Flag ? 1 : 0,
             s_mqtt_connected ? 1 : 0,
             s_device_hw_id,
             s_active_serial[0] ? s_active_serial : "",
             (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ),
             (unsigned long)s_mqtt_reconnect_attempts,
             (unsigned long)s_mqtt_consecutive_failures,
             s_last_error);

    mqtt_publish_json_dual_channel("esp/status", json_buffer);
    s_last_status_publish_tick = now;
}

static void mqtt_publish_log_event(const char *level, const char *code, const char *message) {
    char json_buffer[384];

    if (client == NULL || !s_mqtt_connected || level == NULL || code == NULL || message == NULL) {
        return;
    }

    s_log_seq++;
    snprintf(json_buffer, sizeof(json_buffer),
             "{\"seq\":%lu,\"level\":\"%s\",\"code\":\"%s\",\"msg\":\"%s\",\"hw_id\":\"%s\",\"serial\":\"%s\",\"uptime_s\":%lu}",
             (unsigned long)s_log_seq,
             level,
             code,
             message,
             s_device_hw_id,
             s_active_serial[0] ? s_active_serial : "",
             (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ));

    mqtt_publish_json_dual_channel("esp/log", json_buffer);
}

void publish_debug_message(const uint8_t *data, size_t data_len, const char *topic, const char *address) {
    char json_buffer[1024];
    int json_len;

    json_len = snprintf(json_buffer, sizeof(json_buffer), "{\"message\":\"");

    for (size_t i = 0; i < data_len; i++) {
        json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "%d", data[i]);
        if (i < data_len - 1) {
            json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, ",");
        }
    }

    json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "\",\"topic\":\"%s\",\"address\":\"%s\"}", topic, address);

    if (is_mqtt_ready) {
        int msg_id = esp_mqtt_client_publish(client, topic, json_buffer, json_len, 1, 0);
        ESP_LOGI(TAG_MQTT_ENDPOINT, "%s publish successful, msg_id=%d", topic, msg_id);
    }
}

void mqtt_publish_with_suffix(const char *address, const char *suffix, const uint8_t *data, size_t data_len) {
    char topic_buffer[64];
    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, suffix);
    publish_debug_message(data, data_len, topic_buffer, address);
}

void mqtt_publish_eeprom(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/eeprom", data, data_len);
}

void mqtt_publish_polling(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/polling", data, data_len);
}

void mqtt_publish_debug(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/debug", data, data_len);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG_MQTT_ENDPOINT, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client                        = event->client;
    char serial_address[EEPROM_SERIAL_MAX_LEN + 1] = {0};
    char hw_request_topic[64] = {0};
    char hw_eeprom_topic[64] = {0};
    char serial_request_topic[64] = {0};
    char serial_eeprom_topic[64] = {0};
    bool serial_valid;

    mqtt_init_hw_id_once();
    (void)mqtt_update_active_serial_from_eeprom();
    strlcpy(serial_address, s_active_serial, sizeof(serial_address));
    serial_valid = serial_address[0] != '\0';

    mqtt_build_topic(hw_eeprom_topic, sizeof(hw_eeprom_topic), s_device_hw_id, "app/eeprom");
    mqtt_build_topic(hw_request_topic, sizeof(hw_request_topic), s_device_hw_id, "app/request");
    if (serial_valid) {
        mqtt_build_topic(serial_eeprom_topic, sizeof(serial_eeprom_topic), serial_address, "app/eeprom");
        mqtt_build_topic(serial_request_topic, sizeof(serial_request_topic), serial_address, "app/request");
    }

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_CONNECTED");
            s_mqtt_connected = true;
            s_mqtt_consecutive_failures = 0;
            s_mqtt_reconnect_attempts = 0;
            s_next_mqtt_retry_tick = 0;
            mqtt_set_last_error("none");
            comm_led_set_fault(false);
            comm_led_mark_activity_source("MQTT_CONNECTED");
            s_hw_id_subscribed = false;
            s_last_serial_subscribed[0] = '\0';
            mqtt_refresh_app_subscriptions(true);
            mqtt_publish_log_event("INFO", "MQTT_CONNECTED", "mqtt connected");
            mqtt_publish_status(true);
            if ((Unit_Partition_State) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2 * 8192, NULL, 5, &Unit_Update_Task_xHandle);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_DISCONNECTED");
            is_mqtt_ready = false;
            s_subscribe_counter = 0;
            s_mqtt_connected = false;
            mqtt_set_last_error("mqtt disconnected");
            comm_led_set_fault(true);
            mqtt_publish_log_event("WARN", "MQTT_DISCONNECTED", "mqtt disconnected");
            mqtt_publish_status(true);
            s_mqtt_consecutive_failures++;
            mqtt_compute_next_retry();
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            comm_led_mark_activity_source("MQTT_SUBSCRIBED");
            if (s_subscribe_counter == 1) {
                is_mqtt_ready = true;
                s_subscribe_counter = 0;
                ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_SUBSCRIBTION FINISHED.");
            } else {
                s_subscribe_counter++;
            }
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            comm_led_mark_activity_source("MQTT_PUBLISHED");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_DATA");
            comm_led_mark_activity_source("MQTT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            if (mqtt_topic_equals(event, hw_eeprom_topic) || (serial_valid && mqtt_topic_equals(event, serial_eeprom_topic))) {
                if (!mqtt_apply_eeprom_payload((const uint8_t *)event->data, (size_t)event->data_len)) {
                    ESP_LOGW(TAG_MQTT_ENDPOINT,
                             "EEPROM write ignored: invalid payload len=%d expected-min=241 expected-struct=%u",
                             event->data_len,
                             (unsigned)sizeof(gRDEeprom));
                    mqtt_set_last_error("eeprom write invalid payload length");
                    mqtt_publish_log_event("WARN", "EEPROM_WRITE_LEN_INVALID", "eeprom write rejected invalid payload length");
                    mqtt_publish_status(true);
                    break;
                }

                ESP_LOGI(TAG_MQTT_ENDPOINT, "Speed : %d", gRDEeprom.sel_idxStepMotors);
                Read_Eeprom_Request_Index |= 0x800;
                Read_Eeprom_Request_Index |= 0x1000;
                Read_Eeprom_Request_Index |= 0x2000;
                Read_Eeprom_Request_Index |= 0x4000;
                Read_Eeprom_Request_Index |= 0x8000;
                ESP_LOGI(TAG_MQTT_ENDPOINT, "Invio scrittura completata");
                mqtt_set_last_error("none");
                mqtt_publish_log_event("INFO", "EEPROM_WRITE_OK", "eeprom write completed");
                mqtt_publish_status(true);
                if (mqtt_update_active_serial_from_eeprom()) {
                    mqtt_refresh_app_subscriptions(false);
                }
            }

            if (mqtt_topic_equals(event, hw_request_topic) || (serial_valid && mqtt_topic_equals(event, serial_request_topic))) {
                printf("READ DATA EEPROM\r\n");
                if (is_mqtt_ready) {
                    const char *publish_address = serial_valid ? serial_address : s_device_hw_id;
                    mqtt_publish_eeprom(publish_address, (u_int8_t *)&gRDEeprom, sizeof(gRDEeprom));
                    ESP_LOGI(TAG_MQTT_ENDPOINT, "eeprom sent publish successful");
                }
            }

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT_EVENT_ERROR");
            is_mqtt_ready = false;
            s_subscribe_counter = 0;
            s_mqtt_connected = false;
            mqtt_set_last_error("mqtt transport error");
            comm_led_set_fault(true);
            mqtt_publish_log_event("ERROR", "MQTT_TRANSPORT_ERROR", "mqtt transport error");
            mqtt_publish_status(true);
            s_mqtt_consecutive_failures++;
            mqtt_compute_next_retry();
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG_MQTT_ENDPOINT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            if ((Unit_Partition_State) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2 * 8192, NULL, 5, &Unit_Update_Task_xHandle);
            }
            break;
        default:
            ESP_LOGI(TAG_MQTT_ENDPOINT, "Other event id:%d", event->event_id);
            break;
    }
}

static bool mqtt_app_start_with_endpoint(const MqttEndpoint *endpoint) {
    if (!mqtt_endpoint_is_valid(endpoint)) {
        ESP_LOGE(TAG_MQTT_ENDPOINT, "Cannot start MQTT: invalid endpoint");
        return false;
    }

    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "%s://%s:%u", endpoint->tls ? "mqtts" : "mqtt", endpoint->host, endpoint->port);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                     = s_mqtt_uri,
        .broker.verification.crt_bundle_attach  = esp_crt_bundle_attach,
        .credentials.authentication.certificate = (const char *)certificate_pem_crt_start,
        .credentials.authentication.key         = (const char *)private_pem_key_start,
        .credentials.username                   = endpoint->username[0] ? endpoint->username : NULL,
        .credentials.authentication.password    = endpoint->password[0] ? endpoint->password : NULL,
        .network.disable_auto_reconnect         = true,
    };

    if (client != NULL) {
        (void)esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    ESP_LOGI(TAG_MQTT_ENDPOINT, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    mqtt_endpoint_log_sanitized("MQTT start with endpoint:", endpoint);
    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT_ENDPOINT, "esp_mqtt_client_init failed");
        s_mqtt_started = false;
        return false;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MQTT_ENDPOINT, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client);
        client = NULL;
        s_mqtt_started = false;
        return false;
    }

    s_mqtt_started = true;
    s_mqtt_connected = false;
    s_active_endpoint = *endpoint;
    return true;
}

static void mqtt_connection_maintenance(void) {
    bool serial_changed;

    mqtt_endpoint_init_once();
    mqtt_init_hw_id_once();
    serial_changed = mqtt_update_active_serial_from_eeprom();

    if (!Wifi_Connected_Flag) {
        return;
    }

    if (!s_mqtt_started) {
        (void)mqtt_try_endpoint_discovery(true, "initial-start");
        if (!mqtt_app_start_with_endpoint(&s_selected_endpoint)) {
            s_mqtt_consecutive_failures++;
            mqtt_compute_next_retry();
        }
        return;
    }

    if (is_mqtt_ready || s_mqtt_connected) {
        mqtt_publish_status(false);
        if (serial_changed) {
            mqtt_refresh_app_subscriptions(false);
        }
        if ((xTaskGetTickCount() - s_last_discovery_tick) >= pdMS_TO_TICKS(MQTT_DISCOVERY_INTERVAL_MS)) {
            bool updated = mqtt_try_endpoint_discovery(true, "periodic-refresh");
            if (updated && !mqtt_endpoint_equals(&s_selected_endpoint, &s_active_endpoint)) {
                ESP_LOGI(TAG_MQTT_ENDPOINT, "Endpoint changed by periodic discovery, restarting MQTT client");
                if (!mqtt_app_start_with_endpoint(&s_selected_endpoint)) {
                    s_mqtt_consecutive_failures++;
                    mqtt_compute_next_retry();
                }
            }
        }
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_next_mqtt_retry_tick != 0 && now < s_next_mqtt_retry_tick) {
        return;
    }

    bool updated = mqtt_try_endpoint_discovery(false, "reconnect");
    if (updated && !mqtt_endpoint_equals(&s_selected_endpoint, &s_active_endpoint)) {
        ESP_LOGI(TAG_MQTT_ENDPOINT, "Endpoint changed by discovery, recreating MQTT client");
        s_mqtt_reconnect_attempts++;
        if (!mqtt_app_start_with_endpoint(&s_selected_endpoint)) {
            s_mqtt_consecutive_failures++;
            mqtt_compute_next_retry();
        }
        return;
    }

    if (client != NULL) {
        s_mqtt_reconnect_attempts++;
        esp_err_t reconnect_err = esp_mqtt_client_reconnect(client);
        if (reconnect_err == ESP_OK) {
            ESP_LOGI(TAG_MQTT_ENDPOINT, "MQTT reconnect triggered");
        } else {
            ESP_LOGW(TAG_MQTT_ENDPOINT, "MQTT reconnect failed: %s", esp_err_to_name(reconnect_err));
            s_mqtt_consecutive_failures++;
            mqtt_compute_next_retry();
        }
        return;
    }

    s_mqtt_reconnect_attempts++;
    if (!mqtt_app_start_with_endpoint(&s_selected_endpoint)) {
        s_mqtt_consecutive_failures++;
        mqtt_compute_next_retry();
    }
}

static esp_err_t apply_wifi_request(const wifi_request_t *request) {
    esp_err_t err;
    int retry = 0;
    TickType_t now = xTaskGetTickCount();

    if (Wifi_Connected_Flag &&
        strcmp(WIFI_SSID, request->ssid) == 0 &&
        strcmp(WIFI_PASSWORD, request->password) == 0) {
        s_wifi_manager_state = WIFI_MANAGER_CONNECTED;
        ESP_LOGI(TAG_WIFI_MANAGER, "Skip Wi-Fi request id=%lu: credentials unchanged", (unsigned long)request->request_id);
        return ESP_OK;
    }

    if ((now - s_last_apply_tick) < pdMS_TO_TICKS(WIFI_APPLY_COOLDOWN_MS)) {
        TickType_t wait_ticks = pdMS_TO_TICKS(WIFI_APPLY_COOLDOWN_MS) - (now - s_last_apply_tick);
        vTaskDelay(wait_ticks);
    }

    if (request->persist_to_nvs) {
        err = nvs_write_string("wifi_ssid", request->ssid);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIFI_MANAGER, "NVS write SSID failed id=%lu err=%s", (unsigned long)request->request_id, esp_err_to_name(err));
            return err;
        }
        err = nvs_write_string("wifi_pass", request->password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIFI_MANAGER, "NVS write PASSWORD failed id=%lu err=%s", (unsigned long)request->request_id, esp_err_to_name(err));
            return err;
        }
    }

    strlcpy(WIFI_SSID, request->ssid, sizeof(WIFI_SSID));
    strlcpy(WIFI_PASSWORD, request->password, sizeof(WIFI_PASSWORD));

    s_wifi_manager_state = WIFI_MANAGER_APPLYING;
    (void)wifi_disconnect();

    while (retry < WIFI_MAX_RETRY) {
        TickType_t attempt_start = xTaskGetTickCount();
        err = wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        TickType_t elapsed_ms = (xTaskGetTickCount() - attempt_start) * portTICK_PERIOD_MS;

        if (err == ESP_OK) {
            s_wifi_manager_state = WIFI_MANAGER_CONNECTED;
            s_last_apply_tick = xTaskGetTickCount();
            ESP_LOGI(TAG_WIFI_MANAGER, "Wi-Fi request id=%lu applied in %lu ms (retry=%d)",
                     (unsigned long)request->request_id, (unsigned long)elapsed_ms, retry);
            return ESP_OK;
        }

        retry++;
        s_wifi_manager_state = WIFI_MANAGER_FAILED;
        ESP_LOGW(TAG_WIFI_MANAGER, "Wi-Fi request id=%lu failed retry=%d err=%s elapsed=%lu ms",
                 (unsigned long)request->request_id, retry, esp_err_to_name(err), (unsigned long)elapsed_ms);

        if (retry < WIFI_MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_BASE_MS * retry));
        }
    }

    return err;
}

void mqtt_task(void *arg) {
    wifi_request_t request;
    wifi_request_t latest;

    if (s_wifi_req_queue == NULL) {
        s_wifi_req_queue = xQueueCreate(WIFI_REQ_QUEUE_LEN, sizeof(wifi_request_t));
    }
    if (s_wifi_req_queue == NULL) {
        ESP_LOGE(TAG_WIFI_MANAGER, "Failed to create Wi-Fi request queue");
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    mqtt_init_hw_id_once();
    (void)mqtt_update_active_serial_from_eeprom();
    mqtt_endpoint_init_once();
    (void)mqtt_enqueue_wifi_credentials(WIFI_SSID, WIFI_PASSWORD, false, "boot");

    while (1) {
        if (xQueueReceive(s_wifi_req_queue, &request, pdMS_TO_TICKS(200)) == pdTRUE) {
            latest = request;
            while (xQueueReceive(s_wifi_req_queue, &request, 0) == pdTRUE) {
                latest = request;
            }

            TickType_t queue_delay_ms = (xTaskGetTickCount() - latest.enqueue_tick) * portTICK_PERIOD_MS;
            ESP_LOGI(TAG_WIFI_MANAGER, "Applying Wi-Fi request id=%lu source=%s queueDelay=%lu ms",
                     (unsigned long)latest.request_id, latest.source, (unsigned long)queue_delay_ms);

            if (apply_wifi_request(&latest) == ESP_OK) {
                mqtt_connection_maintenance();
            }
        }

        mqtt_connection_maintenance();
    }
}
