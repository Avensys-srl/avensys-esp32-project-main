#include "main.h"
#include "CL_WBM.h"
#include "protocol_Serial.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_spiffs.h"

#define MAX_HTTP_OUTPUT_BUFFER 1024
#define VERSION_URL "https://mybestdocument.altervista.org/ESP32/version.json"
#define BUFFSIZE 1024

//#define Quarke_URL "https://www.alsaqr.io/.well-known/publicFIleTest/T533422LK/File1.bin"
//#define Quarke_URL "https://dl.espressif.com/dl/esp-idf/ci/esp_http_client_demo.txt"
#define Quarke_URL "https://www.avensys-srl.com/ESP32/Quarke_Main.bin"


char *BROKER_URL = "mqtts://a3qi68kx39kjfk-ats.iot.eu-central-1.amazonaws.com:8883";
// char WIFI_SSID[30]     = "FiberHGW_ZYF75B";
// char WIFI_PASSWORD[30] = "XNfXFYxURF9K";
//char WIFI_SSID[MAX_SSID_LENGTH + 1]  = "FASTWEB-DA0E83";
//char WIFI_PASSWORD[MAX_PASSWORD_LENGTH + 1] = "TGRRFEZGZR";
char WIFI_SSID[MAX_SSID_LENGTH + 1]     = "TP-LINK_3965";
char WIFI_PASSWORD[MAX_PASSWORD_LENGTH + 1] = "558FC4A469ZE6";

static const char *TAG_OTA =  "OTA";
static const char *CURRENT_VERSION =  "1.0.1"; //versione corrente del firmware
bool Ota_In_Progress = false;

static char *json_response = NULL;
static int total_len = 0;

bool is_mqtt_ready = false;

bool send_eeprom_read_request = false;

bool connect_to_wifi   = false;
bool wifi_is_ssid_send = false;
bool wifi_is_pass_send = false;

// Variables
TimerHandle_t xTimer_Led = NULL;
uint16_t Counter_Led = 0;
uint16_t Counter_Led1 = 0;
uint8_t base_mac_addr[6] = {0};
char Serial_Number[20] = {0};
uint8_t Serial_Number_Size = 0;
uint8_t buffer[20] = {0};

uint8_t error = 0;

static const char *TAG1 = "Main Task : ";

volatile uint32_t millis_tick = 0;

TaskHandle_t Quarke_Task_xHandle = NULL;
static const char *TAG3 = "Quarke Task : ";
bool Quarke_Partition_State = false;
TaskHandle_t Quarke_Update_Task_xHandle = NULL;
static const char *TAG_QRK_UPDATE =  "Quarke Update : ";
bool File_opened = false;
FILE* f = NULL;
char Buffer_Test[1536];
int Firmware_version = 0;
int Firmware_version_server = 0;
char Load_Counter = 0;
char* Pointer = NULL;
uint16_t Data_Counter = 0;
bool Quarke_Update_task_Flag = false;
volatile uint32_t Quarke_Update_Counter = 0;
int Filesize = 0;
bool Bootloader_State_Flag = false;
char Send_Buffer[100];
int Bytes_Transfered = 0;
char Temp_Buffer[20];

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

// Contiene i dati in RAM (PollingBase)
CLKTSData gKTSData;

// Dati del polling debug data
CLKTSDebugData gKTSDebugData;

// Contiene le variabili globali
CLKTSGlobal gKTSGlobal;

// Contiene i dati della EPROM in RAM ----
S_EEPROM gRDEeprom;

uint16_t Read_Eeprom_Request_Index = 0;
bool Eeprom_Data_received = false;

__FKI_Board FKI_Board;

extern byte buff_ser1[128];     // buffer di appoggio

uint32_t Millis ( void );
void Connect_To_QRK ( void );
void WBM_Polling_Base_Data_Parse ( byte* rxBuffer );
void WBM_Debug_Data_Parse ( byte* rxBuffer );
void WBM_Read_Eeprom_Data_Parse ( byte* rxBuffer );
void WBM_Write_Eeprom_Data_Parse ( byte* rxBuffer );
void Quarke_Update_task (void *pvParameters);

// MQTT address

int msg_id;
char topic_buffer[64];
/**
 * @brief The MQTT address of the device.
 *
 * This variable stores the MQTT address of the device. It is used to specify the topic
 * to which the device will publish or subscribe.
 *
 * @note The address should be a string representation of the device ID.
 */
