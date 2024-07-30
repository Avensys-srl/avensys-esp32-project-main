/*
 * gpio_api.h
 *
 *  Created on: 15 april. 2021
 *  Author: Zakaria Taleb Bendiab
 */

#ifndef INCLUDE_GPIO_API_H_
#define INCLUDE_GPIO_API_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//Inputs


//Outputs

#define Wifi_Led1    21  //2
#define GPIO_OUTPUT_PIN_SEL  (1ULL<< Wifi_Led1)

#define Wifi_Ready    12
#define GPIO_OUTPUT_PIN_SEL1  (1ULL<< Wifi_Ready)

/**
 * @brief initialize gpio
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL
 */
esp_err_t initialize_gpio();

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_GPIO_API_H_ */
