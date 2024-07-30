#ifndef PROTHOCOL_SERIAL_H
#define PROTHOCOL_SERIAL_H

//=====================================================================================
// Protocol Descriptor debug message
//
// Call
//       KTS: 's','m', 6, 'D', ..... ,  CRC_LO, CRC_HI
//   RD_UNIT: 's','m', 6, 'D', ..... ,  CRC_LO, CRC_HI      ( TOTAL: (IDD_POS_CRC_HI + 1) Byte))
//
//=====================================================================================

#include "main.h"
#include "CL_WBM.h"

//---- STATI RICEZIONE SERIALI --------
#define  RUN_DOWNLOAD              1
#define  BUFFER_RX_EMPY            0
#define  ERROR_HEAD_MESSAGE       -1
#define  ERROR_LEN_MESSAGE        -2
#define  ERROR_CKSUM_16           -3

#define PROTO_START_MSG_0    's'
#define PROTO_START_MSG_1    'm'

//---- TYPE_COMAND ----
#define COMMAND_TEST_UNIT         'T'    // Richiesta di esecuzione di un determinato test
#define COMMAND_DATA_DEBUG        'D'    // comando per la lettura dei dati per il debug, misure velocitÃ?Æ?  motori, etc... 
#define COMMAND_POLLING_BASE      'P'    // comando di polling base di dati 
#define COMMAND_READ_EEPROM       'R'    // comando di lettura N Byte eeprom 
#define COMMAND_WRITE_EEPROM      'W'    // comando di Scrittura N Byte eeprom
#define COMMAND_ASSIGN_ID_KTS     'A'    // durante lo start-up di comunicazione, viene passato il valore ID univoco per KTS

//---- TYPE TEST ----
#define  START_TEST              'S'
#define  END_TEST                'E'
#define  TEST_BYPASS             '0' 
#define  TEST_OUTPUT             '1'
#define  TEST_BUZZER             '2'
#define  TEST_IPEHD              '3'
#define  TEST_LEDS               '4'
#define  TEST_BURN_IN            '5'
#define  TEST_CHANGE_SPEED       '6'
#define  TEST_RESULT             '9'

 
//---------------------------------------------------------------
//  Testa dei Messaggi del protocollo 1.0
//---------------------------------------------------------------
enum index_Head_Message_Ser1{
   IHM1_START_MSG_0 = 0,          // byte_0: start message 's'
   IHM1_START_MSG_1,              // byte_1: start message 'm'
   IHM1_POS_CRC_LO,               // byte_2: posizione cksum_16 (byte LO) che Ã?Æ?Ã?Â¨ anche il penultimo byte
   IHM1_TYPE_COMAND,              // byte_3: Tipo di comando/richiesta
   IHM1_START_DATA                // byte_4: Inizio BYTE Dati, lo sviluppo dei dati Ã?Æ?Ã?Â¨ in funzione del TYPE_COMAND
};

//----------------------------------------------------------
// 'T': Richiesta di esecuzione di un determinato test
//   messaggio di inizio test: 's', 'm', 8, 'T', 'S' ,'t', 'a', CK_16
//   messaggio di   fine test: 's', 'm', 8, 'T', 'E' ,'n', 'd', CK_16
//----------------------------------------------------------
enum index_message_request_test {
   IRQT_TYPE_TEST = IHM1_START_DATA,       // byte_04: 
   IRQT_DATA_1_TEST,                       // byte_05:
   IRQT_DATA_2_TEST,                       // byte_06:
   IRQT_CRC_LO,                            // byte_07:  
   IRQT_CRC_HI                             // byte_08:
};  // Totale: 10  Byte

//----------------------------------------------------------
// 'D': Indici array messaggio Request DATA_DEBUG
//----------------------------------------------------------
enum index_message_request_data_debug {
  IRQD_NONE_0 = IHM1_START_DATA,           // byte_05: 
  IRQD_NONE_1,                             // byte_06:
  IRQD_CRC_LO,                             // byte_07:        
  IRQD_CRC_HI                              // byte_08:          
};    // Totale: 9  Byte

//--  Indici array messaggio Respone DATA_DEBUG --
enum index_message_resp_data_debug {
  // PAGINA 3: Pressione
   IRSD_LEV_LINK_CAP = IHM1_START_DATA ,   // byte_05: livello di comunicazione tra Unita'  e Accesorio CAP/CAF 0/10 
   IRSD_MEASUR_PA_LO,                      // byte_06: byte LO: differential_Pa
   IRSD_MEASUR_PA_HI,                      // byte_07: byte HI: differential_Pa

