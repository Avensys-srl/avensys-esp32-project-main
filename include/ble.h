#ifndef BLE_H
#define BLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include "comm_manager.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_bt_device.h"
#include "main.h"

#define GATTS_TABLE_TAG "ESP_BLE"

#define PROFILE_NUM     1
#define PROFILE_APP_IDX 0
#define ESP_APP_ID      0x55
#define DEVICE_NAME     "AVENSYS"
#define SVC_INST_ID     0

extern uint16_t ble_handle_table[];

extern uint16_t temp_ble;

/* Attributes State Machine */
enum {
    IDX_SVC,

    IDX_CHAR_EEPROM_DATA,
    IDX_CHAR_VAL_EEPROM_DATA,
    IDX_CHAR_EEPROM_DATA_CFG,

    IDX_CHAR_DEBUG_DATA,
    IDX_CHAR_VAL_DEBUG_DATA,
    IDX_CHAR_DEBUG_DATA_CFG,

    IDX_CHAR_POLLIING,
    IDX_CHAR_VAL_POLLIING,
    IDX_CHAR_POLLIING_CFG,

    IDX_CHAR_WIFI_SSID,
    IDX_CHAR_VAL_WIFI_SSID,
    IDX_CHAR_WIFI_SSID_CFG,

    IDX_CHAR_WIFI_PASSWORD,
    IDX_CHAR_VAL_WIFI_PASSWORD,
    IDX_CHAR_WIFI_PASSWORD_CFG,

    IDX_CHAR_CONNECT_TO_CLOUD,
    IDX_CHAR_VAL_CONNECT_TO_CLOUD,
    IDX_CHAR_CONNECT_TO_CLOUD_CFG,

    IDX_CHAR_OTA_URL,
    IDX_CHAR_VAL_OTA_URL,
    IDX_CHAR_OTA_URL_CFG,

    IDX_CHAR_UPDATE_FIRMWARE,
    IDX_CHAR_VAL_UPDATE_FIRMWARE,
    IDX_CHAR_UPDATE_FIRMWARE_CFG,

    IDX_CHAR_DEVICE_ID,
    IDX_CHAR_VAL_DEVICE_ID,
    IDX_CHAR_DEVICE_ID_CFG,

    HRS_IDX_NB,
};

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

#endif