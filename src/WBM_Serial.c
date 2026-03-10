#include "main.h"
#include "string.h"
#include "stdlib.h"
#include <stdio.h>
#include "protocol_Serial.h"
#include "definitions.h"

#define UART1_CONFIG_TX_BYTEQ_LENGTH (128+1)
#define UART1_CONFIG_RX_BYTEQ_LENGTH (128+1)

#define MAX_SERIAL_BUFFER_LENGHT  128

#define RX_BUFFER_SIZE 128

static  byte rx_buf_tail = 0;
byte buff_ser1[128];     // buffer di appoggio

uint8_t Numbytes_Received = 0;

extern char Serial_Number[20];
extern uint8_t Serial_Number_Size;

extern uint8_t error;

static const uint8_t* s_rx_ptr = NULL;

bool Bootloader_Mode = false;
bool Ack_Received = false;

void Serial_begin( void )
{
	
}

void Serial_end( void )
{
	
}

int Serial_available( void )
{
	size_t Temp;
	Temp = Uart1_RxClaim(&s_rx_ptr);
	if (Temp != 0) {
		return Temp;
	}
	return 0;
}

int Serial_peek( void )
{
	return 0;
}

int Serial_read( void )
{
	uint8_t data = 0;

	if (s_rx_ptr != NULL) {
		data = *s_rx_ptr;
		s_rx_ptr++;
	}

    return data;
}

void Serial_flush( void )
{
	
}

void Serial_write(uint8_t* b, uint16_t len)
{
	
}

//---------------------------------------------------------
// Funzione che Calcola il Cksum su 16 bit e ritorna il 
// complemento a 1 della somma.
//---------------------------------------------------------
uint16_t calc_cksum_16(uint8_t * buffer, int length)
{
    uint16_t cksum16_temp = 0;
  
    for(int i=0; i < length; i++)
      cksum16_temp += buffer[i];
  
    return (~cksum16_temp) & 0x0000ffff;
}

//-------------------------------------------------------------
//Send Message to the UNIT
//-------------------------------------------------------------
void Write_Message(uint8_t *buff )
{
    uint16_t Cksum16, len, i;

    // testa del messaggio
    buff[IHM1_START_MSG_0] = PROTO_START_MSG_0;
    buff[IHM1_START_MSG_1] = PROTO_START_MSG_1;
   
    // lunghezza del messaggio
    len = buff[IHM1_POS_CRC_LO] + 2; 
    len &= 0x7F; // max 127 byte. 
    
    // calcola il cksum
    Cksum16 = calc_cksum_16(buff,  buff[IHM1_POS_CRC_LO]);
   
    i = buff[ IHM1_POS_CRC_LO ]; 
    buff[i]     = lowByte(Cksum16);
    buff[i + 1] = highByte(Cksum16);

	//Serial_write(buff,len);
	Uart_Write ( (char*)buff,  len);
        
}

int Read_Message()
{  
   unsigned short Cksum16, Cksum16_calc;
   int  Byte_aval = 0;
   int  i, len;
  
   Byte_aval = Serial_available();
   
   // se abbiamo ricevuto dei carattere, attendiamo la fine del messaggio prima di andare a scaricarlo.
   if(Byte_aval) {      
       if(Byte_aval != rx_buf_tail) {
         // finch� il numero di caratteri aumenta aspetta ad andare a scaricare
         // il messaggio.
         rx_buf_tail = Byte_aval;
         return RUN_DOWNLOAD; // aquisizione del messaggio in corso, aspettiamo di che finisca.
       }            
   }else { 
     if (rx_buf_tail == 0)
       return BUFFER_RX_EMPY; // BUFFER EMPY     
   }  
   
   //--------------------------------------------------------------------
   // Abbiamo ricevuto un messaggio, andiamo a verificare se � corretto
   //---------------------------------------------------------------------   
   len = (rx_buf_tail % RX_BUFFER_SIZE);
   rx_buf_tail = 0;
	 
	 memset ( buff_ser1, 0, sizeof( buff_ser1 ));
   // scarichiamo il messaggio
   for(i=0; i < len; i++) {
     buff_ser1[i] = Serial_read();     
   }  
   
	 Numbytes_Received = len;

	if( strstr ( (char*)buff_ser1, "Bootloader") == NULL )
	{
		if( strstr ( (char*)buff_ser1, "ACK") == NULL )
			{
				// 1. Verifichiamo L'intestazione del messaggio.
				if((buff_ser1[IHM1_START_MSG_0] != PROTO_START_MSG_0) || (buff_ser1[IHM1_START_MSG_1] != PROTO_START_MSG_1))
					return  ERROR_HEAD_MESSAGE;
					
				// 2. Verifichaimo la lunghezza del messaggio.
				if(((buff_ser1[IHM1_POS_CRC_LO] + 2) != len) || ( len <= IHM1_START_DATA))
					return  ERROR_LEN_MESSAGE;
				
				// 3. Verifichiamo il cksum_16 del messaggio
				i = buff_ser1[IHM1_POS_CRC_LO];
				Cksum16 = (unsigned short)(((unsigned short)buff_ser1[i+1] << 8) | buff_ser1[i]) ;
				Cksum16 &= 0x0FFFF;
				Cksum16_calc = calc_cksum_16(buff_ser1, i);

							
				if (Cksum16 != Cksum16_calc)  
					return  ERROR_CKSUM_16;
			}
		else
			{
				Ack_Received = true;
			}

	}
	else
	{
		Bootloader_Mode = true;
		//gpio_set_level( Wifi_Led1, 0);
	}
    return len;
 
 }

