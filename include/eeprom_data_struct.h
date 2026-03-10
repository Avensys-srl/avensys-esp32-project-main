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
	/*000*/ byte  AddrUnit;               //  1 Byte:  da 1 a 32, lo 0 Ã¨ utilizzato per i comandi di broadcast.
	/*001*/ byte  Type_func;              //  1 Byte:  configurazione funzionalita' UnitÃ :  0= BASIC, 1= EXTRA
	/*002*/ byte  HW_Vers[4];             //  4 Byte:  Ascii: versione hardware MB     Es: " 4.0"
	/*006*/ byte  SW_Vers[5];             //  5 Byte:  Ascii: versione software MB     Es: "2.01" oppure "2.10"
	/*011*/ byte  SerialString[18];       // 18 Byte:  Ascii:  "OrVe SIZE CFG DATA PRG",
	//              es:  "1565 0023 115 1351 002"
	//          SIZE:  0023, 0043, 0053,... 0423
	//          CFG:   [8]: '0'= OSC, '1'= SSC, '2'= EOS, '3'= FOS, .. | [9]: '0'=none, '1'='A', '2'='B',  ... | [10]: '0'= none, '1', '2', ... , '9'
	//          DATA:  [11,12]: '13' = Anno | [13,14]: '51' = Settimana
	//          PROG:  Progress. Number
	byte  SW_Vers_ModBus[2];      //  2 Byte: Number: Versione Modbus.   Es:  1.01
	byte  SignTEST[2];            //  2 Byte: Ascii codice operato che ha eseguito il test
	byte  CodeErrTest;            //  1 Byte:   se = 0  esito positivo, altrimenti viene inserito un codic di errore.
	
	/*034*/ byte  hour_runnig[4];         //  4 Byte:  ogni 8 ore memorizza in eeprom l'avanzamento del timer.
	/*038*/ byte  time_lastCloggedFilters[4]; //  4 Byte:  tempo in ore dell'ultima pulizia filtri.
	
	/*042*/ byte  AccessoyHW[4];          //  4 Byte:  parte 1 list accessory HW   (MSB)    DXD,   BPD,   AWP,   CWD,   EHD,   HWD,  PHWD,  PEHD  (LSB)
	//           parte 2 list accessory HW   (MSB)    DPS,  PCAF,  PCAP,   INP,   OUT, DDPV2,   RFM,  MBUS  (LSB)
	//           parte 3 list accessory HW   (MSB)  P2CO2, P1CO2,  EBPD,  P2RH,  P1RH,   SSR, P1VOC,  EBP2  (LSB)
	//           parte 4 list accessory HW   (MSB)  -----, -----, -----, -----,  EXT4,  EXT3,  EXT2,  EXT1  (LSB)
	// Leggenda Acronimi:  DPS=Controllo Filtri con Press., P1CO2=Probe 1 CO2,  PCAP= Probe Constant Air Pressure
	
	/*046*/ unsigned short  Enab_Fuction; //  2 Byte:  (LOW) bit[7]:CAF  | bit[6]:CAP  | bit[5]:CSF     | bit[4]:ImbalanON  | bit[3]:STPL       | bit[2]:WeeklyON   | bit[1]:BoostON    | bit[0]:DeFrostON.
	//           (HIGH)bit[15]:--- | bit[14]:MBF | bit[13]:DPP    | bit[12]:PREHEATING| bit[11]:EN_SUMMER | bit[10]:EN_WINTER | bit[9]:CtrlFilter | bit[8]:PASSWORD
	unsigned short  msk_Enab_Fuction; // i bit=0, disabilitano la possibilità di attivare la funzione
	
	/*050*/ byte  Dsc_Sdcard_Update_Delay;	// 1 Byte : 3 to 60
	
	byte  Pir_Update_Delay;		// 1 Byte : 3 to 30
	
	unsigned short  calibra_cap1;     //  2 Byte:
	
	byte  size1_free[4];          //  6 Byte:  liberi per ampliamenti futuri
	
	byte  cntUpdate_info;         //  1 Byte:  contatore degli aggiornamenti dell'eeprom
	
	//--- configurazioni HW di fabbrica non modificabili (17 Byte) ---
	byte  numMotors;             //  1 Byte:  valori: 2, 4, 6.
	byte  numPulseMotors;        //  1 Byte:  numero di pulse per giro. 1, 2, 3, 4,..., 8
	byte  typeMotors;            //  1 Byte:  'F' = Forward , 'B' = Backward
	byte  chWireless;            //  1 Byte:  numero canale wireless da 1 a 16 (era  progMotors = 'S'  non usato)
	byte  depotMotors;           //  1 Byte:  depotenziamento dei motori da 100% a 40%
	byte  numNTC;                //  1 Byte:  numero sonde da 2 a 4.
	byte  Posiz_NTC;             //  1 Byte:  bit 7,6 = Exuast, bit 5,4 = Supply, bit 3,2 = Return, bit 1,0 = Fresh
	byte  RotazioneBypass;       //  1 Byte:  0xFF = bypass chiuso in senso AntiOrario(default), 0= bypass chiuso in senso Orario
	
	//Byte  size2_free[9];         // 9 Byte:  liberi per ampliamenti futuri
	unsigned short Set_StepMotors_CAP1[4];		//  8 byte: 4 Soglie di velocita'Â  dei motori in modalita'Â : CAP  (Range da 30Pa a 250/350/450Pa), ..[3]= per funz. in Stepless
	byte  size2_free[1];						//  9 byte:  liberi per ampliamenti futuri
	
	//---------  Setting/Config. Param.(51 Byte) ---------------
	/*075*/ byte  Set_Power_ON;           //  1 Byte:  1= SET UNIT IDLE (power_on), 0 = SET UNIT STANDBY (power_off)
	byte  Config_Bypass;          //  1 Byte:  0= Automatic,  1= Ext Input 1,  2= Ext Input 2,  3= Manual CLOSE,  4= Manual OPEN
	byte  Set_Input[2];           //  2 Byte:  0= Disable
	//           1= 10V->Unit RUN , 0V->Unit STOP
	//           2=  0V->Unit RUN ,10V->Unit STOP
	//           3= 0-10V Air flow regulation
	//           4= 10V->Bypass Open ,  0V->Bypass Closed
	//           5=  0V->Bypass Open , 10V->Bypass Closed
	//           6= PIR
	byte  Set_Output[2];          //   0= Disable                   (il rele rimane non alimentato)
	//   1= Bypass Status Open        (il rele passa in attrazione se: Bypass Open)
	//   2= Common Fault Status       (il rele passa in attrazione se: Unità in allarme)
	//   3= Unit is Run (ex: SDD)     (il rele passa in attrazione se: Unita è in funz.)
	// 128= Disable                   (il rele rimane in attrazione)
	// 129= Bypass Status Open        (il rele passa in Diseccitaz. se: Bypass Open)
	// 130= Common Fault Status       (il rele passa in Diseccitaz. se: Unità in allarme)
	// 131= Unit is Run (ex: SDD)     (il rele passa in Diseccitaz. se: Unita è in funz.)
	byte  sel_idxStepMotors;      //  1 Byte:   Selettore dell'indice degli array Set_StepMotors*
	//            bit 7: puntatore array 0= CFS_CAF, 1= CAP[4]:
	//            bit 3,2,1,0: index array
	unsigned short  Set_StepMotorsCFS_CAF[4]; //  8 Byte: 4 Soglie di velocita'  dei motori in modalita' : CFS/CAF (Range: da 20.0% a 100.0%),   ..[3]= per funz. in Stepless
	unsigned short  Set_StepMotors_CAP[4];    //  8 Byte: 4 Soglie di velocita'  dei motori in modalita' : CAP  (Range da 30Pa a 250/350/450Pa), ..[3]= per funz. in Stepless
	char  Set_Imbalance[2];       //  2 Byte: 2 Set di Imbalance,  values max: +/-70 %.
	byte  Set_TimeBoost;          //  1 Byte: valore espresso in minuti(min:15, max:240) se abilitato va al massimo della velocita'  per il tempo indicato.
	unsigned short  SetPoint_CO2; //  2 Byte: (da 70 a 150)*10 = da 700 a 1500 PPM
	byte  SetPoint_RH;            //  1 Byte: da 20% a 99%
	unsigned short  SetPoint_VOC;    //  2 Byte: SetPoint_VOC, da 1 a 100 PPM
	unsigned short  gg_manut_Filter; //  2 Byte: gg funz., trascorso il quale occorre fare manutenzione ai Filter
	byte  service_password[5];       //  5 Byte: password, 5 caratteri in Ascii numerici da'0' a '9'.
	byte  user_password[5];          //  5 Byte: password, 5 caratteri in Ascii numerici da'0' a '9'.
	unsigned short  calibra_cap;     //  2 Byte:
	byte manual_reset;               //  1 Byte: 1 = riarmo manuale inserito (incendio) / 0 = riarmo manuale ripristinato
	byte DPP_Calibrationvalue;       //  1 Byte: valore % di calibrazione, se vale 0 nessuna calibrazione
	byte  Set_MBF_fresh;             //  1 Byte: set modbus fan fresh  (Range: da 0.0% a 100.0%) // il modbus controlla in modo indipendente i motori
	byte  Set_MBF_return;            //  1 Byte: set modbus fan return (Range: da 0.0% a 100.0%) // il modbus controlla in modo indipendente i motori
	//Byte  size3_free[1];             //  1 Byte: liberi per ampliamenti futuri
	byte  SetPoint_Airflow_CO2;	  //  1 Byte: da 40% a 100%
	
	
	byte  cntUpdate_SettingPar;   //  1 Byte:
	
	//---------- Calibrazione Sonde ( 6 Byte) -----------------
	byte  Calibr[6];              //  6 Byte: 4 sonde temp + 2 Qual_Air
	
	//--------- Soglie Temperature (20 Byte)----------------
	short Bypass_minTempExt;                    //  2 Byte:  valore espresso in 15.0 Â°C
	short SetPointTemp[2];                      //  4 Byte:  Set-Point Temperature 1&2 value 16.0 Â°C a 32.0 Â°C.
	char  idxSetPointT;                         //  1 Byte:  ..., bit[0]: 1=Rif.Temp2, 0=Rif.Temp1
	signed char  hister_AWP_Temp_Hot[2];        //  2 Byte:  isteresi temperature per attivare HWD/EHD (0= OFF, 1= ON), con AWP presente.
	signed char  hister_AWP_Temp_Cold[2];       //  2 Byte:  isteresi temperature 1&2 per attivare CWD (0= OFF, 1= ON), con AWP presente.
	signed char  hister_Temp_Hot[2];            //  2 Byte:  isteresi temperature per attivare HWD/EHD (0= OFF, 1= ON), loop aperto
	signed char  hister_Temp_Cold[2];           //  2 Byte:  isteresi temperature 1&2 per attivare CWD (0= OFF, 1= ON), loop aperto
	byte  Ref_T_setting;                        //  1 Byte:  ...bit[6]: 1=START test IPEHD 0=STOP test IPEHD, bit[5,4,3,2]: speed_regulator , bit[1]: 1= TempAmb=TR / TempAmb=TS, bit[0]: *
	byte  DeltaTemp_Supply;                     //  1 Byte:  isteresi da applicare alla temperatura in uscita al compressore se il controllo è sulla sonda supply
	byte  Set_EHD_mod;                          //  1 Byte: set EHD modulation level (Range: da 0.0% a 100.0%) // il modbus controlla in modo indipendente un EHD modulante
	byte  Set_BPD_mod;                          //  1 Byte: set BPD modulation level (Range: da 0.0% a 100.0%) // il modbus controlla in modo indipendente un BPD modulante
	
	//Byte  size4_free[0];          //  0 Byte:  liberi per ampliamenti futuri
	
	byte  cntUpdate_SetTemp;      //  1 Byte:
	
	//--------- weekly program (85 Byte) -------------------
	SDAYPROG sDayProg[7];         // 84 Byte:  12 Byte * 7 days
	
	byte  cntUpdate_dayProg;      //  1 Byte:
	
	//------------------------  (2 Byte) -------------------
	byte  version_eeprom;         //  1 Byte: '1'
	byte  check_eeprom[2];        //  1 Byte:  contatore aggiornamenti della eeprom
}_PACK S_EEPROM;   // totale  Byte: 241

#endif