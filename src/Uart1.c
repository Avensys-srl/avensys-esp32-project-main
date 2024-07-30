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
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "Uart1.h"

#include "esp32/rom/uart.h"
#include "main.h"

#define UART1_NUM UART_NUM_1
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)

QueueHandle_t Uart1_Queue;
TaskHandle_t Uart1_Task_xHandle = NULL;

static const char *TAG2 = "Uart1_Events : ";
size_t Byte_Available_On_Uart1 = 0;
uint8_t Buffer_Temp[128];

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    //size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);

	ESP_LOGI(TAG2, "Uart1 Task Started");

    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(Uart1_Queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
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
				Byte_Available_On_Uart1 = event.size;
				memset ( Buffer_Temp, 0, sizeof ( Buffer_Temp));
				memcpy ( Buffer_Temp, dtmp, event.size);
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

esp_err_t Uart1_Initialize ( void )
{

	/* Configure parameters of an UART driver,
	* communication pins and install the driver */
	uart_config_t uart_config = {
		.baud_rate = 921600, // 921600
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
	};

	ESP_ERROR_CHECK(uart_param_config(UART1_NUM, &uart_config));

	//Set UART log level
	esp_log_level_set(TAG2, ESP_LOG_INFO);

	//Set UART pins (using UART1 default pins ie no changes.)
	ESP_ERROR_CHECK(uart_set_pin(UART1_NUM, GPIO_NUM_17, GPIO_NUM_16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	//Install UART driver, and get the queue.
	ESP_ERROR_CHECK(uart_driver_install(UART1_NUM, BUF_SIZE , BUF_SIZE, 20, &Uart1_Queue, 0));

	//Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, &Uart1_Task_xHandle);

	// enable RX interrupt
	ESP_ERROR_CHECK(uart_enable_rx_intr(UART1_NUM));

	return ESP_OK;
}

void Uart_Write ( char* Data,  size_t Length)
{
	uart_write_bytes(UART1_NUM, Data, Length);
	//ESP_LOGI(TAG2, "RX Done");
}

