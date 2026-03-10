/*
 * Uart1.h
 *
 *  Created on: 17 mai 2021
 *      Author: Zakaria Taleb Bendiab
 */

#ifndef INCLUDE_UART1_H_
#define INCLUDE_UART1_H_

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __packed _Uart1_Message_Tag {
	uint8_t* Receive_Buff;
	uint8_t* Transmit_Buff;
	uint16_t Rx_length;
	uint16_t Tx_Length;
	bool	Rx_Flag;
	bool 	Tx_Flag;
} _Uart1_Message;

esp_err_t Uart1_Initialize ( void );

esp_err_t Uart1_Initialize_1 ( void );

void Uart_Write ( char* Data , size_t Length);

size_t Uart1_RxClaim (const uint8_t **out_buffer);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_UART1_H_ */
