/*************************************************************************************
 *  file:			KTS.h
 *
 *  Author/Date:
 *
 *  Descrizione:	...
 *
 *************************************************************************************/

#ifndef __CLKTP_H
#define __CLKTP_H

#include <stdbool.h>
#include <stdint.h>

#define word	uint16_t
#define byte	uint8_t

#include "data.h"
#include "eeprom_data_struct.h"
#include "main.h"

#define KTSStatusUnit_IsVOCMax( status ) (status & 0x0800)
#define KTSStatusUnit_IsCO2Max( status ) (status & 0x0400)
#define KTSStatusUnit_IsRHMax( status ) (status & 0x0200)
#define KTSStatusUnit_CmdFanInput( status ) (status & 0x0100)
#define KTSStatusUnit_BypassState( status ) (status & 0x00C0)

#define KTSStatusUnit_UnitState_IsSTANDBY( status ) ((status & 0x3F) == 0x00)
#define KTSStatusUnit_UnitState_IsIDLE( status ) (status & 0x01)
#define KTSStatusUnit_UnitState_IsRUN_DEFROST( status ) (status & 0x02)
#define KTSStatusUnit_UnitState_IsRUN_POST_VENT( status ) (status & 0x04)	
#define KTSStatusUnit_UnitState_IsRUN_IMBALANCE( status ) (status & 0x08)
#define KTSStatusUnit_UnitState_IsRUN_BOOST( status ) (status & 0x10)

#define KTSStatusUnit_BypassState_BypassClose	0x80
#define KTSStatusUnit_BypassState_BypassRun		0x40
#define KTSStatusUnit_BypassState_BypassOpen	0x00

#define EVENT_COUNT 13;

// Valore del campo PowerON in eeprom 
enum CLKTSPowerMode
{
	CLKTSPowerMode_Off	= 0,
	CLKTSPowerMode_On	= 1
};

// Valore del campo Type_Func in eeprom
enum CLKTSType
{
	CLKTSType_BASIC	= 0,
	CLKTSType_EXTRA	= 1,
	CLKTSType_DEMO	= 0xFF
};

// Dati del polling base
// -----------------------------------------------------------------------------
typedef struct __CLKTSData
{
	// ------------------------------------------------------
	// ----[ Dati valorizzati dal messaggio PollingBase ]----
	// ------------------------------------------------------

	// 0=T_Fresh, 1=T_Return, 2=T_Supply, 3=T_Exhaust  (i valori temp., es: 102 = 10.2 �?�??�?°C)
	float Measure_Temp[ 4 ];

	// valore massimo di Co2 tra i vari sensori: espresso in PPM ( da 0 a 2000 PPM)
	int Measure_CO2_max;

	// valore massimo di Umidit�?�?�?  tra i vari sensori: da 0% a 99%
	int Measure_RH_max;

	// valore massimo di VOC tra i vari sensori: da 0 a 100 ppm
	int Measure_VOC_max;

	// temperature corrente awp
	float Measure_Temp_AWP;

	// stati
	word Status_Unit;	// 2 byte:  bit[15,14,13,12]: * none *
						//          bit[11]: 1= VOC_MAX, bit[10]: 1= CO2_MAX, bit[9]: 1= RH_MAX
						//          bit[8]: 1= CMD_FAN_INPUT
						//          bit[7,6]: b10=BypassClose, b01=BypassRun, b00=BypassOpen
						//          bit[5,...,0]: b100000= * none *,  b010000= RUN BOOST, b001000= RUN IMBALANCE, b000100=RUN POST_VENT, b000010=RUN DeFROST, b00001=RUN NORMAL, b000000=STANDBY

	byte Status_Weekly; // 1 byte:  bit[7,6]:1,0=SPEED3/ 0,1=SPEED2/ 0,0=SPEED1 | bit[5,4]: 0,1=IMBAL_1_ON/ 0,0=IMBAL_OFF | bit[3,2]:0,1=RIF_TEMP2/ 0,0=RIF_TEMP1 | bit[1]: WEEKLY_RUN, bit[0]:WEEKLY_ENABLE

	// Allarmi / Eventi
	byte Events[ 13 ];							// 13 byte:  vedi in testa al files 'Definizione byte eventi'

	// Update Counters
	byte CntUpdate_info;         //  1 byte:
	byte CntUpdate_SettingPar;   //  1 byte:
	byte CntUpdate_SetTemp;      //  1 byte:
    byte CntUpdate_dayProg;      //  1 byte:

	byte InputMeasure1;
	byte InputMeasure2;

	byte IncreaseSpeed_RH_CO2;
	
	byte DSC_Status;
	
} _PACK CLKTSData;