   // PAGINA 1 : Motors
   IRSD_LEV_LINK_CTRL_FAN,                 // byte_09: livello di comunicazione tra modulo rd2. e CTRL_FAN 0/10
   IRSD_STATUS_MOTORS_R,                   // byte_10: bit.. 7.none | 6.AlarmMR3 | 5.AlarmMR2 | 4.AlarmMR1 | 3.none | 2.PowerMR3 | 1.PowerMR2 | 0.PowerMR1 
   IRSD_STATUS_MOTORS_F,                   // byte_11: bit.. 7.none | 6.AlarmMF3 | 5.AlarmMF2 | 4.AlarmMF1 | 3.none | 2.PowerMF3 | 1.PowerMF2 | 0.PowerMF1 
   IRSD_FEEDBK_VOUT_R_LO,                  // byte_12:  valore di tensione letto in uscita del pilota ventole F: da 0.0V a 11.00V
   IRSD_FEEDBK_VOUT_R_HI,                  // byte_13:
   IRSD_FEEDBK_VOUT_F_LO,                  // byte_14:
   IRSD_FEEDBK_VOUT_F_HI,                  // byte_15:
   IRSD_RPM_MOTOR_R1_LO,                   // byte_16: byte LO: RPM motors R1
   IRSD_RPM_MOTOR_R1_HI,                   // byte_17: byte HI: RPM motors R1
   IRSD_RPM_MOTOR_R2_LO,                   // byte_18: byte LO: RPM motors R2
   IRSD_RPM_MOTOR_R2_HI,                   // byte_19: byte HI: RPM motors R2
   IRSD_RPM_MOTOR_R3_LO,                   // byte_20: byte LO: RPM motors R3
   IRSD_RPM_MOTOR_R3_HI,                   // byte_21: byte HI: RPM motors R3
   IRSD_RPM_MOTOR_F1_LO,                   // byte_22: byte LO: RPM motors F1
   IRSD_RPM_MOTOR_F1_HI,                   // byte_23: byte HI: RPM motors F1
   IRSD_RPM_MOTOR_F2_LO,                   // byte_24: byte LO: RPM motors F2
   IRSD_RPM_MOTOR_F2_HI,                   // byte_25: byte HI: RPM motors F2
   IRSD_RPM_MOTOR_F3_LO,                   // byte_26: byte LO: RPM motors F3
   IRSD_RPM_MOTOR_F3_HI,                   // byte_27: byte HI: RPM motors F3  

   // PAGINA 2 : Accessory CLIMATE (le informazioni sono in eeprom sugli accessori configurati)
   IRSD_LEV_LINK_PREHEATER,                // 0/10 Level
   IRSD_LEV_LINK_HEATER,                   // 0/10 Level
   IRSD_LEV_LINK_COOLER,                   // 0/10 Level
    /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *
	1 byte STATUS:	bit.4: 1=IN_ALLARME, 0=NO ALLARMI
					bit.3: [Comando da UnitÃ?Â (1 ACCENDITI/SPEGNITI)]
					bit.2: ON=1/OFF=0
					bit.1: AIR=1/WATER=0
					bit.0: CONNECT
	* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */
   IRSD_STATUS_PREHEATER,                  // 1 byte: status
   IRSD_STATUS_HEATER,                     // 1 byte: status
   IRSD_STATUS_COOLER,                     // 1 byte: status
   IRSD_MEASURE_TW_HEAT_LO,                // 2 byte:  Temperature water se  l'accessory Ã?Â¨ HWD altrimenti dato inutile
   IRSD_MEASURE_TW_HEAT_HI,
   IRSD_MEASURE_TA_HEAT_LO,                // 2 byte:  Temperature air sia per HWD che EHD
   IRSD_MEASURE_TA_HEAT_HI,
   IRSD_MEASURE_TW_COOL_LO,                // 2 byte:  Temperature water accessoy
   IRSD_MEASURE_TW_COOL_HI,
   IRSD_MEASURE_AIRFLOW_LO,                // 
   IRSD_MEASURE_AIRFLOW_HI,                // 

