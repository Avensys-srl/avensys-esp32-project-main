#include "main.h"
#include "ble_app.h"
#include "mqtt_app.h"
#include "unit_comm.h"
#include "CL_WBM.h"
#include "protocol_Serial.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_spiffs.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define MAX_HTTP_OUTPUT_BUFFER 1024
#define VERSION_URL "https://mybestdocument.altervista.org/ESP32/version.json"
#define BUFFSIZE 1024

//#define Unit_URL "https://www.alsaqr.io/.well-known/publicFIleTest/T533422LK/File1.bin"
//#define Unit_URL "https://dl.espressif.com/dl/esp-idf/ci/esp_http_client_demo.txt"
//#define Unit_URL "https://www.avensys-srl.com/ESP32/RD_ATSAM4N_Test.bin"
#define Unit_URL "https://www.avensys-srl.com/ESP32/RD_ATSAM4N_V1_0.bin"


// char WIFI_SSID[30]     = "FiberHGW_ZYF75B";
// char WIFI_PASSWORD[30] = "XNfXFYxURF9K";
char WIFI_SSID[MAX_SSID_LENGTH + 1]  = "FASTWEB-DA0E83";
char WIFI_PASSWORD[MAX_PASSWORD_LENGTH + 1] = "TGRRFEZGZR";
//char WIFI_SSID[MAX_SSID_LENGTH + 1]     = "TP-LINK_3965";
//char WIFI_PASSWORD[MAX_PASSWORD_LENGTH + 1] = "558FC4A469ZE6";

static const char *TAG_OTA =  "OTA";
static const char *CURRENT_VERSION =  "1.0.1"; //versione corrente del firmware
bool Ota_In_Progress = false;

static char *json_response = NULL;
static int total_len = 0;

bool send_eeprom_read_request = false;

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

TaskHandle_t Mqtt_Task_xHandle = NULL;
bool Unit_Partition_State = false;
TaskHandle_t Unit_Update_Task_xHandle = NULL;
static const char *TAG_UNIT_UPDATE =  "Unit Update : ";
bool File_opened = false;
FILE* f = NULL;
char Buffer_Test[1536];
int Firmware_version = 0;
int Firmware_version_server = 0;
char Load_Counter = 0;
char* Pointer = NULL;
uint16_t Data_Counter = 0;
bool Unit_Update_task_Flag = false;
volatile uint32_t Unit_Update_Counter = 0;
int Filesize = 0;
bool Bootloader_State_Flag = false;
char Send_Buffer[100];
int Bytes_Transfered = 0;
char Temp_Buffer[20];

extern byte buff_ser1[128];     // buffer di appoggio

bool No_Update = false;

uint32_t	File_Start = 0;

bool Wifi_Connected_Flag = false;

uint32_t Millis ( void );
void Unit_Update_task (void *pvParameters);

extern const uint8_t Alsaqr_pem_start[] asm("_binary_Alsaqr_pem_start");
extern const uint8_t Alsaqr_pem_end[] asm("_binary_Alsaqr_pem_end");
extern const uint8_t espressif_pem_start[] asm("_binary_espressif_pem_start");
extern const uint8_t espressif_pem_end[] asm("_binary_espressif_pem_end");

bool read_eeprom_data  = true;

extern bool Bootloader_Mode;
extern bool Ack_Received;

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

