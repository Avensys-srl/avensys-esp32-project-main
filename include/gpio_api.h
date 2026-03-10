/*
 * gpio_api.h
 *
 *  Created on: 15 april. 2021
 *  Author: Zakaria Taleb Bendiab
 */

#ifndef INCLUDE_GPIO_API_H_
#define INCLUDE_GPIO_API_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//Inputs


//Outputs

#define Wifi_Led1    21  //2
#define GPIO_OUTPUT_PIN_SEL  (1ULL<< Wifi_Led1)

#define Wifi_Led2    22  //2
#define GPIO_OUTPUT_PIN_SEL2  (1ULL<< Wifi_Led2)

#define Wifi_Ready    12
#define GPIO_OUTPUT_PIN_SEL1  (1ULL<< Wifi_Ready)

#define TX_PIN    17
#define GPIO_OUTPUT_PIN_SEL3  (1ULL<< TX_PIN)

/**
 * @brief initialize gpio
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL
 */
esp_err_t initialize_gpio();
void comm_led_mark_activity(void);
void comm_led_mark_activity_source(const char *source);
void comm_led_set_fault(bool active);
bool comm_led_is_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_GPIO_API_H_ */