   IRSD_MEASURE_ABSOR_BYPASS_MIN,          // valore espresso in mA da 0 a 255mA 
   IRSD_MEASURE_ABSOR_BYPASS_MAX_LO,       // valore espresso in mA da 0 a 280mA
   IRSD_MEASURE_ABSOR_BYPASS_MAX_HI,
   IRSD_LEV_LINK_I_SENSOR_AMB,             // Valore di comunicazione del sensore RH/CO2/VOC
   IRSD_LEV_LINK_MODBUS,                   // Valore di comunicazione del Modbus
   IRSD_MEASURE_TW_PREHEAT_LO,             // 2 byte:  Temperature water se  l'accessory Ã¨ PHWD 
   IRSD_MEASURE_TW_PREHEAT_HI,
   IRSD_MEASURE_TA_PREHEAT_LO,             // 2 byte:  Temperature air sia per PHWD che PEHD
   IRSD_MEASURE_TA_PREHEAT_HI,
   IRSD_MEASURE_TA_COOL_LO,                // 2 byte:  Temperature air per CWD
   IRSD_MEASURE_TA_COOL_HI,
   IRSD_STATUS_DSC,

   
   IRSD_CRC_LO,                            // byte_42: CRC : Low 
   IRSD_CRC_HI                             // byte_43: CRC : high    
}; 

//-------------------------------------------------------------
// 'R': Indici array messaggio Request Read/write eeprom
//-------------------------------------------------------------
enum index_message_request_read_eeprom {
  IRQR_ADDR_BYTE_START_EEP = IHM1_START_DATA,        // byte_05:  Indirizzo INIZIALE di memoria delle eeprom  che si vuole scrivere o leggere  
  IRQR_ADDR_NUM_BYTE_EEP,                            // byte_06:  Numero di Byte da leggere  
  IRQR_CRC_LO,                                       // byte_07: 
  IRQR_CRC_HI                                        // byte_08: 
};    

//------------------ response ----------------------------------
enum index_message_response_read_eeprom {
  IRSR_ADDR_BYTE_START_EEP = IHM1_START_DATA,
  IRSR_ADDR_NUM_BYTE_EEP,
  IRSR_START_DATA_EEPROM, // byte_07:
  // ..... N BYTE EEPROM di Lettura
  //IRSR_CRC_LO = (1 + IRSR_START_DATA_EEPROM + IRSR_ADDR_NUM_BYTE_EEP),                                      
  //IRSR_CRC_HI                                      
};  

//-------------------------------------------------------------
// 'W': Indici array messaggio Request Information
//-------------------------------------------------------------
enum index_message_request_write_eeprom {
  IRQW_ADDR_BYTE_START_EEP = IHM1_START_DATA,   // byte_05: Indirizzo di partenza di scrittura eeprom
  IRQW_ADDR_NUM_BYTE_EEP,                       // byte_06:  Numero di Byte da leggere  
  IRQW_START_DATA_EEPROM,                       // byte_07: Inizio area dati da scrivere in eeprom Max 116 byte  
  //.............. N BYTE EEPROM di Scrittura 
  //IRQW_CRC_LO = (1 + IRQW_START_DATA_EEPROM + IRQW_ADDR_NUM_BYTE_EEP),                                       
  //IRQW_CRC_HI                                    
};    

//------------------ response ----------------------------------
enum index_message_response_write_eeprom {
  IRSW_ADDR_BYTE_START_EEP = IHM1_START_DATA,         
  IRSW_ADDR_NUM_BYTE_EEP,                       // byte_06:  Numero di Byte da leggere                        
  IRSW_RESULT_W,                                // byte_07:   '0': scrittura OK,  '1': errore zona eeprom non valida,  '2': errore dati non validi,  '3': dispositivo occupato.       
  IRSW_CNT_UPDATE_EEP_INFO,                     // byte_08:
  IRSW_CNT_UPDATE_EEP_SETTING_PAR, 				      // byte_09:
  IRSW_CNT_UPDATE_EEP_SETP_TEMP,                // byte_10:
  IRSW_CNT_UPDATE_EEP_WEEKLY,                   // byte_11:
  IRSW_CRC_LO,                                  // byte_12:    
  IRSW_CRC_HI                                   // byte_13:   
};  

//-------------------------------------------------------------
// '#': Indici array messaggio Debug
//-------------------------------------------------------------
enum index_message_request_debug
{
  IRQDBG_BYTE_COUNT = IHM1_START_DATA,
  IRQDBG_START_DATA                               
};