const char *address = "a0001";  // device id address

esp_mqtt_client_handle_t client;

/**
 * @brief External declarations of binary data arrays.
 *
 * These declarations define the start and end addresses of various binary data arrays
 * used in the project. The arrays include AWS root CA certificate, certificate PEM,
 * and private PEM key.
 *
 * The arrays are defined using the asm directive to specify their start and end addresses.
 * The arrays are used for secure communication with AWS services.
 */
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");         /**< Start address of AWS root CA certificate array */
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");             /**< End address of AWS root CA certificate array */
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start"); /**< Start address of certificate PEM array */
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");     /**< End address of certificate PEM array */
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");         /**< Start address of private PEM key array */
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");             /**< End address of private PEM key array */
extern const uint8_t Alsaqr_pem_start[] asm("_binary_Alsaqr_pem_start");
extern const uint8_t Alsaqr_pem_end[] asm("_binary_Alsaqr_pem_end");
extern const uint8_t espressif_pem_start[] asm("_binary_espressif_pem_start");
extern const uint8_t espressif_pem_end[] asm("_binary_espressif_pem_end");

bool read_eeprom_data  = true;
bool write_eeprom_data = false;
bool is_bluetooth      = true;

extern bool Bootloader_Mode;
extern bool Ack_Received;

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(GATTS_TABLE_TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void publish_debug_message(const uint8_t *data, size_t data_len, const char *topic, const char *address) {
        char json_buffer[1024]; // Adjust the size according to your message length
        int json_len;

        // Format the JSON message
        json_len = snprintf(json_buffer, sizeof(json_buffer), "{\"message\":\"");

        // Append data array
        for (size_t i = 0; i < data_len; i++) {
            json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "%d", data[i]);
            if (i < data_len - 1) {
                json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, ",");
            }
        }

        // Finish JSON message
        json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "\",\"topic\":\"%s\",\"address\":\"%s\"}", topic, address);

        // Publish the JSON message via MQTT
        if (is_mqtt_ready) {
            msg_id = esp_mqtt_client_publish(client, topic_buffer, json_buffer, json_len, 0, 0);
            ESP_LOGI(GATTS_TABLE_TAG, "%s publish successful, msg_id=%d", topic, msg_id);
        }
    }

/**
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(GATTS_TABLE_TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client                        = event->client;
    int msg_id;
	address = (const char *)&gRDEeprom.SerialString;



    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_CONNECTED");

            // Subscribe to the EEEPROM Data
            snprintf(topic_buffer, sizeof(topic_buffer), "/%s/app/eeprom", address);
            msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
            ESP_LOGI(GATTS_TABLE_TAG, "sent subscribe successful, msg_id=%d", msg_id);

            // Subscribe to the EEPROM Data Request
            snprintf(topic_buffer, sizeof(topic_buffer), "/%s/app/request", address);
            msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
            ESP_LOGI(GATTS_TABLE_TAG, "sent subscribe successful, msg_id=%d", msg_id);

            is_mqtt_ready = true;
			if (( Quarke_Partition_State ) && (!Quarke_Update_task_Flag) && (!Bootloader_State_Flag) )
		    	xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2*8192, NULL, 5, &Quarke_Update_Task_xHandle);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_DISCONNECTED");

            is_mqtt_ready = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
			
			snprintf(topic_buffer, sizeof(topic_buffer), "/%s/app/eeprom", address);
            printf("compare_eeprom=%d\r\n", strncmp(event->topic, topic_buffer, event->topic_len));

			if (strncmp(event->topic, topic_buffer, event->topic_len) == 0) {
                memcpy(&gRDEeprom, event->data, sizeof(gRDEeprom));
                //ESP_LOGI(GATTS_TABLE_TAG, "Speed : %d", gRDEeprom.Set_StepMotorsFSC_CAF[3]);
				ESP_LOGI(GATTS_TABLE_TAG, "Speed : %d", gRDEeprom.sel_idxStepMotors);
				Read_Eeprom_Request_Index |= 0x800;
				Read_Eeprom_Request_Index |= 0x1000;
				Read_Eeprom_Request_Index |= 0x2000;
				Read_Eeprom_Request_Index |= 0x4000;
				Read_Eeprom_Request_Index |= 0x8000;
				ESP_LOGI(GATTS_TABLE_TAG, "Invio scrittura completata");
                //write_eeprom_data = true;
                //is_bluetooth      = false;
            }

			// Proccess EEPROM Data received from app via MQTT
            snprintf(topic_buffer, sizeof(topic_buffer), "/%s/app/request", address);
            printf("compare_request=%d\r\n", strncmp(event->topic, topic_buffer, event->topic_len));
            if (strncmp(event->topic, topic_buffer, event->topic_len) == 0) {
                printf("READ DATA EEPROM\r\n");
				if (is_mqtt_ready) {
				// Publish EEPROM Data to the App via MQTT
				snprintf(topic_buffer, sizeof(topic_buffer), "/%s/esp/eeprom", address);
				publish_debug_message((u_int8_t *)&gRDEeprom, sizeof(gRDEeprom), topic_buffer, address);
				ESP_LOGI(GATTS_TABLE_TAG, "eeprom sent publish successful");
				}	
            }

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(GATTS_TABLE_TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
			if (( Quarke_Partition_State ) && (!Quarke_Update_task_Flag) && (!Bootloader_State_Flag) )
		    	xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2*8192, NULL, 5, &Quarke_Update_Task_xHandle);
            break;
        default:
            ESP_LOGI(GATTS_TABLE_TAG, "Other event id:%d", event->event_id);
            break;
    }
}
esp_err_t nvs_read_string(const char* key, char* value, size_t max_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    size_t required_size;
    err = nvs_get_str(my_handle, key, NULL, &required_size);
    if (err == ESP_OK && required_size <= max_len) {
        err = nvs_get_str(my_handle, key, value, &required_size);
    }
    nvs_close(my_handle);
    return err;
}

/**
 * @brief Starts the MQTT client and initializes the MQTT client configuration.
 *
 * This function initializes the MQTT client configuration with the provided broker URL,
 * certificate, and authentication key. It then initializes the MQTT client and registers
 * the event handler. Finally, it starts the MQTT client.
 */
