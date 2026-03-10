/*
 * gpio_api.c
 *
 *  Created on: 15 april. 2021
 *  Author: Zakaria Taleb Bendiab
 */

#include "esp_err.h"
#include "driver/gpio.h"
#include "gpio_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define COMM_LED_TASK_PERIOD_MS 25
#define COMM_LED_HEARTBEAT_MS 5000
#define COMM_LED_HEARTBEAT_PULSE_MS 200
#define COMM_LED_ACTIVITY_FRESH_MS 15000

static const char *TAG_COMM_LED = "comm_led";

static volatile bool s_comm_led_fault = false;
static volatile TickType_t s_comm_led_pulse_until = 0;
static volatile TickType_t s_comm_led_last_activity_tick = 0;
static volatile TickType_t s_comm_led_next_heartbeat_tick = 0;
static TaskHandle_t s_comm_led_task_handle = NULL;

static void comm_led_task(void *pvParameters) {
    (void)pvParameters;

    while (true) {
        int level = 0;
        TickType_t now = xTaskGetTickCount();
        TickType_t pulse_until = s_comm_led_pulse_until;
        TickType_t last_activity = s_comm_led_last_activity_tick;

        if (s_comm_led_fault) {
            level = 1;
        } else {
            bool activity_fresh = (last_activity != 0) &&
                                 ((now - last_activity) <= pdMS_TO_TICKS(COMM_LED_ACTIVITY_FRESH_MS));

            if (activity_fresh) {
                if (s_comm_led_next_heartbeat_tick == 0 || now >= s_comm_led_next_heartbeat_tick) {
                    s_comm_led_pulse_until = now + pdMS_TO_TICKS(COMM_LED_HEARTBEAT_PULSE_MS);
                    s_comm_led_next_heartbeat_tick = now + pdMS_TO_TICKS(COMM_LED_HEARTBEAT_MS);
                    pulse_until = s_comm_led_pulse_until;
                }
            } else {
                s_comm_led_next_heartbeat_tick = 0;
                s_comm_led_pulse_until = 0;
                pulse_until = 0;
            }

            if (pulse_until != 0 && now < pulse_until) {
                level = 1;
            }
        }

        gpio_set_level(Wifi_Led2, level);
        vTaskDelay(pdMS_TO_TICKS(COMM_LED_TASK_PERIOD_MS));
    }
}

esp_err_t initialize_gpio() {
	gpio_config_t io_conf;

	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL1;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL2;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	gpio_set_level( Wifi_Led1, 0);
	gpio_set_level( Wifi_Led2, 0);
	gpio_set_level( Wifi_Ready, 0);

	if (s_comm_led_task_handle == NULL) {
		(void)xTaskCreate(comm_led_task, "comm_led_task", 2048, NULL, 2, &s_comm_led_task_handle);
	}

	return ESP_OK;
}

void comm_led_mark_activity(void) {
    if (!s_comm_led_fault) {
        TickType_t now = xTaskGetTickCount();
        s_comm_led_last_activity_tick = now;
        if (s_comm_led_next_heartbeat_tick == 0) {
            s_comm_led_next_heartbeat_tick = now + pdMS_TO_TICKS(COMM_LED_HEARTBEAT_MS);
        }
    }
}

void comm_led_mark_activity_source(const char *source) {
    ESP_LOGI(TAG_COMM_LED, "HEARTBEAT_ACTIVITY_SOURCE=%s", source ? source : "UNKNOWN");
    comm_led_mark_activity();
}

void comm_led_set_fault(bool active) {
    s_comm_led_fault = active;
    if (active) {
        s_comm_led_pulse_until = 0;
        s_comm_led_next_heartbeat_tick = 0;
    } else if (s_comm_led_last_activity_tick != 0 && s_comm_led_next_heartbeat_tick == 0) {
        TickType_t now = xTaskGetTickCount();
        s_comm_led_next_heartbeat_tick = now + pdMS_TO_TICKS(COMM_LED_HEARTBEAT_MS);
    }
}

bool comm_led_is_fault(void) {
    return s_comm_led_fault;
}