//-------------------------------------------------------------
// 'P': Indici array messaggio Req. Polling BASE 
//-------------------------------------------------------------
enum index_message_request_polling_base {
  IRQP_DATA_SEC = IHM1_START_DATA,           // byte_04:
  IRQP_DATA_MIN,                             // byte_05:
  IRQP_DATA_HOURS,                           // byte_06:
  IRQP_DATA_WEEKDAY,                         // byte_07:
  IRQP_DATA_DAY,                             // byte_08:
  IRQP_DATA_MOUNTH,                          // byte_09:
  IRQP_DATA_YEAR,                            // byte_10:
  IRQP_DATA_CENTURY,                         // byte_11:
  IRQP_CRC_LO,                               // byte_12:
  IRQP_CRC_HI                                // byte_13:
};    // Totale: 14  Byte

//------------------ response ----------------------------------
enum index_message_resp_polling_base {
  IRSP_MEASURE_TEMP_1_LO  = IHM1_START_DATA,
  IRSP_MEASURE_TEMP_1_HI,  
  IRSP_MEASURE_TEMP_2_LO,  
  IRSP_MEASURE_TEMP_2_HI,
  IRSP_MEASURE_TEMP_3_LO,  
  IRSP_MEASURE_TEMP_3_HI,
  IRSP_MEASURE_TEMP_4_LO,  
  IRSP_MEASURE_TEMP_4_HI,
  IRSP_MEASURE_IN1,					// Misura di input 1 (0-100%) < 20% -> off
  IRSP_MEASURE_IN2,					// Misura di input 2 (0-100%) < 20% -> off
  IRSP_INFO_PROBE_AD,				//    7		  6		  5		  4		  3		  2		  1		  0
									//  P2CO2	P1CO2	PICO2	P2RH	P1RH	PIRH	P1VOC	PIVOC
									//	0 = Ambiente, 1 = Duct
  IRSP_MEASURE_RH_SENS,           // 1 byte:  da 0% a 99%
  IRSP_MEASURE_CO2_SENS_LO,       // 2 byte:  da 0 a 2000 PPM 
  IRSP_MEASURE_CO2_SENS_HI,       // 
  IRSP_MEASURE_VOC_SENS_LO,       // 2 byte:  da 0 a 10000 ug/mq
  IRSP_MEASURE_VOC_SENS_HI,
  IRSP_STATUS_UNIT_LO,            // 2 byte:  bit[15,14,13,12]: * none *
  IRSP_STATUS_UNIT_HI,            //          bit[11]: 1= VOC_MAX, bit[10]: 1= CO2_MAX,  bit[9]: 1= RH_MAX 
                                  //          bit[8]: 1= CMD_FAN_INPUT 
                                  //          bit[7,6]: b10=BypassClose, b01=BypassRun, b00=BypassOpen              
                                  //          bit[5,...,0]:  b100000= none b010000= RUN BOOST, b001000= RUN IMBALANCE, b000100=RUN POST_VENT, b000010=RUN DeFROST, b00001=RUN NORMAL, b000000=STANDBY     
                                  
  IRSP_STATUS_WEEKLY,             // 1 byte:  bit[7,6]:1,0=STEP_SPEED_3/ 0,1=STEP_SPEED_2/ 0,0=STEP_SPEED_1 | bit[5,4]: 0,1=IMBAL_1_ON/ 0,0=IMBAL_OFF | bit[3,2]:0,1=RIF_TEMP2/ 0,0=RIF_TEMP1 | bit[1]: WEEKLY_RUN, bit[0]:ENABLE       
 
  IRSP_EVENT_BYTE_00,             //13 byte: vedi descrizione allarmi nel file: data.h
  IRSP_EVENT_BYTE_01,             
  IRSP_EVENT_BYTE_02,            
  IRSP_EVENT_BYTE_03,             
  IRSP_EVENT_BYTE_04,          
  IRSP_EVENT_BYTE_05,                                                
  IRSP_EVENT_BYTE_06,           
  IRSP_EVENT_BYTE_07,             
  IRSP_EVENT_BYTE_08,            
  IRSP_EVENT_BYTE_09,           
  IRSP_EVENT_BYTE_10,                                                     
  IRSP_EVENT_BYTE_11,                
  IRSP_EVENT_BYTE_12,
  
  IRSP_NONE_0,                                   // 1 byte: a disposizione 
  IRSP_INCREASE_SPEED_RH_CO2,                    // 1 byte: a disposizione
  // dati utili a capire se vi Ã?Æ?Ã?Â¨ stata fatta una variazione di eeprom da parte di altri. 
  IRSP_CNT_UPDATE_EEP_INFO,
  IRSP_CNT_UPDATE_EEP_SETTING_PAR,
  IRSP_CNT_UPDATE_EEP_SETP_TEMP,
  IRSP_CNT_UPDATE_EEP_WEEKLY,
  IRSP_MEASURE_AWP_SENS_LO,       // 2 byte:  Temperatura x 10
  IRSP_MEASURE_AWP_SENS_HI,
  IRSP_BELIMO1_STATE,
  IRSP_BELIMO2_STATE,
  IRSP_FIRE_STATE,
	