static void mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                     = BROKER_URL,
        .broker.verification.certificate        = (const char *)aws_root_ca_pem_start,
        .credentials.authentication.certificate = (const char *)certificate_pem_crt_start,
        .credentials.authentication.key         = (const char *)private_pem_key_start,
    };

    ESP_LOGI(GATTS_TABLE_TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

/**
 * @brief Task function for MQTT communication.
 *
 * This task initializes the UART, connects to the Wi-Fi network, and starts the MQTT application.
 * It then enters an infinite loop where it periodically delays for 1 second.
 *
 * @param arg Pointer to the task argument (not used in this case).
 */
void mqtt_task(void *arg) {
    // Wait for uart initialization
    vTaskDelay(1000);

    ESP_ERROR_CHECK(wifi_connect(WIFI_SSID, WIFI_PASSWORD));

    mqtt_app_start();

    while (1) {
        /*
         * Connects to a new Wi-Fi network if the SSID, password, and connection flag are all true.
         * Disconnects from the current Wi-Fi network before connecting to the new one.
         * Updates the connection status flags after the connection attempt.
         */
        if (wifi_is_ssid_send == true && wifi_is_pass_send == true && connect_to_wifi == true) {
            ESP_LOGI(GATTS_TABLE_TAG, "WIFI_SSID: %s", WIFI_SSID);
            ESP_LOGI(GATTS_TABLE_TAG, "WIFI_PASSWORD: %s", WIFI_PASSWORD);

            wifi_disconnect();                                        // Disconnect from the current Wi-Fi network
            ESP_ERROR_CHECK(wifi_connect(WIFI_SSID, WIFI_PASSWORD));  // Connect to the new Wi-Fi network

            wifi_is_ssid_send = false;
            wifi_is_pass_send = false;
            connect_to_wifi   = false;
        }

        vTaskDelay(1);
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (json_response == NULL) {
                    json_response = (char *) malloc(MAX_HTTP_OUTPUT_BUFFER);
                    total_len = 0;
                }
                if (json_response != NULL) {
                    memcpy(json_response + total_len, evt->data, evt->data_len);
                    total_len += evt->data_len;
                    json_response[total_len] = '\0';  // Null-terminate the buffer
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_OTA, "HTTP_EVENT_DISCONNECTED");
            break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG_OTA, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static esp_err_t _http1_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_ERROR");
			if ( File_opened )
				{
					File_opened = false;
					Load_Counter = 0;
					Firmware_version = 0;
					Firmware_version_server = 0;
					remove("/spiffs/firmware.bin");
				}
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_ON_CONNECTED");
			 ESP_LOGI(TAG_QRK_UPDATE, "Opening file firmware.bin");
			f = fopen ("/spiffs/firmware.bin", "r");
			if ( f == NULL) // the file does not exist create a new one 
				{
					f = fopen("/spiffs/firmware.bin", "w");
					File_opened = true;
					if (f == NULL) {
						ESP_LOGE(TAG_QRK_UPDATE, "Failed to open file for writing");
						File_opened = false;
					}
					Firmware_version = 0;
				}
			else	// the file exists we need to compare firmware version with the one on the server
				{
					File_opened = false;
					fread(Buffer_Test, 1, sizeof(Buffer_Test), f); // read the first 1200 bytes on the file to get firmware version
					Firmware_version = (Buffer_Test[1173] * 100 ) + Buffer_Test[1172];
					ESP_LOGI(TAG_QRK_UPDATE, "Firmware version stored on file = %d", Firmware_version);
					fclose (f);
					f = NULL;
					memset ( Buffer_Test, 0, sizeof ( Buffer_Test));
					Pointer = &Buffer_Test[0];
				}
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
			ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			if( Firmware_version != 0) // here we compare the firmware version
				{
					if( Load_Counter < 3 )
					{
						memcpy ( Pointer , evt->data, evt->data_len);
						Pointer = Pointer + evt->data_len;
						Load_Counter ++;
						Data_Counter = Data_Counter + evt->data_len;
					}
					if ( Load_Counter == 3) // compare firmware version from loaded file with stored file
					{
						Firmware_version_server = (Buffer_Test[1173] * 100 ) + Buffer_Test[1172];
						ESP_LOGI(TAG_QRK_UPDATE, "Firmware version stored on server = %d", Firmware_version_server);
						if ( Firmware_version_server == Firmware_version ) // same version detected no update needed
							{
								Load_Counter = 0;
								Firmware_version = 0;
								Firmware_version_server = 0;
								ESP_LOGI(TAG_QRK_UPDATE, "No update needed , keep the same firmware on Quarke Board");
							}
						else // the version is not the same update will be done
							{
								f = fopen ("/spiffs/firmware.bin", "w");
								if ( f == NULL) // the file does not exist create a new one 
									{
										ESP_LOGE(TAG_QRK_UPDATE, "Failed to open file for writing");
										File_opened = false;
										Load_Counter = 0;
										Firmware_version = 0;
										Firmware_version_server = 0;
									}
								else
									{
										ESP_LOGI(TAG_QRK_UPDATE, "Storing new firmware File on ESP32 Flash");
										File_opened = true;
										fwrite ( Buffer_Test, 1, Data_Counter, f);
										Load_Counter = 0;
										Firmware_version = 0;
										Firmware_version_server = 0;
									}	
							}

					}
				}
			else // here no file is stored so we take the one that is coming from server
				{
					if ( File_opened )
					{
						fwrite ( evt->data, 1, evt->data_len, f);
					}
				}
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_ON_FINISH");
			if ( File_opened )
				{
					File_opened = false;
					fclose (f);
					f = NULL;
					Firmware_version = 0;
					Firmware_version_server = 0;
					gpio_set_level( Wifi_Ready, 0);
					vTaskDelay(6);
					gpio_set_level( Wifi_Ready, 1);
					vTaskDelay(6);
					gpio_set_level( Wifi_Ready, 0);
					vTaskDelay(6);
					gpio_set_level( Wifi_Ready, 1);
					vTaskDelay(6);
					gpio_set_level( Wifi_Ready, 0);
					vTaskDelay(6);
					gpio_set_level( Wifi_Ready, 1);
				}
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_DISCONNECTED");
            break;
    	case HTTP_EVENT_REDIRECT:
        	ESP_LOGI(TAG_QRK_UPDATE, "HTTP_EVENT_REDIRECT");
        	break;
    }
    return ESP_OK;
}

void check_update_task(void *pvParameter) {
    ESP_LOGI(TAG_OTA, "Checking for firmware update");

    esp_http_client_config_t http_config = {
        .url = VERSION_URL,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA, "HTTP GET request succeeded");

        if (json_response != NULL) {
            ESP_LOGI(TAG_OTA, "Received JSON: %s", json_response);

            cJSON *json = cJSON_Parse(json_response);
            if (json == NULL) {
                ESP_LOGE(TAG_OTA, "Failed to parse JSON");
                free(json_response);
                json_response = NULL;
                esp_http_client_cleanup(client);
                vTaskDelete(NULL);
                return;
            }

            cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
            cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "url");
            if (cJSON_IsString(version) && (version->valuestring != NULL) &&
                cJSON_IsString(url) && (url->valuestring != NULL)) {

                ESP_LOGI(TAG_OTA, "Current version: %s", CURRENT_VERSION);
                ESP_LOGI(TAG_OTA, "Available version: %s", version->valuestring);

                if (strcmp(CURRENT_VERSION, version->valuestring) != 0) {
                    ESP_LOGI(TAG_OTA, "New firmware available, updating...");
					Ota_In_Progress = true;
					Counter_Led1 = 0;
                    esp_http_client_config_t ota_http_config = {
                        .url = url->valuestring,
						.crt_bundle_attach = esp_crt_bundle_attach,
                        .skip_cert_common_name_check = true,
                    };
                    esp_https_ota_config_t ota_config = {
                          .http_config = &ota_http_config,
                    };
                    esp_err_t ret = esp_https_ota(&ota_config);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG_OTA, "OTA Succeed, Rebooting...");
						Ota_In_Progress = false;
                        esp_restart();
                    } else {
                        ESP_LOGE(TAG_OTA, "OTA Failed...");
						Ota_In_Progress = false;
                    }
                } else {
                    ESP_LOGI(TAG_OTA, "Firmware is up to date");
					Ota_In_Progress = false;
                }
            } else {
                ESP_LOGE(TAG_OTA, "JSON format is incorrect");
				Ota_In_Progress = false;
            }
            cJSON_Delete(json);
            free(json_response);
            json_response = NULL;
        } else {
            ESP_LOGE(TAG_OTA, "Failed to receive JSON response");
			Ota_In_Progress = false;
        }
    } else {
        ESP_LOGE(TAG_OTA, "HTTP GET request failed: %s", esp_err_to_name(err));
		Ota_In_Progress = false;
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void Quarke_Update_task (void *pvParameters)
{

    ESP_LOGI(TAG_QRK_UPDATE, "Checking for Quarke firmware");

	Quarke_Update_task_Flag = true;

    esp_http_client_config_t http_config1 = {
        .url = Quarke_URL,
        .event_handler = _http1_event_handler,
		//.cert_pem = (const char *)Alsaqr_pem_start,
		//.skip_cert_common_name_check=true,
		//.cert_pem = (const char *)espressif_pem_start,
		.crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client1 = esp_http_client_init(&http_config1);
    esp_err_t err = esp_http_client_perform(client1);

	 if (err == ESP_OK) {
        ESP_LOGI(TAG_QRK_UPDATE, "HTTP GET request succeeded");
 
    } else {
        ESP_LOGE(TAG_QRK_UPDATE, "HTTP GET request failed: %s", esp_err_to_name(err));
	}

		esp_http_client_cleanup(client1);
		Quarke_Update_task_Flag = false;
    	vTaskDelete(NULL);
}

static void Led_TimerCallback (TimerHandle_t xTimer) // 1ms Timer
{
    millis_tick++;
    Counter_Led++;
	Quarke_Update_Counter++;

	if ( currentState == Bootloader_State1)
	{
		Counter_Led1++;
		if ( Counter_Led < 200 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if (( Counter_Led > 200 ) && ( Counter_Led < 1200 ))
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
			 if ( Counter_Led > 1200 )
			 	Counter_Led = 0;
		return;		
	}

	if ( Ota_In_Progress)
	{
		Counter_Led1++;
		if ( Counter_Led1 == 100 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led1 == 200 )
            {
            gpio_set_level( Wifi_Led1, 0);
            Counter_Led1 = 0;
            }
		return;	
	}

    if (( WBM_Com_State == WBM_initialize) || ( WBM_Com_State == WBM_Communicating))
    {
        if ( Counter_Led == 500 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led == 1000 )
            {
            gpio_set_level( Wifi_Led1, 0);
            Counter_Led = 0;
            }
    }
    
    if ( WBM_Com_State == WBM_Connected)
    {
        gpio_set_level( Wifi_Led1, 1);
        Counter_Led = 0;
    }
    
    if ( WBM_Com_State == WBM_Error)
    {
        if ( Counter_Led == 200 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led == 400 )
            {
            gpio_set_level( Wifi_Led1, 0);
            Counter_Led = 0;
            }
    }
}

static void Quarke_event_task(void *pvParameters)
{
    WBM_Com_State = WBM_initialize ;

    FKI_Board.Counter = 0;
	FKI_Board.Counter_1s = 0;
	FKI_Board.Counter_200ms = 0;
	FKI_Board.Counter_500ms = 0;
	FKI_Board.Pairing = false;
	FKI_Board.Pairing_Success = false;
	FKI_Board.Paired = false;

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

    gKTSData.Belimo1_State.Close = 0;
	gKTSData.Belimo1_State.Moving = 0;
	gKTSData.Belimo1_State.Not_Connected = 0;
	gKTSData.Belimo1_State.No_Fki = 0;
	gKTSData.Belimo1_State.Open = 0;
	gKTSData.Belimo1_State.Test_Finished = 0;
	gKTSData.Belimo1_State.Test_In_Progress = 0;
	gKTSData.Belimo1_State.Test_Succeded = 0;
	gKTSData.Belimo2_State.Close = 0;
	gKTSData.Belimo2_State.Moving = 0;
	gKTSData.Belimo2_State.Not_Connected = 0;
	gKTSData.Belimo2_State.No_Fki = 0;
	gKTSData.Belimo2_State.Open = 0;
	gKTSData.Belimo2_State.Test_Finished = 0;
	gKTSData.Belimo2_State.Test_In_Progress = 0;
	gKTSData.Belimo2_State.Test_Succeded = 0;
	gKTSData.Fire_State.Fire_On_Belimo1 = 0;
	gKTSData.Fire_State.Fire_On_Belimo2 = 0;

	// Debug and Pollin for BT and WiFi

	uint8_t       *debug_data;
    uint8_t       *polling_data;

	ESP_LOGI(TAG3, "Quarke Task Started");

    for (;;) {

        switch ( (uint8_t)WBM_Com_State )
		{
			case WBM_initialize:
                //gpio_set_level( Wifi_Ready, 1);
				vTaskDelay(2500);
			    WBM_Com_State = WBM_Communicating ;
			    currentState = CLKTSConnectState_Init;
                gpio_set_level( Wifi_Ready, 1);
				break;
			
			case WBM_Communicating:
				Connect_To_QRK ();
				break;
			
			case WBM_Connected:

				if ( Quarke_Update_Counter >= 86400000 ) // 24 hours to check new quarke board firmware on server
				{
					Quarke_Update_Counter = 0;
					if (( Quarke_Partition_State ) && (!Quarke_Update_task_Flag) && (!Bootloader_State_Flag))
		    				xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2*8192, NULL, 5, &Quarke_Update_Task_xHandle);
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
						WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
								snprintf(topic_buffer, sizeof(topic_buffer), "/%s/esp/polling", address);
								publish_debug_message(polling_data, polling_data_len, topic_buffer, address);
							}
							
							esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_POLLIING], polling_data_len, polling_data);
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
										snprintf(topic_buffer, sizeof(topic_buffer), "/%s/esp/debug", address);
										publish_debug_message(debug_data, debug_data_len, topic_buffer, address);
									}

									esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_DEBUG_DATA], debug_data_len, debug_data);
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
									snprintf(topic_buffer, sizeof(topic_buffer), "/%s/esp/eeprom", address);
									publish_debug_message((u_int8_t *)&gRDEeprom, sizeof(gRDEeprom), topic_buffer, address);
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
																if ( (Read_Eeprom_Request_Index & 0x400 ) == 0x400 ) // Fire
																{
																	//Com_SendRequest_WriteEeprom( Button_Fire.Start_Adress, Button_Fire.Count);
																	Eeprom_Data_received = true;
																	Read_Eeprom_Request_Index &= 0xFBFF;
																	gRDEeprom.Fire_Config = gRDEeprom.Fire_Config & 0xF7;
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
					FKI_Board.Pairing = false;
					FKI_Board.Pairing_Success = false;
					FKI_Board.Paired = false;
					FKI_Board.Error_Diconnected = false;
					FKI_Board.Error_Failed = false;
					FKI_Board.Test_In_Progress = false;
					FKI_Board.Timeout = false;
					FKI_Board.Counter_5s_Start = false;
                    gpio_set_level( Wifi_Ready, 0);
				break;
		}
        vTaskDelay(1);
    }
}