// Dati del polling Debug Data
// -----------------------------------------------------------------------------
typedef struct __CLKTSDebugData
{
	byte PreHeater_Status;
	byte Heater_Status;
	byte Cooler_Status;
	byte Dsc_Status;

	int Measures_Pressure_CAPS;
    int Measures_Pressure_CAPR;

	word MeasureAirflow_CAPS;
    word MeasureAirflow_CAPR;
    
    byte CAPS_LinkLevel;
	byte CAPR_LinkLevel;
    byte CAPS_Status;
	byte CAPR_Status;
} CLKTSDebugData;

#define AccessoryClimaStatus_InAlarm( status )		((status & 0x10) >> 4)
#define AccessoryClimaStatus_Command( status )		((status & 0x08) >> 3)
#define AccessoryClimaStatus_IsOn( status )			((status & 0x04) >> 2)
#define AccessoryClimaStatus_IsAir( status )		((status & 0x02) >> 1)
#define AccessoryClimaStatus_IsConnected( status )	(status & 0x01)

// Stato del KTS
typedef enum 
{
	CLKTSRunningMode_Initializing,
	CLKTSRunningMode_PowerOff,
	CLKTSRunningMode_Running,
	CLKTSRunningMode_Scanning,
	CLKTSRunningMode_FireAlarm
} CLKTSRunningMode;

// Variabili globali
// -----------------------------------------------------------------------------
typedef struct __CLKTSGlobal
{
	// Indica se �?¨ presente almeno un allarme (escluso il filtro da cambiare)
	bool InAlarm;

	// Indica se �?¨ l'allarme del filtro
	bool FilterInAlarm;

	// Indica la modalit�?  di esecuzione corrente
	CLKTSRunningMode RunningMode;

	// Indica se �?¨ attivo lo screen saver
	bool ScreenSaverActive;
	short ScreenSaverSuspendCounter;
	unsigned long ScreenSaver_LastTouchedMilliseconds;

	// Ultimo touch
	unsigned long LastTouchedMilliseconds;

	// Variabile per la gestione del party (BOOST)
	//CLDateTime Party_StartDateTime;
	bool Party_IsEnabled;

	// Lingua corrente
	int LanguageId;

	unsigned long PollingBase_TimerMilliseconds;
	unsigned long PollingDebugData_TimerMilliseconds;
	unsigned long UpdateRTC_TimerMilliseconds;

	bool FirstRunningTime;
	
	char DisableTX;
	byte DataTest[ 3 ];
	byte DataTestDebug0;

	char ComLinkType; // BY_SERIAL_0 | BY_WIRELESS

	// Indica il timer dell'ultima ricezione del polling
	unsigned long LastReceivedPollingBase_TimerMilliseconds;

} _PACK CLKTSGlobal;

enum CLRFMCheckChannel
{
	CLRFMCheckChannel_OK,
	CLRFMCheckChannel_Busy
};

// Funzioni del KTS
enum CLKTSProcessResult
{
	CLKTSProcessResult_Ok,
	CLKTSProcessResult_ScreenSaverExecuted
};

// Dati globali del KTS
//extern CLKTSGlobal gKTSGlobal;

// Dati del polling base
//extern CLKTSData gKTSData;

// Dati del polling debug data
//extern CLKTSDebugData gKTSDebugData;


// Dati della Eeprom dell'rd
extern S_EEPROM gRDEeprom;


#define MODELNAME_MAXLENGTH 4
#define MODEL_AIRFLOW_CALCULATE 0xFFFF
#define MODEL_PRESSURE_CALCULATE 0xFFFF

typedef struct _PACK SCLUnitModel
{
	char* Model; // char* Model
	unsigned short MaxAirFlow;
	unsigned short MaxPressure;
} CLUnitModel;


// Stato del KTS durante la connessione
// -----------------------------------------------------------------------------
enum CLKTSConnectState
{
	CLKTSConnectState_Init,
	CLKTSConnectState_LinkConnecting,
	CLKTSConnectState_TrySerialLink,
	CLKTSConnectState_TryRFMLink,
	CLKTSConnectState_LinkConnected,
	CLKTSConnectState_ReadEeprom_Info,
	CLKTSConnectState_ReadEeprom_Configuration,
	CLKTSConnectState_ReadEeprom_SettingPar,
	CLKTSConnectState_ReadEeprom_SetTemp,
	CLKTSConnectState_ReadEeprom_DayProg,
	CLKTSConnectState_ReadEeprom_HWSetting,
	CLKTSConnectState_PollingBase,
	CLKTSConnectState_Connected,
	Bootloader_State,
	Bootloader_State1,
	Bootloader_State_end
};

enum CLKTSConnectOption
{
	CLKTSConnectOption_Default			= 0x00,
	CLKTSConnectOption_SkipSerialLink	= 0x01,
	CLKTSConnectOption_SkipRFMLink		= 0x02
};

#endif