  IRSP_CRC_LO,                                  
  IRSP_CRC_HI                                    
};    // Totale:  Byte

//----------------------------------------------------------
// 'A': COMAND_ASSIGN_ID_KTS
//----------------------------------------------------------
enum index_message_request_assign_id {
    IRAI_NUMBER_ID_LO = IHM1_START_DATA,      // byte_04:
    IRAI_NUMBER_ID_HI,
    IRAI_NONE_0,
    IRAI_NONE_1,
    IRAI_CRC_LO,
    IRAI_CRC_HI 
};

// --------------------------
// EEprom
// --------------------------
typedef enum
{
	EEepromSection_Info,
	EEepromSection_Configuration,
	EEepromSection_SettingPar,
	EEepromSection_SetTemp,
	EEepromSection_DayProg,
	EEepromSection_HWSetting
} EEepromSection;

#define Eeprom_Info_StartAddress			offsetof( S_EEPROM, AddrUnit )
#define Eeprom_Info_EndAddress				(offsetof( S_EEPROM, cntUpdate_info ) + sizeof(gRDEeprom.cntUpdate_info) - 1)
#define Eeprom_Info_Count					((Eeprom_Info_EndAddress - Eeprom_Info_StartAddress) + 1)

#define Eeprom_Configuration_StartAddress	offsetof( S_EEPROM, numMotors )
#define Eeprom_Configuration_EndAddress		(offsetof( S_EEPROM, size2_free ) + sizeof(gRDEeprom.size2_free) - 1)
#define Eeprom_Configuration_Count			((Eeprom_Configuration_EndAddress - Eeprom_Configuration_StartAddress) + 1)

#define Eeprom_SettingPar_StartAddress		offsetof( S_EEPROM, Set_Power_ON )
#define Eeprom_SettingPar_EndAddress		(offsetof( S_EEPROM, cntUpdate_SettingPar ) + sizeof(gRDEeprom.cntUpdate_SettingPar) - 1)
#define Eeprom_SettingPar_Count				((Eeprom_SettingPar_EndAddress - Eeprom_SettingPar_StartAddress) + 1)

#define Eeprom_SetTemp_StartAddress			offsetof( S_EEPROM, Bypass_minTempExt )
#define Eeprom_SetTemp_EndAddress			(offsetof( S_EEPROM, cntUpdate_SetTemp ) + sizeof(gRDEeprom.cntUpdate_SetTemp) - 1)
#define Eeprom_SetTemp_Count				((Eeprom_SetTemp_EndAddress - Eeprom_SetTemp_StartAddress) + 1)

#define Eeprom_DayProg_StartAddress			offsetof( S_EEPROM, sDayProg )
#define Eeprom_DayProg_EndAddress			(offsetof( S_EEPROM, cntUpdate_dayProg ) + sizeof(gRDEeprom.cntUpdate_dayProg) - 1)
#define Eeprom_DayProg_Count				((Eeprom_DayProg_EndAddress - Eeprom_DayProg_StartAddress) + 1)

#define Eeprom_HWSetting_StartAddress		offsetof( S_EEPROM, numMotors )
#define Eeprom_HWSetting_EndAddress			(offsetof( S_EEPROM, size2_free ) + sizeof(gRDEeprom.size2_free) - 1)
#define Eeprom_HWSetting_Count				((Eeprom_HWSetting_EndAddress - Eeprom_HWSetting_StartAddress) + 1)

extern void Serial_begin( void );
extern void Serial_end( void );
extern int Serial_available(void);
extern int Serial_peek(void);
extern int Serial_read(void);
extern void Serial_flush(void);
extern void Serial_write(uint8_t* b, uint16_t len);
extern uint16_t calc_cksum_16(uint8_t * buffer, int length);
extern void Com_SendRequest_PollingBase();
extern int Read_Message();
extern void Com_SendRequest_ReadEeprom( byte startAddress, byte count );
extern bool Com_SendRequest_ReadEeprom1( EEepromSection eepromSection );
extern void Com_SendRequest_DataDebug();
extern void Com_SendRequest_WriteEeprom( byte startAddress, byte count );
extern bool Com_SendRequest_WriteEeprom1( EEepromSection eepromSection );

#endif /*PROTHOCOL_SERIAL_H*/