// Funzione da chiamare all'avvio per leggere i valori salvati
void read_wifi_credentials_from_nvs() {
    esp_err_t err;

    // Leggi SSID
    err = nvs_read_string("wifi_ssid", WIFI_SSID, sizeof(WIFI_SSID));
    if (err == ESP_OK) {
        ESP_LOGI(GATTS_TABLE_TAG, "SSID letto dalla NVS: %s", WIFI_SSID);
    } else {
        ESP_LOGE(GATTS_TABLE_TAG, "Errore (%s) leggendo SSID dalla NVS!", esp_err_to_name(err));
    }

    // Leggi Password
    err = nvs_read_string("wifi_pass", WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    if (err == ESP_OK) {
        ESP_LOGI(GATTS_TABLE_TAG, "Password letta dalla NVS: %s", WIFI_PASSWORD);
    } else {
        ESP_LOGE(GATTS_TABLE_TAG, "Errore (%s) leggendo Password dalla NVS!", esp_err_to_name(err));
    }
}

void write_default_nvs_values() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        err = nvs_get_str(nvs_handle, "wifi_ssid", NULL, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_set_str(nvs_handle, "wifi_ssid", "FASTWEB-DA0E83");
            nvs_set_str(nvs_handle, "wifi_pass", "TGRRFEZGZR");
            nvs_commit(nvs_handle);
        }
        nvs_close(nvs_handle);
    }
}

