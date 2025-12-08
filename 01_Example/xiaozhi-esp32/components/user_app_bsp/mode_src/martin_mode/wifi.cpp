#include <vector>
#include <cJSON.h>
#include <string.h>
#include <cstring> 

#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_http_client.h"

#include "ssid_manager.h"

// Event handler for Wi-Fi
static char *output_buffer;  // Buffer to store response
static int output_len;       // Current length of data
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define MAX_HTTP_OUTPUT_BUFFER 2048*16

static const char *TAG = "WIFI";

cJSON* json;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
    }
}

bool time_synced;
void time_sync_callback(struct timeval *tv) {
    time_synced = true;  // Set the flag to true once the time is synchronized
}

void initialize_sntp(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_callback);
    esp_sntp_init();

    while (!time_synced) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Sleep for 100ms before checking again
    }

    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);  // Set timezone to UTC (change as needed)
    tzset();
}

// Connect to Wi-Fi
void InitWifi(void) {
    
    SsidManager& ssidManager = SsidManager::GetInstance();
    std::vector<SsidItem> ssids = ssidManager.GetSsidList();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    std::string ssid = ssids[0].ssid;                                        
    std::string password = ssids[0].password;
    wifi_config_t wifi_config = {
        .sta = {
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        },
    };

    // Ensure SSID is copied correctly (max length is WIFI_SSID_MAX_LEN)
    std::strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), WIFI_SSID_MAX_LEN);
    wifi_config.sta.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';  // Null-terminate in case the SSID is too long

    // Ensure password is copied correctly (max length is WIFI_PASS_MAX_LEN)
    std::strncpy((char*)wifi_config.sta.password, password.c_str(), WIFI_PASS_MAX_LEN);
    wifi_config.sta.password[WIFI_PASS_MAX_LEN - 1] = '\0';  // Null-terminate in case the password is too long


    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // Wait for Wi-Fi connection
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    initialize_sntp();
    
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (esp_http_client_is_chunked_response(evt->client)) {
                // Allocate buffer if not already done
                if (output_buffer == NULL) {
                    output_buffer = (char*)malloc(MAX_HTTP_OUTPUT_BUFFER);
                    output_len = 0;
                }
                // Copy chunk into buffer
                if (evt->data_len + output_len < MAX_HTTP_OUTPUT_BUFFER) {
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                }
            } else {
                printf("%.*s", evt->data_len, (char*)evt->data);
            
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL) {
                output_buffer[output_len] = '\0';  // Null-terminate
                
                if(json != NULL){
                    cJSON_Delete(json);
                    json = NULL;
                }

                // Parse JSON
                json = cJSON_Parse(output_buffer);
                if (json == NULL) {
                    printf("JSON Parse Error!\n");
                }

                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;

        default:
            break;
    }
    return ESP_OK;
}

// Download file from URL
cJSON* DownloadJSON(std::string url){
  
    printf("Downloading %s\n",url.c_str());
    
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .event_handler = _http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .buffer_size = 1024*1024,
        .crt_bundle_attach = esp_crt_bundle_attach,        
        
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        
        int status = esp_http_client_get_status_code(client);
        int length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d, Response Length = %d",
                 status, length);


    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    return json;
}




void StopWifi(){
    esp_wifi_stop();
}