#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[128];
    uint16_t port;
    bool tls;
    char username[64];
    char password[64];
} MqttEndpoint;

void mqtt_task(void *arg);
void publish_debug_message(const uint8_t *data, size_t data_len, const char *topic, const char *address);
void mqtt_subscribe_app_topics(const char *address);
void mqtt_publish_with_suffix(const char *address, const char *suffix, const uint8_t *data, size_t data_len);
void mqtt_publish_eeprom(const char *address, const uint8_t *data, size_t data_len);
void mqtt_publish_polling(const char *address, const uint8_t *data, size_t data_len);
void mqtt_publish_debug(const char *address, const uint8_t *data, size_t data_len);
bool mqtt_enqueue_wifi_credentials(const char *ssid, const char *password, bool persist_to_nvs, const char *source_tag);
bool FetchMqttEndpointFromHttps(MqttEndpoint *out);
bool LoadMqttEndpointFromNvs(MqttEndpoint *out);
bool SaveMqttEndpointToNvs(const MqttEndpoint *in);

extern esp_mqtt_client_handle_t client;
extern bool is_mqtt_ready;

#ifdef __cplusplus
}
#endif

#endif