void app_main(void) {
    esp_err_t ret;

    ESP_LOGI(GATTS_TABLE_TAG, "[APP] Startup..");
    ESP_LOGI(GATTS_TABLE_TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    ESP_LOGI(GATTS_TABLE_TAG, "[APP] IDF version: %s", esp_get_idf_version());

    /* Initialize NVS. */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
	//write_default_nvs_values();
	read_wifi_credentials_from_nvs();
    ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG1, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };

	// Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG1, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG1, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG1, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }

	 ESP_LOGI(TAG1, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG1, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG1, "SPIFFS_check() successful");
    }

	size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG1, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
        return;
    } else {
        ESP_LOGI(TAG1, "Partition size: total: %d, used: %d", total, used);
		Quarke_Partition_State = true;
		//remove("/spiffs/firmware.bin");
		//esp_spiffs_format(conf.partition_label);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret                               = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "gatts register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "gap register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gatts_app_register(ESP_APP_ID);
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "gatts app register error, error code = %x", ret);
        return;
    }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    ret = esp_efuse_mac_get_default(base_mac_addr);

   ESP_LOGI(TAG1, "ESP32 Mac Adress : %X %X %X %X %X %X",base_mac_addr[0],base_mac_addr[1],base_mac_addr[2],base_mac_addr[3],base_mac_addr[4],base_mac_addr[5]);

   strcat ( (char*)Serial_Number , "WIFI-WBM-");
   sprintf ( (char*)buffer, "%02X%02X%02X%02X%02X%02X", base_mac_addr[0], base_mac_addr[1], base_mac_addr[2], base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);
   strcat ( (char*)Serial_Number , (char*)buffer);
   Serial_Number_Size = strlen ( Serial_Number );
   ESP_LOGI(TAG1, "Board serial Number : %s", (char*)Serial_Number );

   ESP_ERROR_CHECK(initialize_gpio());

   xTimer_Led = xTimerCreate("Led", 1 , pdTRUE,( void * ) 0, Led_TimerCallback );
   xTimerStart( xTimer_Led, 0 );

   vTaskDelay(100 );

   xTaskCreate(mqtt_task, "mqtt_task", MQTT_TASK_STACK_SIZE, NULL, 10, NULL);

   ESP_ERROR_CHECK (Uart1_Initialize ( ));

   vTaskDelay(100 );

   //Create a task to handle Data comming from Quarke Board
   xTaskCreate(Quarke_event_task, "Quarke_event_task", 2*2048, NULL, 2, &Quarke_Task_xHandle);
}

