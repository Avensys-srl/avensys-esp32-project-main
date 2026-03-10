/*
 * Uart1.c
 *
 *  Created on: 17 mai 2021
 *      Author: Zakaria Taleb Bendiab
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "Uart1.h"

#include "main.h"

#define UART1_NUM UART_NUM_1
#define UART1_TX_PIN GPIO_NUM_17
#define UART1_RX_PIN GPIO_NUM_16
#define UART1_BUF_SIZE (1024)
#define UART1_RX_CACHE_SIZE (128)

QueueHandle_t Uart1_Queue;
TaskHandle_t Uart1_Task_xHandle = NULL;

static const char *TAG2 = "Uart1_Events : ";
static size_t s_rx_available = 0;
static uint8_t s_rx_cache[UART1_RX_CACHE_SIZE];

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(UART1_BUF_SIZE);

	ESP_LOGI(TAG2, "Uart1 Task Started");

    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(Uart1_Queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            memset(dtmp, 0, UART1_BUF_SIZE);
            ESP_LOGI(TAG2, "uart[%d] event:", UART1_NUM);
            switch (event.type) {
            //Event of UART receving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
                //ESP_LOGI(TAG2, "[UART DATA]: %d", event.size);
                uart_read_bytes(UART1_NUM, dtmp, event.size, portMAX_DELAY);
                //ESP_LOGI(TAG2, "[DATA EVT]:");
				{
					size_t copy_len = event.size;
					if (copy_len > sizeof(s_rx_cache)) {
						copy_len = sizeof(s_rx_cache);
						ESP_LOGW(TAG2, "RX data truncated (%u -> %u)", (unsigned)event.size, (unsigned)copy_len);
					}
					s_rx_available = copy_len;
					memset(s_rx_cache, 0, sizeof(s_rx_cache));
					memcpy(s_rx_cache, dtmp, copy_len);
				}
                //uart_write_bytes(UART1_NUM, (const char*) dtmp, event.size);
                break;
            //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG2, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(UART1_NUM);
                xQueueReset(Uart1_Queue);
                break;
            //Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG2, "ring buffer full");
                // If buffer full happened, you should consider increasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(UART1_NUM);
                xQueueReset(Uart1_Queue);
                break;
            //Event of UART RX break detected
            case UART_BREAK:
                ESP_LOGI(TAG2, "uart rx break");
                break;
            //Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(TAG2, "uart parity error");
                break;
            //Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(TAG2, "uart frame error");
                break;
            //UART_PATTERN_DET
            case UART_PATTERN_DET:
                
                break;
            //Others
            default:
                ESP_LOGI(TAG2, "uart event type: %d", event.type);
                break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

static esp_err_t uart1_apply_config(uint32_t baud_rate, bool full_init)
{
	uart_config_t uart_config = {
		.baud_rate = baud_rate,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
	};

	ESP_ERROR_CHECK(uart_param_config(UART1_NUM, &uart_config));
	esp_log_level_set(TAG2, ESP_LOG_INFO);

	if (full_init) {
		ESP_ERROR_CHECK(uart_set_pin(UART1_NUM, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
		ESP_ERROR_CHECK(uart_driver_install(UART1_NUM, UART1_BUF_SIZE, UART1_BUF_SIZE, 20, &Uart1_Queue, 0));
		xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, &Uart1_Task_xHandle);
		ESP_ERROR_CHECK(uart_enable_rx_intr(UART1_NUM));
	}

	return ESP_OK;
}

esp_err_t Uart1_Initialize ( void )
{
	return uart1_apply_config(921600, true);
}

esp_err_t Uart1_Initialize_1 ( void )
{
	return uart1_apply_config(921600, false);
}

void Uart_Write ( char* Data,  size_t Length)
{
    //Set UART pins (using UART1 default pins ie no changes.)
	ESP_ERROR_CHECK(uart_set_pin(UART1_NUM, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    vTaskDelay(50);
	uart_write_bytes(UART1_NUM, Data, Length);
    vTaskDelay(50);
    uart_wait_tx_done(UART1_NUM, pdMS_TO_TICKS(150));

	//ESP_LOGI(TAG2, "RX Done");
}

size_t Uart1_RxClaim (const uint8_t **out_buffer)
{
	if (s_rx_available == 0) {
		return 0;
	}

	if (out_buffer != NULL) {
		*out_buffer = s_rx_cache;
	}

	size_t available = s_rx_available;
	s_rx_available = 0;
	return available;
}
