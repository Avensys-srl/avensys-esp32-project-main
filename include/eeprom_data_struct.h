#ifndef EEPROM_DATA_STRUCT_H
#define EEPROM_DATA_STRUCT_H

#include "definitions.h"

#ifndef _PACK
#define _PACK __attribute__((aligned(1), packed))
#endif

typedef struct SDAYPROG_TAG {
    byte numRange;
    byte timeON[4];
    byte timeOFF[4];
    char ConfigSpeed;
    char ConfigImbal;
    char ConfigTemp;
} _PACK SDAYPROG;

typedef struct EEPROM_DATA_TAG {
    /* Information */
    byte AddrUnit;
    byte Type_func;
    byte HW_Vers[4];
    byte SW_Vers[5];
    byte SerialString[18];
    byte SW_Vers_ModBus[2];
    byte SignTEST[2];
    byte CodeErrTest;

    byte hour_runnig[4];
    byte time_lastCloggedFilters[4];

    byte AccessoyHW[4];

    unsigned short Enab_Fuction;
    unsigned short msk_Enab_Fuction;

    byte Dsc_Sdcard_Update_Delay;
    byte Pir_Update_Delay;

    /* X2 layout fields */
    byte Time_Fire_Test_Counter[4];
    char Imbalance_Speed2;
    char Imbalance_Speed3;

    byte cntUpdate_info;

    /* Factory HW config */
    byte numMotors;
    byte numPulseMotors;
    byte typeMotors;
    byte chWireless;
    byte depotMotors;
    byte numNTC;
    byte Posiz_NTC;
    byte RotazioneBypass;
    char Imbalance_IAQSpeed;
    byte size2_free[8];

    /* Setting/Config parameters */
    byte Set_Power_ON;
    byte Config_Bypass;
    byte Set_Input[2];
    byte Set_Output[2];
    byte sel_idxStepMotors;
    unsigned short Set_StepMotorsCFS_CAF[4];
    unsigned short Set_StepMotors_CAP[4];
    char Set_Imbalance[2];
    byte Set_TimeBoost;
    unsigned short SetPoint_CO2;
    byte SetPoint_RH;
    unsigned short SetPoint_VOC;
    unsigned short gg_manut_Filter;
    byte service_password[5];
    byte user_password[5];
    unsigned short calibra_cap;
    byte manual_reset;
    byte DPP_Calibrationvalue;
    byte Set_MBF_fresh;
    byte Set_MBF_return;
    byte SetPoint_Airflow_CO2;
    byte Time_Fire_Test;
    byte Fire_Config;

    byte cntUpdate_SettingPar;

    /* Sensor calibration */
    byte Calibr[6];

    /* Temperature thresholds */
    short Bypass_minTempExt;
    short SetPointTemp[2];
    char idxSetPointT;
    signed char hister_AWP_Temp_Hot[2];
    signed char hister_AWP_Temp_Cold[2];
    signed char hister_Temp_Hot[2];
    signed char hister_Temp_Cold[2];
    byte Ref_T_setting;
    byte DeltaTemp_Supply;
    byte Set_EHD_mod;
    byte Set_BPD_mod;

    byte cntUpdate_SetTemp;

    /* Weekly program */
    SDAYPROG sDayProg[7];

    byte cntUpdate_dayProg;

    byte version_eeprom;
    byte check_eeprom[2];
} _PACK S_EEPROM; /* total: 242 bytes */

#endif