uint32_t Millis ( void )
{
	return millis_tick;
}

void Connect_To_QRK ( void )
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
			}
			currentState = CLKTSConnectState_TrySerialLink;
			break;
		case CLKTSConnectState_TrySerialLink: // wait response from Quarke Board
			
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					if ( retriesCounter == 2 )
					{
							WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
							retriesCounter = 0;
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
		    Com_SendRequest_ReadEeprom1( EEepromSection_Info );
		    currentState = CLKTSConnectState_ReadEeprom_Info;
		    globalTimeoutMilliseconds = Millis ();
            ESP_LOGI(TAG1, "Read Eeprom Request" );
			break;
		case CLKTSConnectState_ReadEeprom_Info:
			if (((Millis() - globalTimeoutMilliseconds) >= TimeoutMilliseconds) || (Millis() < globalTimeoutMilliseconds))
				{
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
					WBM_Com_State = WBM_Error ; // no connection with Quarke board after Timeout
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
			WBM_Polling_Base_Data_Parse ( buff_ser1 );
            ESP_LOGI(TAG1, "WBM connected to Quarke" );
			esp_ble_gatts_set_attr_value(ble_handle_table[IDX_CHAR_VAL_EEPROM_DATA], sizeof(gRDEeprom), (u_int8_t *)&gRDEeprom);
			if (is_mqtt_ready) {
			// Publish EEPROM Data to the App via MQTT
			snprintf(topic_buffer, sizeof(topic_buffer), "/%s/esp/eeprom", address);
			publish_debug_message((u_int8_t *)&gRDEeprom, sizeof(gRDEeprom), topic_buffer, address);
			}	
			break;

		case Bootloader_State:
			if ( !Quarke_Update_task_Flag ) // check if we are not in quarke firmware update task
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
						Uart_Write ( (char*)Send_Buffer,  6);
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
							Uart_Write ( Send_Buffer,  6);
							currentState = Bootloader_State1;
						}
						else
						{
							Filesize = 0; // file is empty , send 0 to bootloader
							fclose(f);
							// send file size to bootloader
							memset ( Send_Buffer, 0, sizeof(Send_Buffer));
							Send_Buffer[0] = ((Filesize >> 20) & 0xF) + 48;
							Send_Buffer[1] = ((Filesize >> 16) & 0xF) + 48;
							Send_Buffer[2] = ((Filesize >> 12) & 0xF) + 48;
							Send_Buffer[3] = ((Filesize >> 8) & 0xF) + 48;
							Send_Buffer[4] = ((Filesize & 0xF0) >> 4) + 48;
							Send_Buffer[5] = (Filesize & 0xF) + 48;
							Uart_Write ( Send_Buffer,  6);
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
	
	gKTSData.Belimo1_State.Not_Connected	= rxBuffer[ IRSP_BELIMO1_STATE ] & 0x1;
	gKTSData.Belimo1_State.Close	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x2) >> 1;
	gKTSData.Belimo1_State.Moving	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x4) >> 2;
	gKTSData.Belimo1_State.Open	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x8) >> 3;
	gKTSData.Belimo1_State.No_Fki	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x10) >> 4;
	gKTSData.Belimo1_State.Test_In_Progress	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x20) >> 5;
	gKTSData.Belimo1_State.Test_Finished	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x40) >> 6;
	gKTSData.Belimo1_State.Test_Succeded	= (rxBuffer[ IRSP_BELIMO1_STATE ] & 0x80) >> 7;
	
	gKTSData.Belimo2_State.Not_Connected	= rxBuffer[ IRSP_BELIMO2_STATE ] & 0x1;
	gKTSData.Belimo2_State.Close	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x2) >> 1;
	gKTSData.Belimo2_State.Moving	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x4) >> 2;
	gKTSData.Belimo2_State.Open	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x8) >> 3;
	gKTSData.Belimo2_State.No_Fki	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x10) >> 4;
	gKTSData.Belimo2_State.Test_In_Progress	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x20) >> 5;
	gKTSData.Belimo2_State.Test_Finished	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x40) >> 6;
	gKTSData.Belimo2_State.Test_Succeded	= (rxBuffer[ IRSP_BELIMO2_STATE ] & 0x80) >> 7;
	
	gKTSData.Fire_State.Fire_On_Belimo1	  = rxBuffer[ IRSP_FIRE_STATE ] & 0x01;
	gKTSData.Fire_State.Fire_On_Belimo2	  = ( rxBuffer[ IRSP_FIRE_STATE ] & 0x02 ) >> 1;
	
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
		ESP_LOGI(TAG1, "Eeprom wrote successfully" );
	}
	else
	{
		ESP_LOGI(TAG1, "Eeprom writing error" );
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
