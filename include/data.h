#ifndef DATA_H
#define DATA_H

#include "definitions.h"

#ifndef _PACK
#define _PACK __attribute__((aligned(1), packed))
#endif

//const int EVENT_COUNT = 13;

//--------------- Maschere: status_unit -------------------
#define MSK_CLEAR_OPERATING          0xFFC0   // Maschera bit[5,...,0], b000000 = equivale a  UNITA' IN STANDBY
#define POS_BIT_UNIT_RUN                  0   // bit[0]: se =1 l'unita' sta facendo girare i motori.
#define POS_BIT_DEFROST_OPERATING         1   // bit[1]: FUNZIONAMENTO IN DEFROST
#define POS_BIT_POST_VENT_OPERATING       2   // bit[2]: FUNZIONAMENTO POST VENTILAZIONE
#define POS_BIT_IMBALANCE_OPERATING       3   // bit[3]: FUNZIONAMENTO CON  I 2 RAMI SBILANCIATI
#define POS_BIT_BOOST_OPERATING           4   // bit[4]: FUNZIONAMENTO IN BOOST ATTIVO
#define POS_BIT_BOOST_KHK                 5   // bit[5]: FUNZIONAMENTO IN BOOST KHK
#define MSK_CLEAR_STS_BYPASS         0xFF3F   // Maschera bit[7,6]
#define POS_BIT_BYPASS_RUN                6   // bit[6]: STATO BYPASS IN APERTURA/CHIUSURA
#define POS_BIT_BYPASS_CLOSE              7   // bit[7]: STATO BYPASS CHIUSO
#define POS_BIT_CMD_FAN_INPUT             8   // bit[8]: Sta Comandando la velocitÃ?Æ?Ã?Â  dei motori, in funz. della lettura degli inputs
#define POS_BIT_MAX_RH                    9
#define POS_BIT_MAX_CO2                  10
#define POS_BIT_MAX_VOC                  11
#define POS_BIT_UNIT_UPDATE               14  //  bit[14]: Unit update fro WBM availlable
#define POS_BIT_BOOST_INPUT2             15  //  bit[15]: BOOST activated by Input2
// dal bit 12 al bit 15  * LIBERI *

//------------- Indici Accessori con uscita digitale (bus I2C) -------------
enum _INDEX_DIGITAL_ACCESSOTY {
  ACC_I2C_HEATER,
  ACC_I2C_COOLER,
  ACC_I2C_PREHEATER,
  ACC_I2C_CO2_1,
  ACC_I2C_CO2_2,
  ACC_I2C_RH_1,
  ACC_I2C_RH_2,
  ACC_I2C_VOC, 
  ACC_I2C_AWP,
  ACC_I2C_PCAP,    
  ACC_I2C_PCAF,    
  ACC_I2C_DPP,  
  ACC_I2C_DXD,                 // dalla 2.16 Accessorio Clima CALDO/FREDDO con compressore 
  ACC_I2C_EXT1,				   // dalla 2.24 ZH v1
  ACC_I2C_EXT2,
  ACC_I2C_EXT3,
  ACC_I2C_EXT4,  
  ACC_I2C_FLW1,				   // dalla 2.25 ZH v2
  ACC_I2C_FLW2,
  ACC_I2C_EBPD,
  ACC_I2C_SSR,
  ACC_I2C_EBP2,
  ACC_I2C_DSC,
  TOT_ACCESSORY_I2C            // totale 23 accessori. 
};

//---------------------Maschere bit: status_weekly ------------------------------
#define MSK_BIT_WEEKLY_ENAB 0x01   // bit[0]
#define MSK_BIT_WEEKLY_RUN  0x02   // bit[1]
#define POS_BIT_RIF_TEMP       2   // bit[3,2]:1,1=---/ 1,0=---    / 0,1=RIF_TEMP2 / 0,0=RIF_TEMP1
#define POS_BIT_SET_IMBAL      4   // bit[5,4]:1,1=---/ 1,0=---    / 0,1=IMBAL_1_ON/ 0,0=IMBAL_OFF
#define POS_STEP_SPEED         6   // bit[7,6]:1,1=---/ 1,0=SPEED_3/ 0,1=SPEED_2   / 0,0=SPEED_1

//--------------------- index: Probes Ambient. -----------------------------
enum _INDEX_PROBES_TEMPERATURE {
  I_PROBE_FRESH,
  I_PROBE_RETURN,
  I_PROBE_SUPPLY,
  I_PROBE_EXHAUST
};


//----------------- Type Probes Quality Ambient.(status_SensQAir) ---------------------
#define  PROBES_RH    0
#define  PROBES_CO2   1
#define  PROBES_VOC   2

//------------------------------------------------------------------------
// Definition of the timer
//------------------------------------------------------------------------
typedef struct S_CLOCK_TAG {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} _PACK S_CLOCK_WEEK;

//------------------------------------------------------------------------
// Definizione della struttura Dati degli Accessori
//------------------------------------------------------------------------
typedef struct S_DIG_ACCESSORY_TAG{
  byte  comand;              // Comando di Accensione o Spegnimento
  byte  sts;                 // Stato in cui si trova l'accessorio: ..., bit[2]:1=ACCESO/0=SPENTO, bit[1]:1=OPEARATIVO, bit[0]:1=Collegato
  int   measure1;            // misura principale
  int   measure2;            // misura secondaria
  byte  size_pwr;            // taglia potenza es: 10 = 1,0KW, 15 = 1,5KW  etc..
  byte  level_link;          // bontÃ?Æ?Ã?Â  della comunicazione (valore espresso in 10/10)
  byte  cnt_link_ok;         // Contatore Link buoni
  byte  cnt_link_ko;         // Contatore Link falliti
  byte  cnt_persist_link_ko; // contatore di persistenza di collegamenti Falliti
} S_DIG_ACCESSORY;  // tot: 12 byte

//------------------ Definizione stati accesorio ------------------------
#define STS_ACC_CONNECT     0x01  // bit.0: l'accesorio e' stato rilevato dall'unita'Ã?Â  almeno 1 volta
#define STS_ACC_OPERATIVE   0x02  // bit.1: l'accessorio e' operativo (no link KO)
#define STS_ACC_ON          0x04  // bit.2: l'accessorio e' acceso
#define STS_ACC_ACQUA       0x08  // bit.3: l'accessorio e' tipo acqua


#define CMD_OFF             0x01   // bit.0: byte command
#define CMD_ON              0x02   // bit.1: byte command

#endif
