/*
 * ESP BLE Mesh Example
 *
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "sdkconfig.h"
#include "wifi_connect.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "main.h"

#define GOT_IPV4_BIT BIT(0)

#define CONNECTED_BITS (GOT_IPV4_BIT)

// Define a timeout period for Wi-Fi connection attempts
#define WIFI_CONNECT_TIMEOUT_MS 30000

// Timer handle
static TimerHandle_t s_connect_timer;


static EventGroupHandle_t s_connect_event_group;
static esp_ip4_addr_t     s_ip_addr;
static const char        *s_connection_name;
static esp_netif_t       *s_wifi_esp_netif = NULL;

static const char *TAG = "wifi_connect";

extern bool Quarke_Partition_State;
extern TaskHandle_t Quarke_Update_Task_xHandle;

extern void check_update_task(void *pvParameter);
extern esp_err_t nvs_read_string(const char* key, char* value, size_t max_len);
extern void Quarke_Update_task (void *pvParameters);

/* set up connection, Wi-Fi or Ethernet */
static void start(const char *ssid, const char *passwd);

/* tear down connection, release resources */
static void stop(void);

// Function prototypes
static void connect_timer_callback(TimerHandle_t xTimer);


static void on_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Got IP event!");
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV4_BIT);

}



static void connect_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Connection attempt timed out, attempting to re-read SSID and password...");
    // Stop the Wi-Fi and delete the event group to trigger a re-read of SSID and password
    stop();
    // Call the function to re-read SSID and password here
    // e.g., read_ssid_password_from_bluetooth();
    // Restart Wi-Fi connection with new SSID and password
    esp_err_t err;
    
    err = nvs_read_string("wifi_ssid", WIFI_SSID, sizeof(WIFI_SSID));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SSID from NVS (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SSID read from NVS: %s", WIFI_SSID);
    }

    err = nvs_read_string("wifi_pass", WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read password from NVS (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Password read from NVS");
    }

    // Restart Wi-Fi connection with new SSID and password
    esp_err_t res = wifi_connect(WIFI_SSID, WIFI_PASSWORD);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconnect to Wi-Fi");
    }
}

void check_wifi_signal_strength(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
    } else {
        ESP_LOGI(TAG, "Failed to get Wi-Fi signal strength");
    }
}


esp_err_t wifi_connect(const char *ssid, const char *passwd) {
    if (s_connect_event_group != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_connect_event_group = xEventGroupCreate();
    start(ssid, passwd);
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&stop));
    ESP_LOGI(TAG, "Waiting for IP");

    // Start the connection timeout timer
    s_connect_timer = xTimerCreate("connect_timer", pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS), pdFALSE, NULL, connect_timer_callback);
    if (s_connect_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create connection timeout timer");
        return ESP_FAIL;
    }
    xTimerStart(s_connect_timer, 0);

    EventBits_t bits = xEventGroupWaitBits(s_connect_event_group, CONNECTED_BITS, pdTRUE, pdTRUE, portMAX_DELAY);
    if (bits & CONNECTED_BITS) {
        // Stop the timer since we successfully connected
        xTimerStop(s_connect_timer, 0);
        ESP_LOGI(TAG, "Connected to %s", s_connection_name);
        ESP_LOGI(TAG, "IPv4 address: " IPSTR, IP2STR(&s_ip_addr));
        
        // Start the firmware update task
        xTaskCreate(&check_update_task, "check_update_task", 8192, NULL, 5, NULL);
        //if ( Quarke_Partition_State )
		    //xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2*8192, NULL, 5, &Quarke_Update_Task_xHandle);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi within timeout period");
        // Stop the timer if the connection failed
        xTimerStop(s_connect_timer, 0);
        return ESP_FAIL;
    }
}


esp_err_t wifi_disconnect(void) {
    if (s_connect_event_group == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    vEventGroupDelete(s_connect_event_group);
    s_connect_event_group = NULL;
    stop();
    ESP_LOGI(TAG, "Disconnected from %s", s_connection_name);
    s_connection_name = NULL;
    return ESP_OK;
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
    ESP_LOGI(TAG, "Wi-Fi disconnected, reason: %d", event->reason);
    
    if (event->reason == WIFI_REASON_NO_AP_FOUND) {
        ESP_LOGE(TAG, "Access point not found");
    } else if (event->reason == WIFI_REASON_AUTH_FAIL) {
        ESP_LOGE(TAG, "Authentication failed");
    }

    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "Wi-Fi not started");
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void start(const char *ssid, const char *passwd) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();

    esp_netif_t *netif = esp_netif_new(&netif_config);

    assert(netif);

    esp_netif_attach_wifi_station(netif);
    esp_wifi_set_default_wifi_sta_handlers();

    s_wifi_esp_netif = netif;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    if (ssid) {
        strncpy((char *)wifi_config.sta.ssid, ssid, strlen(ssid));
    }
    if (passwd) {
        strncpy((char *)wifi_config.sta.password, passwd, strlen(passwd));
    }

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    s_connection_name = ssid;
}

static void stop(void) {
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_wifi_esp_netif));
    esp_netif_destroy(s_wifi_esp_netif);
    s_wifi_esp_netif = NULL;
}

esp_netif_t *get_wifi_netif(void) { return s_wifi_esp_netif; }
