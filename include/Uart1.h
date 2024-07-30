/*
 * Uart1.h
 *
 *  Created on: 17 mai 2021
 *      Author: Zakaria Taleb Bendiab
 */

#ifndef INCLUDE_UART1_H_
#define INCLUDE_UART1_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_TASK_STACK_SIZE (5000)

typedef struct __packed _Uart1_Message_Tag {
	uint8_t* Receive_Buff;
	uint8_t* Transmit_Buff;
	uint16_t Rx_length;
	uint16_t Tx_Length;
	bool	Rx_Flag;
	bool 	Tx_Flag;
} _Uart1_Message;

esp_err_t Uart1_Initialize ( void );

void Uart_Write ( char* Data , size_t Length);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_UART1_H_ */
