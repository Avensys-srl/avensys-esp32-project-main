#ifndef EEPROM_DATA_STRUCT_H
#define EEPROM_DATA_STRUCT_H

#include "definitions.h"

#ifndef _PACK
#define _PACK __attribute__((aligned(1), packed))
#endif

// -------------- Struct Weekly Day ----------------
typedef struct SDAYPROG_TAG {
    byte numRange;    //  1 byte: numbers of ranges (from 0= disable to 4)
    byte timeON[4];   //  4 byte: timeON, Values from 0 to 48 (step di 30')
    byte timeOFF[4];  //  4 byte: timeOFF, Values from 0 to 48 (step di 30')

    // per ogni range si posso configurare finoi a : 4 step di velocita', 4 rif. di temp., 4 stati di imbalance
    char ConfigSpeed;  //  1 byte: bit[7,6]:Step Speed range 4 | bit[5,4]:Step Speed range 3 | bit[3,2]:Step Speed range 2 | bit[1,0]:Step Speed range 1.
    char ConfigImbal;  //  1 byte: bit[7,6]:Set Imbal. range 4 | bit[5,4]:Set Imbal. range 3 | bit[3,2]:Set Imbal. range 2 | bit[1,0]:Set Imbal. range 1.
    char ConfigTemp;   //  1 byte: bit[7,6]:Rif. Temp. range 4 | bit[5,4]:Rif. Temp. range 3 | bit[3,2]:Rif. Temp. range 2 | bit[1,0]:Rif. Temp.range 1.
} _PACK SDAYPROG;      // Totale 12 byte for day

