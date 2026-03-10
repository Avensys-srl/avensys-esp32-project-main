#ifndef UNIT_COMM_H
#define UNIT_COMM_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void unit_comm_start(void);

extern _WBM_Com_State WBM_Com_State;
extern enum CLKTSConnectState currentState;
extern uint16_t Read_Eeprom_Request_Index;
extern S_EEPROM gRDEeprom;

#ifdef __cplusplus
}
#endif

#endif
