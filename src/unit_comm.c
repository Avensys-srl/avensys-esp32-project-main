#include "unit_comm.h"

#include "mqtt_app.h"
#include "protocol_Serial.h"
#include "Uart1.h"
#include "ble.h"

static const char *TAG1 = "Main Task : ";
static const char *TAG3 = "Unit Task : ";
#define BLE_POLLING_MIN_LEN 40
#define BLE_DEBUG_MIN_LEN 48

TaskHandle_t Unit_Task_xHandle = NULL;

_WBM_Com_State WBM_Com_State;

const unsigned long PollingBase_PowerOn_Serial_Milliseconds	= 1000;
const unsigned long PollingBase_PowerOff_Milliseconds		= 5000;
const unsigned long PollingDebugData_Milliseconds			= 4000;
const unsigned long	TimeoutMilliseconds			= 15000;

uint32_t	globalTimeoutMilliseconds	= 0;
enum CLKTSConnectState	currentState;
bool foundComLink	= false;
uint8_t retriesCounter = 0;
int8_t val_ret = 0;

CLKTSData gKTSData;
CLKTSDebugData gKTSDebugData;
CLKTSGlobal gKTSGlobal;
S_EEPROM gRDEeprom;

uint16_t Read_Eeprom_Request_Index = 0;
bool Eeprom_Data_received = false;

const char *address = "a0001";  // device id address

extern byte buff_ser1[128];

extern bool Unit_Partition_State;
extern TaskHandle_t Unit_Update_Task_xHandle;
extern bool Unit_Update_task_Flag;
extern volatile uint32_t Unit_Update_Counter;
extern bool Bootloader_State_Flag;
extern void Unit_Update_task (void *pvParameters);
extern bool Bootloader_Mode;
extern bool Ack_Received;
extern FILE* f;
extern int Filesize;
extern uint32_t	File_Start;
extern int Bytes_Transfered;
extern char Send_Buffer[100];
extern char Temp_Buffer[20];
extern uint32_t Millis ( void );

static void Connect_To_Unit(void);
static void WBM_Polling_Base_Data_Parse(byte* rxBuffer);
static void WBM_Debug_Data_Parse(byte* rxBuffer);
static void WBM_Read_Eeprom_Data_Parse(byte* rxBuffer);
static void WBM_Write_Eeprom_Data_Parse(byte* rxBuffer);