void Com_SendRequest_PollingBase( void )
{
	byte	txBuffer[ MAX_SERIAL_BUFFER_LENGHT ];
	byte i;
	char* Pointer;

	txBuffer[ IHM1_POS_CRC_LO ]		= Serial_Number_Size + IHM1_START_DATA + sizeof (error );
	txBuffer[ IHM1_TYPE_COMAND ]	= COMMAND_POLLING_BASE;  
	Pointer = &Serial_Number[0];
	txBuffer[ IHM1_START_DATA ] = error;
	for ( i= IHM1_START_DATA + 1; i < (Serial_Number_Size + IHM1_START_DATA + 1); i++ )
	{
		txBuffer[i] = *Pointer;
		Pointer++;
	}

	Write_Message( txBuffer );

}

void Com_SendRequest_ReadEeprom( byte startAddress, byte count )
{
	byte	txBuffer[ MAX_SERIAL_BUFFER_LENGHT ];
	
	txBuffer[ IHM1_POS_CRC_LO ]   = IRQR_CRC_LO;
	txBuffer[ IHM1_TYPE_COMAND ]  = COMMAND_READ_EEPROM;  

	txBuffer[ IRQR_ADDR_BYTE_START_EEP ]	= startAddress;  
	txBuffer[ IRQR_ADDR_NUM_BYTE_EEP ]		= count;  
	
	// Invia la richiesta
	Write_Message( txBuffer );

}

bool Com_SendRequest_ReadEeprom1( EEepromSection eepromSection )
{
	switch (eepromSection)
	{
		case EEepromSection_Info:
			Com_SendRequest_ReadEeprom( Eeprom_Info_StartAddress, Eeprom_Info_Count );
			break;

		case EEepromSection_Configuration:
			Com_SendRequest_ReadEeprom( Eeprom_Configuration_StartAddress, Eeprom_Configuration_Count );
			break;

		case EEepromSection_SettingPar:
			Com_SendRequest_ReadEeprom( Eeprom_SettingPar_StartAddress, Eeprom_SettingPar_Count );
			break;

		case EEepromSection_SetTemp:
			Com_SendRequest_ReadEeprom( Eeprom_SetTemp_StartAddress, Eeprom_SetTemp_Count );
			break;

		case EEepromSection_DayProg:
			Com_SendRequest_ReadEeprom( Eeprom_DayProg_StartAddress, Eeprom_DayProg_Count );
			break;

		case EEepromSection_HWSetting:
			Com_SendRequest_ReadEeprom( Eeprom_HWSetting_StartAddress, Eeprom_HWSetting_Count );
			break;
			
		default:
			return false;
	}
	
	return true;
}

void Com_SendRequest_DataDebug()
{
	byte	txBuffer[ MAX_SERIAL_BUFFER_LENGHT ];

	txBuffer[ IHM1_POS_CRC_LO ]   = IRQD_CRC_LO;
	txBuffer[ IHM1_TYPE_COMAND ]  = COMMAND_DATA_DEBUG;  

	Write_Message( txBuffer );
	
}

void Com_SendRequest_WriteEeprom( byte startAddress, byte count )
{
	byte	txBuffer[ MAX_SERIAL_BUFFER_LENGHT ];

	if (((int) startAddress + (int) count) <= sizeof(S_EEPROM))
	{
		txBuffer[ IHM1_POS_CRC_LO ]   = IRQR_CRC_LO + count;
		txBuffer[ IHM1_TYPE_COMAND ]  = COMMAND_WRITE_EEPROM;  

		txBuffer[ IRQW_ADDR_BYTE_START_EEP ]	= startAddress;
		txBuffer[ IRQW_ADDR_NUM_BYTE_EEP ]		= count;
	
		memcpy( txBuffer + IRQW_START_DATA_EEPROM, ((byte*) (&gRDEeprom)) + startAddress, count );
	
		// Invia la richiesta
		Write_Message( txBuffer);
	}
}

bool Com_SendRequest_WriteEeprom1( EEepromSection eepromSection )
{
	switch (eepromSection)
	{
		case EEepromSection_Info:
			Com_SendRequest_WriteEeprom( Eeprom_Info_StartAddress, Eeprom_Info_Count );
			break;
			
		case EEepromSection_SettingPar:
			Com_SendRequest_WriteEeprom( Eeprom_SettingPar_StartAddress, Eeprom_SettingPar_Count );
			break;

		case EEepromSection_SetTemp:
			Com_SendRequest_WriteEeprom( Eeprom_SetTemp_StartAddress, Eeprom_SetTemp_Count );
			break;

		case EEepromSection_DayProg:
			Com_SendRequest_WriteEeprom( Eeprom_DayProg_StartAddress, Eeprom_DayProg_Count );
			break;
			
		case EEepromSection_HWSetting:
			Com_SendRequest_WriteEeprom( Eeprom_HWSetting_StartAddress, Eeprom_HWSetting_Count );
			break;

		default:
			return false;
	}
	
	return true;
}