esp_err_t nvs_write_string(const char* key, const char* value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }

    nvs_close(my_handle);
    return err;
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
			if (( Unit_Partition_State ) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag) )
		    	xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2*8192, NULL, 5, &Unit_Update_Task_xHandle);
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
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_ERROR");
			No_Update = false;
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
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_ON_CONNECTED");
			 ESP_LOGI(TAG_UNIT_UPDATE, "Opening file firmware.bin");
			f = fopen ("/spiffs/firmware.bin", "r");
			if ( f == NULL) // the file does not exist create a new one 
				{
					f = fopen("/spiffs/firmware.bin", "w");
					File_opened = true;
					if (f == NULL) {
						ESP_LOGE(TAG_UNIT_UPDATE, "Failed to open file for writing");
						File_opened = false;
					}
					Firmware_version = 0;
				}
			else	// the file exists we need to compare firmware version with the one on the server
				{
					File_opened = false;
					fread(Buffer_Test, 1, sizeof(Buffer_Test), f); // read the first 1200 bytes on the file to get firmware version
					Firmware_version = (Buffer_Test[1173] * 100 ) + ((Buffer_Test[1172] / 16 ) * 10 ) + (Buffer_Test[1172] % 16 );
					ESP_LOGI(TAG_UNIT_UPDATE, "Firmware version stored on file = %d", Firmware_version);
					Filesize = 0;
					fclose (f);
					ESP_LOGI(TAG_UNIT_UPDATE, "File size = %d", Filesize);
					f = NULL;
					memset ( Buffer_Test, 0, sizeof ( Buffer_Test));
					Pointer = &Buffer_Test[0];
				}
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
			ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
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
						Firmware_version_server = (Buffer_Test[1173] * 100 ) + ((Buffer_Test[1172] / 16 ) * 10) + (Buffer_Test[1172] % 16 );
						ESP_LOGI(TAG_UNIT_UPDATE, "Firmware version stored on server = %d", Firmware_version_server);
						if ( Firmware_version_server == Firmware_version ) // same version detected no update needed
							{
								Load_Counter = 0;
								Firmware_version = 0;
								Firmware_version_server = 0;
								ESP_LOGI(TAG_UNIT_UPDATE, "No update needed , keep the same firmware on Unit Board");
								No_Update = true;
							}
						else // the version is not the same update will be done
							{
								remove("/spiffs/firmware.bin");
								f = fopen ("/spiffs/firmware.bin", "w");
								if ( f == NULL) // the file does not exist create a new one 
									{
										ESP_LOGE(TAG_UNIT_UPDATE, "Failed to open file for writing");
										File_opened = false;
										Load_Counter = 0;
										Firmware_version = 0;
										Firmware_version_server = 0;
									}
								else
									{
										ESP_LOGI(TAG_UNIT_UPDATE, "Storing new firmware File on ESP32 Flash");
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
					/*else
					{
						if ( !No_Update )
							{
								f = fopen("/spiffs/firmware.bin", "w");
								File_opened = true;
								if (f == NULL) {
									ESP_LOGE(TAG_UNIT_UPDATE, "Failed to open file for writing");
									File_opened = false;
								}
								else
									fwrite ( evt->data, 1, evt->data_len, f);
							}
					}*/
				}
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_ON_FINISH");
			No_Update = false;
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
            ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_DISCONNECTED");
            break;
    	case HTTP_EVENT_REDIRECT:
        	ESP_LOGI(TAG_UNIT_UPDATE, "HTTP_EVENT_REDIRECT");
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

static void ota_boot_task(void *pvParameter) {
    const TickType_t wait_step = pdMS_TO_TICKS(250);
    const TickType_t wait_timeout = pdMS_TO_TICKS(90000);
    TickType_t start = xTaskGetTickCount();

    while (!Wifi_Connected_Flag && (xTaskGetTickCount() - start) < wait_timeout) {
        vTaskDelay(wait_step);
    }

    if (!Wifi_Connected_Flag) {
        ESP_LOGW(TAG_OTA, "Skipping OTA check at boot: Wi-Fi not connected within timeout");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_OTA, "Boot Wi-Fi connected, starting one-shot OTA check");
    if (xTaskCreate(check_update_task, "check_update_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG_OTA, "Unable to create check_update_task");
    }

    vTaskDelete(NULL);
}

void Unit_Update_task (void *pvParameters)
{

    ESP_LOGI(TAG_UNIT_UPDATE, "Checking for Unit firmware");

	Unit_Update_task_Flag = true;
	//gpio_set_level( Wifi_Ready, 0);

    esp_http_client_config_t http_config1 = {
        .url = Unit_URL,
        .event_handler = _http1_event_handler,
		//.cert_pem = (const char *)Alsaqr_pem_start,
		//.skip_cert_common_name_check=true,
		//.cert_pem = (const char *)espressif_pem_start,
		.crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client1 = esp_http_client_init(&http_config1);
    esp_err_t err = esp_http_client_perform(client1);

	 if (err == ESP_OK) {
        ESP_LOGI(TAG_UNIT_UPDATE, "HTTP GET request succeeded");
 
    } else {
        ESP_LOGE(TAG_UNIT_UPDATE, "HTTP GET request failed: %s", esp_err_to_name(err));
	}

		esp_http_client_cleanup(client1);
		Unit_Update_task_Flag = false;
		//gpio_set_level( Wifi_Ready, 1);
    	vTaskDelete(NULL);
}

static void Led_TimerCallback (TimerHandle_t xTimer) // 1ms Timer
{
    millis_tick++;
    //Counter_Led++;
	Unit_Update_Counter++;

	if ( Unit_Update_task_Flag)
	{
		Counter_Led++;
		if ( Counter_Led <= 150 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led <= 350 )
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
				if ( Counter_Led <= 500 )
				{
					gpio_set_level( Wifi_Led1, 1);
				}
				else
					if ( Counter_Led <= 1300 )
					{
						gpio_set_level( Wifi_Led1, 0);
					}
					else
			 			Counter_Led = 0;
		return;
	}

	if ( currentState == Bootloader_State1)
	{
		Counter_Led++;
		if ( Counter_Led <= 200 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led <= 1200 )
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
			 	Counter_Led = 0;
		return;		
	}

	if ( Ota_In_Progress)
	{
		Counter_Led1++;
		if ( Counter_Led1 <= 100 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led1 <= 200 )
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
			 	 Counter_Led1 = 0;
		return;	
	}

    if (( WBM_Com_State == WBM_initialize) || ( WBM_Com_State == WBM_Communicating))
    {
		Counter_Led++;
        if ( Counter_Led <= 500 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led <= 1000 )
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
				Counter_Led = 0;
		return;		
    }
    
    if ( WBM_Com_State == WBM_Connected)
    {
        gpio_set_level( Wifi_Led1, 1);
        Counter_Led = 0;
		return;
    }
    
    if ( WBM_Com_State == WBM_Error)
    {
		Counter_Led++;
        if ( Counter_Led <= 1000 )
        {
            gpio_set_level( Wifi_Led1, 1);
        }
        else
            if ( Counter_Led <= 2000 )
            {
            	gpio_set_level( Wifi_Led1, 0);
            }
			else
				Counter_Led = 0;
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

	WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

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
		Unit_Partition_State = true;
		//remove("/spiffs/firmware.bin"); // *********************
		//esp_spiffs_format(conf.partition_label);// *********************
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ret = ble_app_init();
    if (ret != ESP_OK) {
        return;
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

	   xTaskCreate(mqtt_task, "mqtt_task", MQTT_TASK_STACK_SIZE, NULL, 10, &Mqtt_Task_xHandle);
       xTaskCreate(ota_boot_task, "ota_boot_task", 4096, NULL, 5, NULL);

	   unit_comm_start();
}

uint32_t Millis ( void )
{
	return millis_tick;
}