static void Unit_event_task(void *pvParameters)
{
    WBM_Com_State = WBM_initialize ;

    // Allarmi
	gKTSGlobal.InAlarm			= false;
	gKTSGlobal.FilterInAlarm	= false;

	// Initializing
	gKTSGlobal.RunningMode	= CLKTSRunningMode_Initializing;
	
	gKTSDebugData.PreHeater_Status	= 0;
	gKTSDebugData.Heater_Status		= 0;
	gKTSDebugData.Cooler_Status		= 0;
	gKTSDebugData.Dsc_Status		= 0;
	
	gKTSGlobal.PollingBase_TimerMilliseconds		= Millis();
	gKTSGlobal.PollingDebugData_TimerMilliseconds	= gKTSGlobal.PollingBase_TimerMilliseconds;
	
	gKTSGlobal.LastReceivedPollingBase_TimerMilliseconds		= 0;

	// Debug and Pollin for BT and WiFi

	uint8_t       *debug_data;
    uint8_t       *polling_data;

	ESP_LOGI(TAG3, "Unit Task Started");

    for (;;) {

        switch ( (uint8_t)WBM_Com_State )
		{
				case WBM_initialize:
	                ble_set_runtime_ready(false);
	                //gpio_set_level( Wifi_Ready, 1);
					vTaskDelay(2500);
				    WBM_Com_State = WBM_Communicating ;
			    currentState = CLKTSConnectState_Init;
                gpio_set_level( Wifi_Ready, 1);
				break;
			
			case WBM_Communicating:
				Connect_To_Unit ();
				break;
			
			case WBM_Connected:

				if ( Unit_Update_Counter >= 86400000 ) // 24 hours to check new unit board firmware on server
				{
					Unit_Update_Counter = 0;
					if (( Unit_Partition_State ) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag))
		    				xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2*8192, NULL, 5, &Unit_Update_Task_xHandle);
				}

				if ( Millis() < gKTSGlobal.LastReceivedPollingBase_TimerMilliseconds)
							gKTSGlobal.LastReceivedPollingBase_TimerMilliseconds	= Millis();
				
                if (gKTSGlobal.RunningMode == CLKTSRunningMode_Running)
				{
					// Aggiunge la richiesta del DebugData, se si supera il timer
					if (Millis() - gKTSGlobal.PollingDebugData_TimerMilliseconds >= PollingDebugData_Milliseconds || Millis() < gKTSGlobal.PollingDebugData_TimerMilliseconds)
						{
                            //ESP_LOGI(TAG1, "I am Here ...." );
							if( !Eeprom_Data_received )
							{
								Com_SendRequest_DataDebug();
								Eeprom_Data_received = true;
                                ESP_LOGI(TAG1, "Data Debug Request" );
								//test2 = 2;
							}
							gKTSGlobal.PollingDebugData_TimerMilliseconds	= Millis();
							gKTSGlobal.PollingBase_TimerMilliseconds	= Millis();
							globalTimeoutMilliseconds = Millis ();
						}
				}

				if (gKTSGlobal.RunningMode == CLKTSRunningMode_PowerOff || gKTSGlobal.RunningMode == CLKTSRunningMode_Running )
				{
					// Aggiunge la richiesta del PollingBase, se si supera il timer e se non esiste in coda
					if ( (Millis() - gKTSGlobal.PollingBase_TimerMilliseconds) >= (gKTSGlobal.RunningMode == CLKTSRunningMode_PowerOff ? PollingBase_PowerOff_Milliseconds : PollingBase_PowerOn_Serial_Milliseconds) || (Millis() < gKTSGlobal.PollingBase_TimerMilliseconds))
						{
							if( !Eeprom_Data_received )
							{
								Com_SendRequest_PollingBase( );
								Eeprom_Data_received = true;
                                ESP_LOGI(TAG1, "Polling Base Request" );
								//test2 = 1;
							}
							gKTSGlobal.PollingBase_TimerMilliseconds	= Millis();
							globalTimeoutMilliseconds = Millis ();
						}
				}
				
				if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
					{
						WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
						retriesCounter = 0;
						Eeprom_Data_received = false;
						Read_Eeprom_Request_Index = 0;
					}
					
				val_ret = Read_Message();
				if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
				{
					globalTimeoutMilliseconds = Millis ();
					if ( buff_ser1[IHM1_TYPE_COMAND] == COMMAND_POLLING_BASE )
						{
							 size_t polling_data_len = (buff_ser1[IHM1_POS_CRC_LO] - 4) * sizeof(uint8_t);
							        polling_data     = (uint8_t *)malloc(polling_data_len);
								for (uint16_t i = 0; i < (buff_ser1[IHM1_POS_CRC_LO] - 4); i++) {
									polling_data[i] = buff_ser1[IHM1_START_DATA + i];
								}

							if (is_mqtt_ready) {
								// Publish Polling Data to the App via MQTT
                                mqtt_publish_polling(address, polling_data, polling_data_len);
							}
							
							if (polling_data_len < BLE_POLLING_MIN_LEN) {
								uint8_t polling_compat[BLE_POLLING_MIN_LEN] = {0};
								memcpy(polling_compat, polling_data, polling_data_len);
								esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_POLLIING], BLE_POLLING_MIN_LEN, polling_compat);
								ESP_LOGW(TAG1, "Polling payload short (%u), padded to %u for BLE app compatibility",
								         (unsigned)polling_data_len, (unsigned)BLE_POLLING_MIN_LEN);
							} else {
								esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_POLLIING], polling_data_len, polling_data);
							}
							WBM_Polling_Base_Data_Parse ( buff_ser1 );
							Eeprom_Data_received = false;
							ESP_LOGI(TAG1, "Polling Data Received" );
							free(polling_data);
						}
						else
							if ( buff_ser1[IHM1_TYPE_COMAND] == COMMAND_DATA_DEBUG )
								{
									size_t debug_data_len = (buff_ser1[IHM1_POS_CRC_LO] - 4) * sizeof(uint8_t);
										debug_data            = (uint8_t *)malloc(debug_data_len);
										for (uint16_t i = 0; i < (buff_ser1[IHM1_POS_CRC_LO] - 4); i++) {
											debug_data[i] = buff_ser1[IHM1_START_DATA + i];
										}

									if (is_mqtt_ready) {
										// Publish Debug Data to the App via MQTT
                                        mqtt_publish_debug(address, debug_data, debug_data_len);
									}

									if (debug_data_len < BLE_DEBUG_MIN_LEN) {
										uint8_t debug_compat[BLE_DEBUG_MIN_LEN] = {0};
										memcpy(debug_compat, debug_data, debug_data_len);
										esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_DEBUG_DATA], BLE_DEBUG_MIN_LEN, debug_compat);
										ESP_LOGW(TAG1, "Debug payload short (%u), padded to %u for BLE app compatibility",
										         (unsigned)debug_data_len, (unsigned)BLE_DEBUG_MIN_LEN);
									} else {
										esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_DEBUG_DATA], debug_data_len, debug_data);
									}
									WBM_Debug_Data_Parse ( buff_ser1 );
									Eeprom_Data_received = false;
									ESP_LOGI(TAG1, "Debug Data Received" );
									free(debug_data);
								}
							else
								if ( buff_ser1[IHM1_TYPE_COMAND] == COMMAND_READ_EEPROM )
								{
									WBM_Read_Eeprom_Data_Parse ( buff_ser1 );

									if (is_mqtt_ready) {
									// Publish EEPROM Data to the App via MQTT
                                    mqtt_publish_eeprom(address, (u_int8_t *)&gRDEeprom, sizeof(gRDEeprom));
									}	

									esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (u_int8_t *)&gRDEeprom);
									Eeprom_Data_received = false;
                                    ESP_LOGI(TAG1, "Eeprom Parse Data" );
								}
								else
									if ( buff_ser1[IHM1_TYPE_COMAND] == COMMAND_WRITE_EEPROM )
									{
										WBM_Write_Eeprom_Data_Parse ( buff_ser1 );
										Eeprom_Data_received = false;
									}
				}
                else if ( val_ret < 0 )
                {
                    Eeprom_Data_received = false;
                    memset ( buff_ser1, 0, sizeof( buff_ser1));
                }
					
				if ( (Read_Eeprom_Request_Index != 0) && !Eeprom_Data_received )
				{
					if ( (Read_Eeprom_Request_Index & 0x1 ) == 0x01 )
					{
						Com_SendRequest_ReadEeprom1( EEepromSection_Info );
						Eeprom_Data_received = true;
						Read_Eeprom_Request_Index &= 0xFFFE;
                        ESP_LOGI(TAG1, "Eeprom Read EEepromSection_Info" );
					}
					else
						if ( (Read_Eeprom_Request_Index & 0x2 ) == 0x02 )
						{
						  Com_SendRequest_ReadEeprom1( EEepromSection_SettingPar );
						  Eeprom_Data_received = true;
						  Read_Eeprom_Request_Index &= 0xFFFD;
                          ESP_LOGI(TAG1, "Eeprom Read EEepromSection_SettingPar" );
						}
						else
							if ( (Read_Eeprom_Request_Index & 0x4 ) == 0x04 )
							{
								Com_SendRequest_ReadEeprom1( EEepromSection_SetTemp );
								Eeprom_Data_received = true;
								Read_Eeprom_Request_Index &= 0xFFFB;
                                ESP_LOGI(TAG1, "Eeprom Read EEepromSection_SetTemp" );
							}
							else
								if ( (Read_Eeprom_Request_Index & 0x8 ) == 0x08 )
								{
									Com_SendRequest_ReadEeprom1( EEepromSection_DayProg );
									Eeprom_Data_received = true;
									Read_Eeprom_Request_Index &= 0xFFF7;
                                    ESP_LOGI(TAG1, "Eeprom Read EEepromSection_DayProg" );
								}
								else
									if ( (Read_Eeprom_Request_Index & 0x10 ) == 0x10 ) // button3
									{
										//Com_SendRequest_WriteEeprom( Button_3.Start_Adress, Button_3.Count);
										Eeprom_Data_received = true;
										Read_Eeprom_Request_Index &= 0xFFEF;
									}
									else
										if ( (Read_Eeprom_Request_Index & 0x20 ) == 0x20 ) // button2
										{
											//Com_SendRequest_WriteEeprom( Button_2.Start_Adress, Button_2.Count);
											Eeprom_Data_received = true;
											Read_Eeprom_Request_Index &= 0xFFDF;
										}
										else
											if ( (Read_Eeprom_Request_Index & 0x40 ) == 0x40 ) // button1
											{
												//Com_SendRequest_WriteEeprom( Button_1.Start_Adress, Button_1.Count);
												Eeprom_Data_received = true;
												Read_Eeprom_Request_Index &= 0xFFBF;
											}
											else
												if ( (Read_Eeprom_Request_Index & 0x80 ) == 0x80 ) // boost
													{
														//Com_SendRequest_WriteEeprom( Boost.Start_Adress, Boost.Count);
														Eeprom_Data_received = true;
														Read_Eeprom_Request_Index &= 0xFF7F;
													}
													else
														if ( (Read_Eeprom_Request_Index & 0x100 ) == 0x100 ) // Filter
														{
															//Com_SendRequest_WriteEeprom( Button_Filter.Start_Adress, Button_Filter.Count);
															Eeprom_Data_received = true;
															Read_Eeprom_Request_Index &= 0xFEFF;
														}
														else
															if ( (Read_Eeprom_Request_Index & 0x200 ) == 0x200 ) // Stepless
															{
																//Com_SendRequest_WriteEeprom( Stepless.Start_Adress, Stepless.Count);
																Eeprom_Data_received = true;
																Read_Eeprom_Request_Index &= 0xFDFF;
															}
															else
																if ( (Read_Eeprom_Request_Index & 0x800 ) == 0x800 ) // EEepromSection_Info
																	{
																		Com_SendRequest_WriteEeprom1( EEepromSection_Info );
																		Eeprom_Data_received = true;
																		Read_Eeprom_Request_Index &= 0xF7FF;
																		ESP_LOGI(TAG1, "Eeprom Write EEepromSection_Info" );
																	}
																	else
																		if ( (Read_Eeprom_Request_Index & 0x1000 ) == 0x1000 ) // EEepromSection_SettingPar
																			{
																				Com_SendRequest_WriteEeprom1( EEepromSection_SettingPar );
																				Eeprom_Data_received = true;
																				Read_Eeprom_Request_Index &= 0xEFFF;
																				ESP_LOGI(TAG1, "Eeprom Write EEepromSection_SettingPar" );
																			}
																			else
																				if ( (Read_Eeprom_Request_Index & 0x2000 ) == 0x2000 ) // EEepromSection_SetTemp
																					{
																						Com_SendRequest_WriteEeprom1( EEepromSection_SetTemp );
																						Eeprom_Data_received = true;
																						Read_Eeprom_Request_Index &= 0xDFFF;
																						ESP_LOGI(TAG1, "Eeprom Write EEepromSection_SetTemp" );
																					}
																					else
																						if ( (Read_Eeprom_Request_Index & 0x4000 ) == 0x4000 ) // EEepromSection_DayProg
																							{
																								Com_SendRequest_WriteEeprom1( EEepromSection_DayProg );
																								Eeprom_Data_received = true;
																								Read_Eeprom_Request_Index &= 0xBFFF;
																								ESP_LOGI(TAG1, "Eeprom Write EEepromSection_DayProg" );
																							}
																							else
																								if ( (Read_Eeprom_Request_Index & 0x8000 ) == 0x8000 ) // EEepromSection_HWSetting
																									{
																										Com_SendRequest_WriteEeprom1( EEepromSection_HWSetting );
																										Eeprom_Data_received = true;
																										Read_Eeprom_Request_Index &= 0x7FFF;
																										ESP_LOGI(TAG1, "Eeprom Write EEepromSection_HWSetting" );
																									}
																		

				}
				
				break;
			
				case WBM_Error:
	                    ble_set_runtime_ready(false);
	                    gpio_set_level( Wifi_Ready, 0);
					break;
		}
        vTaskDelay(1);
    }
}
void Connect_To_Unit ( void )
{
    uint8_t* Pointer;
	uint8_t* Pointer1;
	
	switch ( (uint8_t)currentState )
	{
		case CLKTSConnectState_Init:
			globalTimeoutMilliseconds = Millis ();
		    currentState = CLKTSConnectState_LinkConnecting;
		    retriesCounter = 0;
			break;
		case CLKTSConnectState_LinkConnecting: // send polling Request
			if ( (retriesCounter < 2) && !foundComLink )
			{
				Com_SendRequest_PollingBase( );
                ESP_LOGI(TAG1, "Pooling Base Request" );
				retriesCounter++;
				globalTimeoutMilliseconds = Millis ();
			}
			currentState = CLKTSConnectState_TrySerialLink;
			break;
		case CLKTSConnectState_TrySerialLink: // wait response from Unit Board
			
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					if ( retriesCounter == 2 )
					{
							WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
							retriesCounter = 0;
							Uart1_Initialize_1 ( );
							currentState = CLKTSConnectState_Init ;
							WBM_Com_State = WBM_Communicating ;
							ESP_LOGI(TAG1, "Restart Communication at 921600 bauds." );
					}
					else
					{
						globalTimeoutMilliseconds = Millis ();
						currentState = CLKTSConnectState_LinkConnecting;
						vTaskDelay(100);
					}
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						currentState = CLKTSConnectState_LinkConnected;
					}
					if( Bootloader_Mode )
						{
							currentState = Bootloader_State;
							ESP_LOGI(TAG1, "Bootloader Mode" );
						}
				}
			break;
		case CLKTSConnectState_LinkConnected:
			foundComLink = true;
			vTaskDelay(100);
		    Com_SendRequest_ReadEeprom1( EEepromSection_Info );
		    currentState = CLKTSConnectState_ReadEeprom_Info;
		    globalTimeoutMilliseconds = Millis ();
            ESP_LOGI(TAG1, "Read Eeprom Request" );
			break;
		case CLKTSConnectState_ReadEeprom_Info:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, AddrUnit );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_ReadEeprom1( EEepromSection_Configuration );
						currentState = CLKTSConnectState_ReadEeprom_Configuration;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Read Eeprom Request" );
					}
				}
			break;
		case CLKTSConnectState_ReadEeprom_Configuration:
			 if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, numMotors );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_ReadEeprom1( EEepromSection_SettingPar );
						currentState = CLKTSConnectState_ReadEeprom_SettingPar;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Read Eeprom Request" );
					}
				}
			break;
		case CLKTSConnectState_ReadEeprom_SettingPar:
			 if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, Set_Power_ON );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_ReadEeprom1( EEepromSection_SetTemp );
						currentState = CLKTSConnectState_ReadEeprom_SetTemp;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Read Eeprom Request" );
					}
				}
			break;
		case CLKTSConnectState_ReadEeprom_SetTemp:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, Bypass_minTempExt );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_ReadEeprom1( EEepromSection_DayProg );
						currentState = CLKTSConnectState_ReadEeprom_DayProg;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Read Eeprom Request" );
					}
				}
			break;
		case CLKTSConnectState_ReadEeprom_DayProg:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, sDayProg );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_ReadEeprom1( EEepromSection_HWSetting );
						currentState = CLKTSConnectState_ReadEeprom_HWSetting;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Read Eeprom Request" );
					}
				}
			break;
		case CLKTSConnectState_ReadEeprom_HWSetting:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						Pointer = &buff_ser1[IRSR_START_DATA_EEPROM];
						Pointer1 = (uint8_t*)&gRDEeprom + offsetof( S_EEPROM, numMotors );
						memcpy ( Pointer1, Pointer, buff_ser1[IRSR_ADDR_NUM_BYTE_EEP] );
						vTaskDelay(100);
						Com_SendRequest_PollingBase( );
						currentState = CLKTSConnectState_PollingBase;
						globalTimeoutMilliseconds = Millis ();
                        ESP_LOGI(TAG1, "Polling Base Request" );
					}
				}
			break;
		case CLKTSConnectState_PollingBase:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Unit board after Timeout
					retriesCounter = 0;
				}
			else
				{
					val_ret = Read_Message();
					if(val_ret > RUN_DOWNLOAD )  // if(val_ret > 0)
					{
						currentState = CLKTSConnectState_Connected;
						globalTimeoutMilliseconds = Millis ();
					}
				}
			break;
		case CLKTSConnectState_Connected:
			if (gRDEeprom.Set_Power_ON == CLKTSPowerMode_Off)
					gKTSGlobal.RunningMode	= CLKTSRunningMode_PowerOff;
			// L'unità è in Power On
			else
					gKTSGlobal.RunningMode	= CLKTSRunningMode_Running;

			gKTSGlobal.LastReceivedPollingBase_TimerMilliseconds	= Millis();
			gKTSGlobal.PollingBase_TimerMilliseconds		= Millis();
			gKTSGlobal.PollingDebugData_TimerMilliseconds	= gKTSGlobal.PollingBase_TimerMilliseconds;
				WBM_Com_State = WBM_Connected ;
				ble_set_runtime_ready(true);
				WBM_Polling_Base_Data_Parse ( buff_ser1 );
	            ESP_LOGI(TAG1, "WBM connected to Unit" );
			esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (u_int8_t *)&gRDEeprom);

			address = (const char *)&gRDEeprom.SerialString;

			mqtt_subscribe_app_topics(address);

			vTaskDelay(350);

			if (is_mqtt_ready) {
			// Publish EEPROM Data to the App via MQTT
            mqtt_publish_eeprom(address, (u_int8_t *)&gRDEeprom, sizeof(gRDEeprom));
			ESP_LOGI(TAG1, "gRDEeprom size : %d", sizeof (S_EEPROM));
			}	
			break;

		case Bootloader_State:
			if ( !Unit_Update_task_Flag ) // check if we are not in unit firmware update task
			{
				Bootloader_State_Flag = true; // here we are in bootloader upgrade mode, so no update is allowed from server
				vTaskDelay(5);
				f = fopen ("/spiffs/firmware.bin", "r");
				if ( f == NULL) // the file does not exist create a new one 
					{
						Filesize = 0; // file does not exist , send 0 to bootloader
						// send file size to bootloader
						memset ( Send_Buffer, 0, sizeof(Send_Buffer));
						Send_Buffer[0] = ((Filesize >> 20) & 0xF) + 48;
						Send_Buffer[1] = ((Filesize >> 16) & 0xF) + 48;
						Send_Buffer[2] = ((Filesize >> 12) & 0xF) + 48;
						Send_Buffer[3] = ((Filesize >> 8) & 0xF) + 48;
						Send_Buffer[4] = ((Filesize & 0xF0) >> 4) + 48;
						Send_Buffer[5] = (Filesize & 0xF) + 48;
						memset ( Send_Buffer, 48, sizeof(Send_Buffer));
						Uart_Write ( (char*)Send_Buffer,  14);
						currentState = Bootloader_State_end;
					}
				else
					{
						if ( fseek(f, 0, SEEK_END) == 0 )
						{
							Filesize = f->_offset;
							ESP_LOGI(TAG1, "file size = %i", Filesize);
							rewind( f );
							// send file size to bootloader
							memset ( Send_Buffer, 0, sizeof(Send_Buffer));
							Send_Buffer[0] = ((Filesize >> 20) & 0xF) + 48;
							Send_Buffer[1] = ((Filesize >> 16) & 0xF) + 48;
							Send_Buffer[2] = ((Filesize >> 12) & 0xF) + 48;
							Send_Buffer[3] = ((Filesize >> 8) & 0xF) + 48;
							Send_Buffer[4] = ((Filesize & 0xF0) >> 4) + 48;
							Send_Buffer[5] = (Filesize & 0xF) + 48;
							fread(&File_Start, 1, sizeof(File_Start), f);
							Send_Buffer[6] = ((File_Start >> 28) & 0xF) + 48;
							Send_Buffer[7] = ((File_Start >> 24) & 0xF) + 48;
							Send_Buffer[8] = ((File_Start >> 20) & 0xF) + 48;
							Send_Buffer[9] = ((File_Start >> 16) & 0xF) + 48;
							Send_Buffer[10] = ((File_Start >> 12) & 0xF) + 48;
							Send_Buffer[11] = ((File_Start >> 8) & 0xF) + 48;
							Send_Buffer[12] = ((File_Start & 0xF0) >> 4) + 48;
							Send_Buffer[13] = (File_Start & 0xF) + 48;
							rewind( f );
							Uart_Write ( Send_Buffer,  14);
							currentState = Bootloader_State1;
						}
						else
						{
							Filesize = 0; // file is empty , send 0 to bootloader
							fclose(f);
							// send file size to bootloader
							memset ( Send_Buffer, 48, sizeof(Send_Buffer));
							Uart_Write ( Send_Buffer,  14);
							currentState = Bootloader_State_end;
						}
					}
			}
			break;

		case Bootloader_State1:
				val_ret = Read_Message();
				if ( Ack_Received)
					{
						Ack_Received = false;
						//ESP_LOGI(TAG1, "ACK Received");
						memset ( Send_Buffer, 0, sizeof(Send_Buffer));
						if ((Filesize - Bytes_Transfered ) > 16)
							{
								memset ( Temp_Buffer, 0, sizeof(Temp_Buffer));
								fread(Temp_Buffer, 1, 16, f);
								for (int index1 = 0; index1 < 4; index1++)
									{
										Send_Buffer[index1 * 8] = (((Temp_Buffer[(index1 * 4)+3] & 0xF0) >> 4) + 48);
										Send_Buffer[(index1 * 8) + 1] = ((Temp_Buffer[(index1 * 4) + 3] & 0xF) + 48);
										Send_Buffer[(index1 * 8) + 2] = (((Temp_Buffer[(index1 * 4) + 2] & 0xF0) >> 4) + 48);
										Send_Buffer[(index1 * 8) + 3] = ((Temp_Buffer[(index1 * 4) + 2] & 0xF) + 48);
										Send_Buffer[(index1 * 8) + 4] = (((Temp_Buffer[(index1 * 4) + 1] & 0xF0) >> 4) + 48);
										Send_Buffer[(index1 * 8) + 5] = ((Temp_Buffer[(index1 * 4) + 1] & 0xF) + 48);
										Send_Buffer[(index1 * 8) + 6] = (((Temp_Buffer[(index1 * 4)] & 0xF0) >> 4) + 48);
										Send_Buffer[(index1 * 8) + 7] = ((Temp_Buffer[(index1 * 4)] & 0xF) + 48);
									}
								Uart_Write ( Send_Buffer,  32);
								Bytes_Transfered = Bytes_Transfered + 16;	
							}
						else
							if ((Filesize - Bytes_Transfered ) != 0)
								{
									memset ( Temp_Buffer, 0, sizeof(Temp_Buffer));
									fread(Temp_Buffer, 1, Filesize - Bytes_Transfered, f);
									for (int index1 = 0; index1 < ((Filesize - Bytes_Transfered ) / 4); index1++)
										{
											Send_Buffer[index1 * 8] = (((Temp_Buffer[(index1 * 4)+3] & 0xF0) >> 4) + 48);
											Send_Buffer[(index1 * 8) + 1] = ((Temp_Buffer[(index1 * 4)+3] & 0xF) + 48);
											Send_Buffer[(index1 * 8) + 2] = (((Temp_Buffer[(index1 * 4) + 2] & 0xF0) >> 4) + 48);
											Send_Buffer[(index1 * 8) + 3] = ((Temp_Buffer[(index1 * 4) + 2] & 0xF) + 48);
											Send_Buffer[(index1 * 8) + 4] = (((Temp_Buffer[(index1 * 4) + 1] & 0xF0) >> 4) + 48);
											Send_Buffer[(index1 * 8) + 5] = ((Temp_Buffer[(index1 * 4) + 1] & 0xF) + 48);
											Send_Buffer[(index1 * 8) + 6] = (((Temp_Buffer[(index1 * 4)] & 0xF0) >> 4) + 48);
											Send_Buffer[(index1 * 8) + 7] = ((Temp_Buffer[(index1 * 4)] & 0xF) + 48);
										}
									Uart_Write ( Send_Buffer,  (Filesize - Bytes_Transfered ) * 2);
									//Bytes_Transfered = Bytes_Transfered + 16;
									currentState = Bootloader_State_end;
									fclose ( f );
									f = NULL;
									ESP_LOGI(TAG1, "Transfert Finished");
									gpio_set_level( Wifi_Ready, 0);
								}	
					}
			break;

		case Bootloader_State_end:
			// WBM waiting for booloader to restart the board
			break;	
	}
		
}
void WBM_Polling_Base_Data_Parse ( byte* rxBuffer )
{
	int eventsCounter;
		
	gKTSData.Measure_Temp[ 0 ]	= ((float) MAKESHORT( rxBuffer, IRSP_MEASURE_TEMP_1_HI, IRSP_MEASURE_TEMP_1_LO ) / 10.0);
	gKTSData.Measure_Temp[ 1 ]	= ((float) MAKESHORT( rxBuffer, IRSP_MEASURE_TEMP_2_HI, IRSP_MEASURE_TEMP_2_LO ) / 10.0);
	gKTSData.Measure_Temp[ 2 ]	= ((float) MAKESHORT( rxBuffer, IRSP_MEASURE_TEMP_3_HI, IRSP_MEASURE_TEMP_3_LO ) / 10.0);
	gKTSData.Measure_Temp[ 3 ]	= ((float) MAKESHORT( rxBuffer, IRSP_MEASURE_TEMP_4_HI, IRSP_MEASURE_TEMP_4_LO ) / 10.0);

	// Misura della sonda RH interna all'Unita'
	gKTSData.Measure_RH_max		= rxBuffer[ IRSP_MEASURE_RH_SENS ];

	// Misura della sonda CO2 interna all'Unita'
	gKTSData.Measure_CO2_max	= MAKEWORD( rxBuffer, IRSP_MEASURE_CO2_SENS_HI, IRSP_MEASURE_CO2_SENS_LO );

	// Misura della sonda VOC interna all'Unita'
	gKTSData.Measure_VOC_max	= MAKEWORD( rxBuffer, IRSP_MEASURE_VOC_SENS_HI, IRSP_MEASURE_VOC_SENS_LO );

	// Misura della sonda AWP
	gKTSData.Measure_Temp_AWP	= ((float) MAKESHORT( rxBuffer, IRSP_MEASURE_AWP_SENS_HI, IRSP_MEASURE_AWP_SENS_LO ) / 10.0);
	
	// Status
	gKTSData.Status_Unit		= MAKEWORD( rxBuffer, IRSP_STATUS_UNIT_HI, IRSP_STATUS_UNIT_LO );

	// Status Weekly
	gKTSData.Status_Weekly		= rxBuffer[ IRSP_STATUS_WEEKLY ];

	gKTSData.InputMeasure1		= rxBuffer[ IRSP_MEASURE_IN1 ];
	gKTSData.InputMeasure2		= rxBuffer[ IRSP_MEASURE_IN2 ];

	// Allarmi/ Eventi
	memcpy( gKTSData.Events, rxBuffer + IRSP_EVENT_BYTE_00, sizeof(gKTSData.Events) );

	// IncreaseSpeed RH/CO2
	gKTSData.IncreaseSpeed_RH_CO2	= rxBuffer[ IRSP_INCREASE_SPEED_RH_CO2 ];

	// Contatori sezioni EEPROM
	gKTSData.CntUpdate_info			= rxBuffer[ IRSP_CNT_UPDATE_EEP_INFO ];
	gKTSData.CntUpdate_SettingPar	= rxBuffer[ IRSP_CNT_UPDATE_EEP_SETTING_PAR ];
	gKTSData.CntUpdate_SetTemp		= rxBuffer[ IRSP_CNT_UPDATE_EEP_SETP_TEMP ];
	gKTSData.CntUpdate_dayProg		= rxBuffer[ IRSP_CNT_UPDATE_EEP_WEEKLY ];
	
	//gKTSData.DSC_Status				= rxBuffer[ IRSP_NONE_0 ];
	
	// Controlla se esistono degli Allarmi attivi ed imposta le variabili di ambiente
	gKTSGlobal.InAlarm			= false;
	gKTSGlobal.FilterInAlarm	= false;
	
	for ( eventsCounter = 0; eventsCounter < 13; eventsCounter++ )
	{
		if (gKTSData.Events[ eventsCounter ])
		{
			if (eventsCounter == 10)
			{
				gKTSGlobal.FilterInAlarm	= gKTSData.Events[ eventsCounter ] & 0x20;
				
				if (gKTSData.Events[ eventsCounter ] != 0x20)
				{
					gKTSGlobal.InAlarm	= true;
					
				}
			}
			else
			{
				gKTSGlobal.InAlarm	= true;
				
			}
		}
	}
	
	if ( !gKTSGlobal.InAlarm )
	{
		
	}
	
	if (gKTSData.CntUpdate_info != gRDEeprom.cntUpdate_info)
		{
				Read_Eeprom_Request_Index |= 0x1;
		}

	if (gKTSData.CntUpdate_SettingPar != gRDEeprom.cntUpdate_SettingPar)
		{
				Read_Eeprom_Request_Index |= 0x2;
		}

	if (gKTSData.CntUpdate_SetTemp != gRDEeprom.cntUpdate_SetTemp)
		{
				Read_Eeprom_Request_Index |= 0x4;
		}

	if (gKTSData.CntUpdate_dayProg != gRDEeprom.cntUpdate_dayProg)
		{
				Read_Eeprom_Request_Index |= 0x8;
		}
		
	// Aggiorna il timer dell'ultimo polling ricevuto
	gKTSGlobal.LastReceivedPollingBase_TimerMilliseconds	= Millis();
	
}
void WBM_Debug_Data_Parse ( byte* rxBuffer )
{
	gKTSDebugData.PreHeater_Status	= rxBuffer[ IRSD_STATUS_PREHEATER ];
	gKTSDebugData.Heater_Status		= rxBuffer[ IRSD_STATUS_HEATER ];
	gKTSDebugData.Cooler_Status		= rxBuffer[ IRSD_STATUS_COOLER ];
	gKTSDebugData.Dsc_Status		= rxBuffer[ IRSD_STATUS_DSC ];

	gKTSDebugData.CAPR_LinkLevel = rxBuffer[ IRSD_LEV_LINK_CAPR ];
    gKTSDebugData.CAPS_LinkLevel = rxBuffer[ IRSD_LEV_LINK_CAPS ];
    gKTSDebugData.CAPR_Status = rxBuffer[ IRSD_STATUS_CAPR ];
    gKTSDebugData.CAPS_Status = rxBuffer[ IRSD_STATUS_CAPS ];
    
    gKTSDebugData.MeasureAirflow_CAPR = ((word) MAKEWORD( rxBuffer, IRSD_MEASUR_AF_CAPR_HI, IRSD_MEASUR_AF_CAPR_LO ));
    gKTSDebugData.MeasureAirflow_CAPS = ((word) MAKEWORD( rxBuffer, IRSD_MEASUR_AF_CAPS_HI, IRSD_MEASUR_AF_CAPS_LO ));
    gKTSDebugData.Measures_Pressure_CAPR = (int)(((int)rxBuffer[IRSD_MEASUR_PA_CAPR_HI] << 8) +  rxBuffer[IRSD_MEASUR_PA_CAPR_LO]); // 2 Byte
    gKTSDebugData.Measures_Pressure_CAPS =  (int)(((int)rxBuffer[IRSD_MEASUR_PA_CAPS_HI] << 8) +  rxBuffer[IRSD_MEASUR_PA_CAPS_LO]);
}
void WBM_Read_Eeprom_Data_Parse ( byte* rxBuffer )
{
	memcpy( ((byte*) &gRDEeprom) + rxBuffer[ IRSR_ADDR_BYTE_START_EEP ],
		((byte*) rxBuffer) + IRSR_START_DATA_EEPROM,
		rxBuffer[ IRSR_ADDR_NUM_BYTE_EEP ] );
}
void WBM_Write_Eeprom_Data_Parse ( byte* rxBuffer )
{
	if ( rxBuffer[ IRSW_RESULT_W ] == '0') // write OK
	{
		ESP_LOGI(TAG1, "Eeprom wrote successfully (unit ACK=%c)", rxBuffer[IRSW_RESULT_W] );
	}
	else
	{
		ESP_LOGW(TAG1, "Eeprom writing error (unit ACK=%c)", rxBuffer[IRSW_RESULT_W] );
	}

		// Controlla che se il counter update INFO e' maggiore di 1, richiede la rilettura della eeprom
		//Vérifier que si le compteur de mise à jour INFO est supérieur à 1, cela nécessite de relire l'eeprom
		if (MAX( rxBuffer[ IRSW_CNT_UPDATE_EEP_INFO ], gRDEeprom.cntUpdate_info ) -
			MIN( rxBuffer[ IRSW_CNT_UPDATE_EEP_INFO ], gRDEeprom.cntUpdate_info ) >= 1)
			{
				Read_Eeprom_Request_Index |= 0x1;
			}
		gRDEeprom.cntUpdate_info	= rxBuffer[ IRSW_CNT_UPDATE_EEP_INFO ];

		// Controlla che se il counter update SETTING_PAR e' maggiore di 1, richiede la rilettura della eeprom
		if (MAX( rxBuffer[ IRSW_CNT_UPDATE_EEP_SETTING_PAR ], gRDEeprom.cntUpdate_SettingPar ) -
			MIN( rxBuffer[ IRSW_CNT_UPDATE_EEP_SETTING_PAR ], gRDEeprom.cntUpdate_SettingPar ) >= 1)
			{
				Read_Eeprom_Request_Index |= 0x2;
			}
		gRDEeprom.cntUpdate_SettingPar	= rxBuffer[ IRSW_CNT_UPDATE_EEP_SETTING_PAR ];
					
		// Controlla che se il counter update SETP_TEMP e' maggiore di 1, richiede la rilettura della eeprom
		if (MAX( rxBuffer[ IRSW_CNT_UPDATE_EEP_SETP_TEMP ], gRDEeprom.cntUpdate_SetTemp ) -
			MIN( rxBuffer[ IRSW_CNT_UPDATE_EEP_SETP_TEMP ], gRDEeprom.cntUpdate_SetTemp ) >= 1)
			{
				Read_Eeprom_Request_Index |= 0x4;
			}
		gRDEeprom.cntUpdate_SetTemp	= rxBuffer[ IRSW_CNT_UPDATE_EEP_SETP_TEMP ];
					
		// Controlla che se il counter update WEEKLY e' maggiore di 1, richiede la rilettura della eeprom
		if (MAX( rxBuffer[ IRSW_CNT_UPDATE_EEP_WEEKLY ], gRDEeprom.cntUpdate_dayProg ) -
			MIN( rxBuffer[ IRSW_CNT_UPDATE_EEP_WEEKLY ], gRDEeprom.cntUpdate_dayProg ) >= 1)
			{
				Read_Eeprom_Request_Index |= 0x8;
			}
		gRDEeprom.cntUpdate_dayProg	= rxBuffer[ IRSW_CNT_UPDATE_EEP_WEEKLY ];
			
}

void unit_comm_start(void)
{
   ESP_ERROR_CHECK (Uart1_Initialize ( ));
   xTaskCreate(Unit_event_task, "Unit_event_task", 2*2048, NULL, 2, &Unit_Task_xHandle);
}