//-----------------------------------------------------------------------
// Definition of the data structure in RAM and related addresses
//------------------------------------------------------------------------
typedef struct EEPROM_DATA_TAG {
    //----------  Information (59 Byte) ------------------
    byte AddrUnit;          //  1 byte: from 1 to 63, 0 is used for broadcast commands.
    byte Type_func;         //  1 byte:  functionality configuration Units: 0= BASIC, 1= PLUS, 2= EXTRA
    byte HW_Vers[4];        //  4 byte:  Ascii: hardware version MB Ex: "4.0"
    byte SW_Vers[5];        //  5 byte:  Ascii: MB software version Ex: "2.01" or "2.10"
    byte SerialString[18];  // 18 byte:  Ascii:  "OrVe SIZE CFG DATA PRG",
                            //              es:  "1565 0023 115 1351 002"
                            //          SIZE:  0023, 0043, 0053,... 0423
                            //          CFG:   [8]: '0'= OSC, '1'= SSC, '2'= EOS, '3'= FOS, .. | [9]: '1'='A', '2'='B',  ... | [10]: '1', '2', ... , '9'
                            //          DATA:  [11,12]: '13' = Anno | [13,14]: '51' = Settimana
                            //          PROG:  Progress. Number
    byte swVer_ModBus[2];   //  2 byte:  Versione [0] = Major version, [1] = Minor Version
    byte Sign_Test[2];      //  2 byte: Ascii: Initials of the operator who performed the test (MM, etc..)
    byte CodeErrTest;       //  1 byte:   if = 0 success, otherwise an error code is inserted.

    uint32_t hour_runnig;             //  4 byte:  every 10 hours it stores the progress of the timer in eeprom.
    uint32_t time_lastCloggedFilers;  //  4 byte:  time in hours of the last filter cleaning.

    byte AccessoyHW[4];  //  4 byte:  parte 1 list accessory HW   (MSB)    PIR, [BPD],   AWP,   CWD,   EHD,   HWD,  PHWD,  PEHD  (LSB)
                         //           parte 2 list accessory HW   (MSB)    DPS,  PCAF,  PCAP, [INPUT], [OUT], DDPV2, RFM,  MBUS  (LSB)
                         //           parte 3 list accessory HW   (MSB)  P2CO2, P1CO2,  EBPD,  P2RH,  P1RH,   SSR, P1VOC,  EBP2  (LSB)
                         //           parte 4 list accessory HW   (MSB)  -----, -----, -----, -----,  EXT4,  EXT3,  EXT2,  EXT1  (LSB)
                         // Legend Acronyms: DPS=Filter Control with Pressure, PIVOC=Internal VOC Probe, P1CO2=Probe 1 CO2, PCAP= Probe Constant Air Pressure

    uint16_t Enab_Fuction;  //  2 byte:  (LOW) bit[7]:CAF  | bit[6]:CAP  | bit[5]:CSF     | bit[4]:ImbalanON | bit[3]:STPL | bit[2]:WeeklyON | bit[1]:BoostON    | bit[0]:DeFrostON.
                            //           (HIGH)bit[15]:--- | bit[14]:--- | bit[13]:---    | bit[12]:---      | bit[11]:--- | bit[10]:CLIMA   | bit[9]:CtrlFilter | bit[8]:PASSWORD

    uint16_t msk_Enab_Fuction;         // bit = 0 function not enabled...
    byte     Dsc_Sdcard_Update_Delay;  // 1 byte : 3 to 60
    byte     Pir_Update_Delay;         // 1 byte : 3 to 30
    byte     Time_fire_test_Counter[4];// 4 byte:  default value 0 (Fire auto test timer)
    byte     size1_free[2];            // 6 byte:  free for future expansion

    byte cntUpdate_info;  //  1 byte:

    //--- non-modifiable factory HW configurations (17 bytes) ---
    byte numMotors;        //  1 byte:  Values: 2, 4, 6.
    byte numPulseMotors;   //  1 byte:  number of pulses per revolution. 1, 2, 3, 4,..., 8
    byte typeMotors;       //  1 byte:  ('F' = Forward) , 'B' = Backward
    byte chWireless;       //  1 byte:  wireless channel number 1 to 16 (was progMotors = 'S' not used)
    byte depotMotors;      //  1 byte:  engine depower from 100% to 40%
    byte numNTC;           //  1 byte:  number of probes from 2 to 4.
    byte Posiz_NTC;        //  1 byte:  bit 7,6 = Exuast, bit 5,4 = Supply, bit 3,2 = Return, bit 1,0 = Fresh
    byte RotazioneBypass;  //  1 byte:  0xFF = bypass closed in Anti-Clockwise direction (default), 0= bypass closed in Clockwise direction
    byte size2_free[9];    //  9 byte:  free for future expansion

    //---------  Setting/Config. Param.(51 byte) ---------------
    byte Set_Power_ON;                     //  1 byte:   1= SET UNIT IDLE (power_on), 0 = SET UNIT STANDBY (power_off)
    byte Config_Bypass;                    //  1 byte:   0= Automatic,  1= Ext Ctrl,  2= Manual CLOSE,  3= Manual OPEN
    byte Set_Input[2];                     //  2 byte:   0= Disable
                                           //            1= 10V->Unit RUN , 0V->Unit STOP
                                           //            2=  0V->Unit RUN ,10V->Unit STOP
                                           //            3= 0-10V Air flow regulation
                                           //            4= 10V->Bypass Open ,  0V->Bypass Closed
                                           //            5=  0V->Bypass Open , 10V->Bypass Closed
    byte Set_Output[2];                    //  2 byte:   0= Disable                   (the relay remains unpowered)
                                           //            1= Bypass Status Open        (the relay switches to attraction if: Bypass Open)
                                           //            2= Common Fault Status       (the relay switches to attraction if: Unit in alarm)
                                           //            3= Unit is Run (ex: SDD)     (the relay switches to attraction if: Unit is in operation.)
                                           //          128= Disable                   (the relay remains attracted)
                                           //          129= Bypass Status Open        (the relay switches to de-energized. if: Bypass Open)
                                           //          130= Common Fault Status       (the relay switches to De-energized: Unit in alarm)
                                           //          131= Unit is Run (ex: SDD)     (the relay switches to De-energized: Unit is running.)
    byte        sel_idxStepMotors;         //  1 byte: Array index selector Set_StepMotors*[4]: 0, 1, 2, 3=Steepless
    uint16_t    Set_StepMotorsFSC_CAF[4];  //  8 byte: 4 Motor speed thresholds in mode: FSC/CAF (Range: from 20.0% to 100.0%), ..[3]= for funct. in Stepless
    uint16_t    Set_StepMotors_CAP[4];     //  8 byte: 4 Motor speed thresholds in mode: CAP (Range from 30Pa to 250/350/450Pa), ..[3]= for funct. in Stepless
    signed char Set_Imbalance[2];          //  2 byte: 2 Imbalance Set, values max: +/-70 %.
    byte        Set_TimeBoost;             //  1 byte: value expressed in minutes (min:15, max:240) if enabled it goes to maximum speed for the indicated time.
    uint16_t    SetPoint_CO2;              //  2 byte: 700 to 1500 PPM
    byte        SetPoint_RH;               //  1 byte: 20% to 99%
    uint16_t    SetPoint_VOC;              //  2 byte: SetPoint_VOC, 1 to 100 ppm
    union _PACK {
        uint16_t gg_manut_Filter;  //  2 byte: operating days, after which maintenance must be carried out on the Filters
        struct _PACK {
            byte DPP_Threshold;    // 1 byte: Filter change percentage threshold for DPP
            byte DPP_FilterLevel;  // 1 byte: bit[0]:   Request: 0 = No request, 1 = Read request
                                   //         bit[1]:   Required Reading Type: 0 = calibration, 1 = verification
                                   //         bit[2-6]: Last reading check (0-20) (in 5% steps)
        };
    };
    byte     servicePassword[5];    //  5 byte: password, 5 Ascii numeric characters from '0' to '9'.
    byte     endUserPassword[5];    //  5 byte: password, 5 Ascii numeric characters from '0' to '9'.
    uint16_t calibra_CAP;           //  2 byte: calibrated CAP value
    byte     manual_Reset;          // 1 byte: 1 = manual rearm alarm armed (fire), 0 = manual rearm alarm reset
    byte     DPP_CalibrationValue;  // Calibration percentage value (0 = No calibration performed)
    byte     Set_MBF_fresh;         //  1 byte: set modbus fan fresh (Range: 0.0% to 100.0%) //modbus independently controls the motors
    byte     Set_MBF_return;        //  1 byte: set modbus fan return (Range: 0.0% to 100.0%) //the modbus independently controls the motors
                                    // byte size3_free[1];							// 12 byte: free for future expansion
    byte SetPoint_Airflow_CO2;      // 1 byte: 40% to 100%
    byte Time_Fire_Test;            // 1 byte: default 10 days
    byte Fire_Config;               // 1 byte:
    byte cntUpdate_SettingPar;      // 1 byte:

    //---------- Probe Calibration (6 bytes) -----------------
    byte Calibr[6];  //  6 Byte: 4 probe temp + 2 Qual_Air

    //--------- Temperature thresholds (20 byte)----------------
    short       Bypass_minTempExt;        //  2 byte:  value expressed in 15.0 °C
    short       SetPointTemp[2];          //  4 byte:  Set-Point Temperature 1&2 value 16.0 °C a 32.0 °C. 0 = sole, 1 = luna
    char        idxSetPointT;             //  1 byte:  ...bit[0]: 1=Rif.Temp2, 0=Rif.Temp1
    signed char hister_AWP_Temp_Hot[2];   //  2 byte:  temperature hysteresis to activate HWD/EHD (0= OFF, 1= ON), with AWP present.
    signed char hister_AWP_Temp_Cold[2];  //  2 byte:  temperature hysteresis 1&2 to activate CWD (0= OFF, 1= ON), with AWP present.
    signed char hister_Temp_Hot[2];       //  2 byte:  temperature hysteresis to activate HWD/EHD (0= OFF, 1= ON), with open loop.
    signed char hister_Temp_Cold[2];      //  2 byte:  temperature hysteresis 1&2 to activate CWD (0= OFF, 1= ON), with open loop.
    byte        RefTSetting;              //  1 byte:  ...bit[6]: 1=START test IPEHD 0=STOP test IPEHD, bit[5,4,3,2]: speed_regulator , bit[1]: 1= TempAmb=TR / TempAmb=TS, bit[0]:
    byte        DeltaTemp_Supply;         //  1 byte: hysteresis to be applied to the compressor outlet temperature if the control is on the supply probe
    byte        Set_EHD_mod;              //  1 byte: set EHD modulation level (Range: 0.0% to 100.0%) //modbus independently controls a modulating EHD
    byte        Set_BPD_mod;              //  1 byte: set BPD modulation level (Range: 0.0% to 100.0%) //the modbus independently controls a modulating BPD

    byte cntUpdate_SetTemp;  //  1 byte:

    //--------- weekly program (85 byte) -------------------
    SDAYPROG sDayProg[7];  // 84 byte:  12 byte * 7 days

    byte cntUpdate_dayProg;  //  1 byte:

    //------------------------  (2 byte) -------------------
    byte none;            //  1 byte:  eeprom update data_length_rx
    byte version_eeprom;  //  1 byte: '1'
} _PACK S_EEPROM;         //  Total  Byte: 242

#endif