/************************************************************************************************
 ************************************************************************************************
 * File: QRKAlarms.h
 *
 ************************************************************************************************/
#ifndef _QRKALARMS_H
#define _QRKALARMS_H

//-----------------------------------------------------------------------------------------------------------------------------------------------
//                                                          Definizione byte eventi  ( 13 Byte)
//-----------------------------------------------------------------------------------------------------------------------------------------------
//   [00]:  ACC. EHD/HWD    : 7.HD_LinkErr, 6.No_pressure, 5.ErrorSize , 4.Ta_OvRange , 3.FaultElect  , 2.Tw_OvRange, 1.Tw_Danger , 0.NTC_Fault
//   [01]:  ACC. CWD        : 7.CWD_LinkEr, 6.- - - - -  , 5.ErrorSize , 4.Ta_OvRange , 3.FaultElect  , 2.Tw_OvRange, 1.Tw_Danger , 0.NTC_Fault
//   [02]:  ACC. PEHD/PHWD  : 7.PHD_LinkEr, 6.No_pressure, 5.ErrorSize , 4.TResist_HOT, 3.FaultElect  , 2.Tw_OvRange, 1.Tw_Danger , 0.NTC_Fault
//   [03]:  PROBES_CO2      : 7.- - - - - , 6.S2_CO2LinkEr,5.S2_CO2_KO , 4.- - - - -  , 3.S1_CO2LinkEr, 2.S1_CO2_KO , 1.- - - - - , 0.- - - - -
//   [04]:  PROBES_RH       : 7.- - - - - , 6.S2_RHLinkEr, 5.S2_RH_KO  , 4.- - - - -  , 3.S1_RHLinkEr , 2.S1_RH_KO  , 1.- - - - - , 0.- - - - -
//   [05]:  ACC. DXD/P_VOC  : 7.DXD_LinkEr, 6.DXD_Compress,5.- - - - - , 4.- - - - -  , 3.S1_VOCLink  , 2.S1_VOC_KO , 1.- - - - - , 0.- - - - -
//   [06]:  SENSORS_PRESSURE: 7.DPS_LinkEr, 6.DPS_KO     , 5.- - - - - , 4.PCAF_LinkEr, 3.PCAF_KO     , 2.- - - - - , 1.PCAP_LinkE, 0.PCAP_KO
//   [07]:  PROBES_OTHERS   : 7.AWP_LinkEr, 6.AWP_KO     , 5.- - - - - , 4.- - - - -  , 3.FireFanSup  , 2.FireFanExt, 1.FireFanMax, 0.FireFanOFF
//   [08]:  MOTORS_R        : 7.- - - - - , 6.NoTachoR3  , 5.NoTachoR2 , 4.NoTachoR1  , 3.Err_VSpeed_R, 2.NoPowerR3 , 1.NoPowerR2 , 0.NoPowerR1
//   [09]:  MOTORS_F        : 7.- - - - - , 6.NoTachoF3  , 5.NoTachoF2 , 4.NoTachoF1  , 3.Err_VSpeed_F, 2.NoPowerF3 , 1.NoPowerF2 , 0.NoPowerF1
//   [10]:  MIX_ALM_1       : 7.- - - - - , 6.KTS_FAULT  , 5.ClogFilter, 4.EepFault   , 3.BypassKO    , 2.BattFault , 1.EB_RemCtrl, 0.EB_CtrlFan_KO
//   [11]:  MIX_ALM_2       : 7.EXT2_KO   , 6.EXT2_Link  , 5.EXT1_KO   , 4.EXT1_Link  , 3.TExhaus_KO  , 2.TSupply_KO, 1.TReturn_KO, 0.TFresh_KO
//   [12]:  MIX_ALM_3       : 7.FLW2_KO   , 6.FLW2_LINK  , 5.FLW1_KO   , 4.FLW1_LINK  , 3.EXT4_KO     , 2.EXT4_Link , 1.EXT3_KO   , 0.EXT3_Link
//   [13]:  MIX_ALM_3       : 7.- - - - - , 6.- - - - -  , 5.EBP2_KO   , 4.EBP2_Link  , 3.SSR_KO      , 2.SSR_Link  , 1.EBPD_KO   , 0.EBPD_Link
//-----------------------------------------------------------------------------------------------------------------------------------------------

 
enum _LISTA_ALLARMI {
//bit:     0                 1                   2                  3                  4                 5                  6                 7  
    ALM_EHD_NTC_KO,    ALM_EHD_TW_DANGER, ALM_EHD_TW_OVR,    ALM_EHD_ELECT,     ALM_EHD_TA_OVR,    ALM_EHD_SIZE,     ALM_EHD_PRESS,      ALM_EHD_LINK,   // byte 00
    ALM_CWD_NTC_KO,    ALM_CWD_TW_DANGER, ALM_CWD_TW_OVR,    ALM_CWD_ELECT,     ALM_CWD_TA_OVR,    ALM_CWD_SIZE,     ALM_NONE_14,        ALM_CWD_LINK,   // byte 01
    ALM_PEH_NTC_KO,    ALM_PEH_TW_DANGER, ALM_PEH_TW_OVR,    ALM_PEH_ELECT,     ALM_PEH_TA_OVR,    ALM_PEH_SIZE,     ALM_PEHD_PRESS,     ALM_PEH_LINK,   // byte 02
    ALM_DSC_KO,      ALM_DSC_LINK,       ALM_PCO2_EX1_KO,   ALM_PCO2_EX1_LINK, ALM_NONE_28,       ALM_PCO2_EX2_KO,  ALM_PCO2_EX2_LINK,  ALM_NONE_31,    // byte 03
    ALM_NONE_32,       ALM_NONE_33,       ALM_PRH_EX1_KO,    ALM_PRH_EX1_LINK,  ALM_NONE_36,       ALM_PRH_EX2_KO,   ALM_PRH_EX2_LINK,   ALM_NONE_39,    // byte 04
    ALM_NONE_40,       ALM_NON_41,        ALM_VOC_EX1_KO,    ALM_VOC_EX1_LINK,  ALM_NONE_44,       ALM_NONE_45,      ALM_NONE_46,        ALM_NONE_47,    // byte 05
    ALM_PCAP_KO,       ALM_PCAP_LINK,     ALM_NONE_50,       ALM_PCAF_KO,       ALM_PCAF_LINK,     ALM_NONE_53,      ALM_DPP_KO,         ALM_DPP_LINK,   // byte 06
    ALM_SMOKE_VENT_OFF,ALM_SMOKE_VENT_MAX,ALM_SMOKE_ONLY_EXT,ALM_SMOKE_ONLY_SUP,ALM_NONE_60,       ALM_NONE_61,      ALM_AWP_KO,         ALM_AWP_LINK,   // byte 07
    ALM_MR1_NO_PWR,    ALM_MR2_NO_PWR,    ALM_MR3_NO_PWR,    ALM_NONE_67,       ALM_MR1_NO_PULSE,  ALM_MR2_NO_PULSE, ALM_MR3_NO_PULSE,   ALM_NONE_71,    // byte 08 
    ALM_MF1_NO_PWR,    ALM_MF2_NO_PWR,    ALM_MF3_NO_PWR,    ALM_NONE_75,       ALM_MF1_NO_PULSE,  ALM_MF2_NO_PULSE, ALM_MF3_NO_PULSE,   ALM_NONE_79,    // byte 09 
    ALM_EB_CTRL_FAN,   ALM_EB_REM_CTRL,   ALM_BATT_KO,       ALM_BYPASS_KO,     ALM_EEP_FAULT,     ALM_FILTER,       ALM_KTS_FAULT,      ALM_NONE_87,    // byte 10
    ALM_PTFRESH_KO,    ALM_PTRET_KO,      ALM_PTSUP_KO,      ALM_PTEXA_KO,      ALM_EXT1_LINK,     ALM_EXT1_KO,      ALM_EXT2_LINK,      ALM_EXT2_KO,    // byte 11
    ALM_EXT3_LINK,     ALM_EXT3_KO,       ALM_EXT4_LINK,     ALM_EXT4_KO,       ALM_FLW1_LINK,     ALM_FLW1_KO,      ALM_FLW2_LINK,      ALM_FLW2_KO,    // byte 12 
		ALM_EBPD_LINK,     ALM_EBPD_KO,       ALM_SSR_LINK,      ALM_SSR_KO,        ALM_EBP2_LINK,     ALM_EBP2_KO,     ALM_NONE_110,       ALM_NONE_111,   // byte 13 
    TOT_ALARMS
};


#endif
