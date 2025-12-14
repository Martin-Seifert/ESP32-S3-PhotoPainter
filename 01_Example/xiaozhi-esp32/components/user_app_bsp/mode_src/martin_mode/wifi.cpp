#include <vector>
#include <cJSON.h>
#include <string.h>
#include <cstring> 
#include <iostream>

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
bool connected = false;
bool tryReconnect = true;

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

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if(tryReconnect){
            ESP_LOGI(TAG, "Wi-Fi disconnected. Trying reconnect...");
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "Wi-Fi disconnected");
        }
        connected = false;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED){
        connected = true;
        initialize_sntp();
    }
}



// Connect to Wi-Fi
esp_err_t InitWifi(void) {
    
    
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL));

    // Start scanning for Wi-Fi networks
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    SsidManager& ssidManager = SsidManager::GetInstance();
    std::vector<SsidItem> ssids = ssidManager.GetSsidList();

    uint16_t num_ap = 0;
    wifi_ap_record_t* ap_info;
    
    int tries = 0;
    while(num_ap == 0 && tries < 10){
        
        ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));   
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&num_ap));
        tries++;
    }
    if(num_ap == 0) {
        
        ESP_LOGE(TAG, "No suitable Wi-Fi network found.");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    ap_info = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * num_ap);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_ap, ap_info));

    // Find the network with the highest RSSI (strongest signal)
    int best_rssi = -128;  // RSSI range is -128 to 0
    std::string best_ssid;
    std::string best_password;

    ESP_LOGI(TAG, "Found %d wifis", num_ap);
    for (int i = 0; i < num_ap; i++) {

        if (ap_info[i].rssi > best_rssi) {
            best_rssi = ap_info[i].rssi;

            // Find the SSID and password of the strongest network
            for (const auto& ssid_item : ssids) {
                if (ssid_item.ssid == std::string(reinterpret_cast<char*>(ap_info[i].ssid))) {
                    best_ssid = ssid_item.ssid;
                    best_password = ssid_item.password;
                    break;
                }
            }
        }
    }

    // Clean up
    free(ap_info);

    // Set up Wi-Fi configuration
    wifi_config_t wifi_config = {
        .sta = {
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        },
    };

    // Copy the best SSID and password
    std::strncpy((char*)wifi_config.sta.ssid, best_ssid.c_str(), WIFI_SSID_MAX_LEN);
    wifi_config.sta.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';  // Null-terminate if too long
    std::strncpy((char*)wifi_config.sta.password, best_password.c_str(), WIFI_PASS_MAX_LEN);
    wifi_config.sta.password[WIFI_PASS_MAX_LEN - 1] = '\0';  // Null-terminate if too long

    // Connect to the strongest Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI("wifi", "Connecting to the best Wi-Fi: %s", best_ssid.c_str());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return ESP_OK;

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
  
    while(!connected){
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "Downloading %s", url.c_str());
    
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
    tryReconnect = false;
    esp_wifi_stop();
}