#include <chrono>
#include <cJSON.h>
#include <cstdio>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "axp_prot.h"
#include "button_bsp.h"
#include "driver/rtc_io.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "led_bsp.h"
#include "mbedtls/debug.h"

#include "calendar.h"
#include "private.h"
#include "user_app.h"


#define ext_wakeup_pin_1 GPIO_NUM_0 
#define ext_wakeup_pin_2 GPIO_NUM_5 
#define ext_wakeup_pin_3 GPIO_NUM_4 

#define MAX_HTTP_OUTPUT_BUFFER 2048*16

static char *output_buffer;  // Buffer to store response
static int output_len;       // Current length of data

static const char *TAG = "MartinMode";

static uint8_t *epd_blackImage = NULL; 
static uint32_t Imagesize;             


static RTC_DATA_ATTR unsigned long long basic_rtc_set_time = 60*60*1000*1000;// User sets the wake-up time in microseconds. // The default is 60 seconds. It is awakened by a timer.

static uint8_t           Basic_sleep_arg = 0; // Parameters for low-power tasks
static SemaphoreHandle_t sleep_Semp;          // Binary call low-power task
 
static uint8_t           wakeup_basic_flag = 0;


cJSON* root;

// Event handler for Wi-Fi
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



static void get_wakeup_gpio(void) {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch (wakeup_reason) {
         case ESP_SLEEP_WAKEUP_UNDEFINED:
            ESP_LOGI("WAKEUP", "In case of deep sleep, reset was not caused by exit from deep sleep");
            break;
        case ESP_SLEEP_WAKEUP_ALL:
            ESP_LOGI("WAKEUP", "Not a wakeup cause, used to disable all wakeup sources with esp_sleep_disable_wakeup_source");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI("WAKEUP", "Wakeup caused by external signal using RTC_IO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGI("WAKEUP", "Wakeup caused by external signal using RTC_CNTL");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI("WAKEUP", "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            ESP_LOGI("WAKEUP", "Wakeup caused by touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            ESP_LOGI("WAKEUP", "Wakeup caused by ULP program");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            ESP_LOGI("WAKEUP", "Wakeup caused by GPIO (light sleep only on ESP32, S2, and S3)");
            break;
        case ESP_SLEEP_WAKEUP_UART:
            ESP_LOGI("WAKEUP", "Wakeup caused by UART (light sleep only)");
            break;
        case ESP_SLEEP_WAKEUP_WIFI:
            ESP_LOGI("WAKEUP", "Wakeup caused by WIFI (light sleep only)");
            break;
        case ESP_SLEEP_WAKEUP_COCPU:
            ESP_LOGI("WAKEUP", "Wakeup caused by COCPU interrupt");
            break;
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
            ESP_LOGI("WAKEUP", "Wakeup caused by COCPU crash");
            break;
        case ESP_SLEEP_WAKEUP_BT:
            ESP_LOGI("WAKEUP", "Wakeup caused by Bluetooth (light sleep only)");
            break;
        case ESP_SLEEP_WAKEUP_VAD:
            ESP_LOGI("WAKEUP", "Wakeup caused by VAD (Voice Activity Detection)");
            break;
        case ESP_SLEEP_WAKEUP_VBAT_UNDER_VOLT:
            ESP_LOGI("WAKEUP", "Wakeup caused by VDD_BAT under voltage");
            break;
        default:
            ESP_LOGI("WAKEUP", "Unknown wakeup cause");
            break;
    }
    
    
    
    if (ESP_SLEEP_WAKEUP_EXT1 == wakeup_reason) {
        uint64_t wakeup_pins = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pins == 0)
            return;
        if (wakeup_pins & (1ULL << ext_wakeup_pin_1)) {
            // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
            // //Disable the previous timer first
            // esp_sleep_enable_timer_wakeup(basic_rtc_set_time);
            // //Reset the 10-second timer
            xEventGroupSetBits(boot_groups, set_bit_button(0)); 
        } else if (wakeup_pins & (1ULL << ext_wakeup_pin_3)) {
            return;
        }
    } else if (ESP_SLEEP_WAKEUP_TIMER == wakeup_reason) {
        xEventGroupSetBits(boot_groups, set_bit_button(0)); 
    }
}


static void default_sleep_user_Task(void *arg) {
    uint8_t *sleep_arg = (uint8_t *) arg;
    for (;;) {
        if (pdTRUE == xSemaphoreTake(sleep_Semp, portMAX_DELAY)) {
            if (*sleep_arg == 1) {
                esp_sleep_pd_config(
                    ESP_PD_DOMAIN_MAX,
                    ESP_PD_OPTION_AUTO);   
                esp_sleep_disable_wakeup_source(
                    ESP_SLEEP_WAKEUP_ALL); 
                const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;
                const uint64_t ext_wakeup_pin_3_mask = 1ULL << ext_wakeup_pin_3;
                ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
                    ext_wakeup_pin_1_mask | ext_wakeup_pin_3_mask,
                    ESP_EXT1_WAKEUP_ANY_LOW)); 
                ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ext_wakeup_pin_3));
                ESP_ERROR_CHECK(rtc_gpio_pullup_en(ext_wakeup_pin_3));
                esp_sleep_enable_timer_wakeup(basic_rtc_set_time);
                //axp_basic_sleep_start(); 
                ESP_LOGI(TAG, "Starting deep sleep (sleep task)");
                vTaskDelay(pdMS_TO_TICKS(500));
                
    
                esp_deep_sleep_start();  
            }
        }
    }
}


std::string getDateString(int offsetDays = 0) {
    using namespace std::chrono;
    auto today = system_clock::now() + hours(24 * offsetDays);
    std::time_t tt = system_clock::to_time_t(today);
    std::tm tm = *std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

// Connect to Wi-Fi
void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi_init_sta finished.");
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
                
                if(root != NULL){
                    cJSON_Delete(root);
                    root = NULL;
                }

                // Parse JSON
                root = cJSON_Parse(output_buffer);
                if (root == NULL) {
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
void DownloadJSON(std::string url){
  
    root = NULL;
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
}

void DownloadAbfall(void) {
    
    
    std::string fromDate = getDateString(0);
    std::string toDate   = getDateString(30);

    std::string url = "https://aht1gh-api.sqronline.de/api/modules/abfall/webshow?"\
                    "module_division_uuid=fde08d95-111b-11ef-bbd4-b2fd53c2005a"\
                    "&cityIds=" + selectedCityId +
                    "&areaIds=" + selectedAreaId +
                    "&fromDate=" + fromDate +
                    "&toDate=" + toDate;
    
    DownloadJSON(url);
}

void ClearImage(void){
    Imagesize       = ((EXAMPLE_LCD_WIDTH % 2 == 0) ? (EXAMPLE_LCD_WIDTH / 2) : (EXAMPLE_LCD_WIDTH / 2 + 1)) * EXAMPLE_LCD_HEIGHT;
    epd_blackImage  = (uint8_t *) heap_caps_malloc(Imagesize * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(epd_blackImage);

    Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 270, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(epd_blackImage); 
    Paint_Clear(EPD_7IN3E_WHITE);   
}

int GetClosestColor(std::string hex){

    int r, g, b;
    // Skip '#' and read two hex digits at a time
    sscanf(hex.c_str(), "%2x%2x%2x", &r, &g, &b);

    if(r < 127){
        if(g < 127){
            if(b < 127){
                return EPD_7IN3E_BLACK;
            } else {
                return EPD_7IN3E_BLUE;
            }
        } else {
            if(b < 127){
                return EPD_7IN3E_GREEN;
            } else {
                if(b > g){
                    return EPD_7IN3E_BLUE;
                } else {
                    return EPD_7IN3E_GREEN;
                }
            }
        }
    } else {
        if(g < 127){
            if(b < 127){
                return EPD_7IN3E_RED;
            } else {
                if(b > r){
                    return EPD_7IN3E_BLUE;
                } else {
                    return EPD_7IN3E_RED;
                }
            }
        } else {
            if(b < 127){
                return EPD_7IN3E_YELLOW;
            } else {
                return EPD_7IN3E_WHITE;
            }
        }
    }
}


void DrawAbfall(EventCalendar* calendar){

    if(root != NULL){
       
        ESP_LOGI(TAG, "Processing abfall data");
        
        cJSON* mdiv = cJSON_GetObjectItem(root, "mdiv");
        cJSON* config = cJSON_GetObjectItem(mdiv, "config");
        cJSON* abfall_types = cJSON_GetObjectItem(config, "abfall_types");

        cJSON *entries = cJSON_GetObjectItem(root, "abfall_dates");
        cJSON *entry = NULL;

        uint16_t y = 10 - 30;
        char* lastDate = 0;
        int xOffset = 0;
        std::map dates = std::map<std::string,std::string>();

        cJSON_ArrayForEach(entry, entries) {

            std::string date (cJSON_GetObjectItem(entry, "date")->valuestring);
            //char date[11];
            //sprintf(date, "%.2s.%.2s.%.4s", rawdate + 8, rawdate + 5, rawdate);
            

            int type = cJSON_GetObjectItem(entry, "abfall_type_id")->valueint;
            cJSON* normal = cJSON_GetObjectItem(abfall_types, "normal");
            cJSON* special = cJSON_GetObjectItem(abfall_types, "special");

            std::string name;
            std::string color;
            cJSON* abfall_type = NULL;
            cJSON_ArrayForEach(abfall_type, normal) {
                
                char* id = cJSON_GetObjectItem(abfall_type, "id")->valuestring;
                if(type == std::atoi(id)){
                    name = std::string(cJSON_GetObjectItem(abfall_type, "name")->valuestring);
                    color = std::string(cJSON_GetObjectItem(abfall_type, "color")->valuestring);
                }
        
            }

            cJSON_ArrayForEach(abfall_type, special) {

                char* id = cJSON_GetObjectItem(abfall_type, "id")->valuestring;
                if(type == std::atoi(id)){
                    name = std::string(cJSON_GetObjectItem(abfall_type, "name")->valuestring);
                    color = std::string(cJSON_GetObjectItem(abfall_type, "color")->valuestring);
                }

            }

            if(name.find("Wertstoffhof") == 0) continue;
            if(name.find("Bio") == 0) continue;
            std::cout << date << " " << name << std::endl;
            
            
            if(!name.empty()){


                
                //int bgColor = GetClosestColor(color);
                //int fgColor = (bgColor == EPD_7IN3E_WHITE || bgColor == EPD_7IN3E_YELLOW) ? EPD_7IN3E_BLACK : EPD_7IN3E_WHITE;
                
                
                dates[date] += color + name.substr(0,3);
                // if(lastDate == NULL || strcmp(lastDate, date) != 0){
                //     lastDate = date;
                //     y+=30;
                //     Paint_DrawString_EN(10, y, date, &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
                //     xOffset = 0;
                    
                // }
                
                // name[1] = 0;
                // Paint_DrawString_EN(230+xOffset, y, name, &Font24, bgColor, fgColor);
                // xOffset+= 30;
                
            }
        }

        for (const auto& pair : dates) {
            
            Date date;
            date.FromYYYY_MM_DD(pair.first);
            calendar->addEvent(date, pair.second);
            // pair.first is the key
            // pair.second is the value
        }
        
    } else {

        ESP_LOGI(TAG, "Received no abfall data");
        
    }

    
               
}


static const std::unordered_map<int, std::string> wmoPhenomenonLookup = {
    {0, "Klarer Himmel"},
    {1, "Hauptsächlich klar"},
    {2, "Teilweise bewölkt"},
    {3, "Bedeckt"},
    {45, "Nebel"},
    {48, "Reifnebel"},
    {51, "Leichter Nieselregen"},
    {53, "Mäßiger Nieselregen"},
    {55, "Dichter Nieselregen"},
    {56, "Gefrier-Nieselregen"},
    {57, "Gefrier-Nieselregen+"},
    {61, "Leichter Regen"},
    {63, "Mäßiger Regen"},
    {65, "Starker Regen"},
    {66, "Leichter Gefrieregen"},
    {67, "Starker Gefrierregen"},
    {71, "Leichter Schneefall"},
    {73, "Mäßiger Schneefall"},
    {75, "Starker Schneefall"},
    {77, "Schneekörner"},
    {80, "Leichte Regenschauer"},
    {81, "Mäßige Regenschauer"},
    {82, "Starke Regenschauer"},
    {85, "Leichter Schneeregen"},
    {86, "Starker Schneeregen"},
    {95, "Gewitter"},
    {96, "Leichter Hagel"},
    {99, "Starker Hagel"}
};

std::string WMOLookup(int code){

    if (wmoPhenomenonLookup.find(code) != wmoPhenomenonLookup.end()) {
        return wmoPhenomenonLookup.at(code);
    } else {
        return "Unbekannt";
    }


}


void DownloadWeather(){

    DownloadJSON(std::format("https://api.open-meteo.com/v1/forecast?latitude={}&longitude={}&models=icon_seamless&hourly=precipitation,rain,showers,temperature_2m,relative_humidity_2m,weather_code&timezone=auto&forecast_days=2", lat, lon));
    
}

bool isCurrentTimeHigherThan(const std::string targetTime) {
    // Get current time
    time_t now = time(0);
    struct tm currentTime;
    localtime_r(&now, &currentTime);
    
    // Extract current time in hours and minutes
    int currentHour = currentTime.tm_hour;
    int currentMinute = currentTime.tm_min;
    
    // Parse the target time (24:00)
    int targetHour, targetMinute;
    char colon;
    std::stringstream(targetTime) >> targetHour >> colon >> targetMinute;

    // Convert both times to minutes for easier comparison
    int currentTimeInMinutes = currentHour * 60 + currentMinute;
    int targetTimeInMinutes = targetHour * 60 + targetMinute;
    
    std::cout << currentTimeInMinutes << " " << targetTimeInMinutes << std::endl;

    return currentTimeInMinutes > targetTimeInMinutes;
}

void DrawWeather(float xOffset, float yOffset) {

    if(root != NULL){
        
        cJSON* current = cJSON_GetObjectItem(root, "current");
        if(current){
            
            double temp = cJSON_GetObjectItem(current, "temperature_2m")->valuedouble;
            double humidity = cJSON_GetObjectItem(current, "relative_humidity_2m")->valueint;
            int code = cJSON_GetObjectItem(current, "weather_code")->valueint;

            std::string str1 = (std::ostringstream() << std::fixed << std::setprecision(0) << temp << "°C " << humidity << "%").str();
            std::string str2 = (std::ostringstream() << std::fixed << std::setprecision(0) << WMOLookup(code)).str();

            Paint_DrawString_EN(xOffset, yOffset, str1.c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
            yOffset +=30;
            Paint_DrawString_EN(xOffset, yOffset, str2.c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
            yOffset +=30;
        }

        cJSON* hourly = cJSON_GetObjectItem(root, "hourly");
        if(hourly){
            
            cJSON* weather_codes = cJSON_GetObjectItem(hourly, "weather_code");
            cJSON* times = cJSON_GetObjectItem(hourly, "time");
            cJSON* entry;
            int lastCode = -1;
            int idx = 0;
            Date lastDate;
            bool first = true;
            std::string text;
            
            cJSON_ArrayForEach(entry, weather_codes){
        
                std::string datetime = std::string(cJSON_GetArrayItem(times, idx)->valuestring);
                std::string datestr = datetime.substr(0, 10);
                std::string timestr = datetime.substr(11);
                Date date;
                date.FromYYYY_MM_DD(datestr);
                std::cout << "timestr: " << timestr << std::endl;
                if(first && isCurrentTimeHigherThan(timestr)){
                    idx++;
                    continue;
                }

                int code = entry->valueint;
                if(lastCode != code){
                
                    if(yOffset > Paint.Height - 30) return;

                    if(first){
                        text = "Wetter heute";
                        if(yOffset > Paint.Height - 60) return;

                        Paint_DrawString_EN(xOffset, yOffset, utf8_to_latin1(text).c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
                        yOffset +=30;
                        lastDate = date;
                        first = false;
                    }
                    else if(date > lastDate){
                        text = "Wetter morgen";
                        if(yOffset > Paint.Height - 60) return;

                        Paint_DrawString_EN(xOffset, yOffset, utf8_to_latin1(text).c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
                        yOffset+=30;
                        lastDate = date;
                        
                    }
                    
                    
                    std::string time = datetime.substr(11);

                    text = time + " " + WMOLookup(code);
                    Paint_DrawString_EN(xOffset, yOffset, utf8_to_latin1(text).c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
                    yOffset +=30;
                }
                lastCode = code;
                idx++;
            }



        }
        


    }
}

void DrawCalendar(EventCalendar* calendar, int startX, int startY){

    std::map<Date, std::string> dates = calendar->getNextDays(30);

    sFONT font = Font24;
    int charWidth = font.Width;
    int y = startY;
    for (const auto& pair : dates) {
         
        int x = startX;
        Date date = pair.first;
        std::string datestr = date.ToDD_MM();
        Paint_DrawString_EN(x, y, datestr.c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
        x+= datestr.length() * charWidth + 10;

        std::vector<std::string> result;
        std::stringstream ss(pair.second);
        std::string token;

        // Split by '#'
        while (std::getline(ss, token, '#')) {
            if(!token.empty()){
                result.push_back(token);
            }
        }
        for (const auto& word : result) {

            std::string color = word.substr(0,6);
            std::string text = word.substr(6);
            
            int bgColor = GetClosestColor(color);
            int fgColor = (bgColor == EPD_7IN3E_WHITE || bgColor == EPD_7IN3E_YELLOW) ? EPD_7IN3E_BLACK : EPD_7IN3E_WHITE;
            
            int maxChars = (Paint.Width - x) / charWidth;

            if(maxChars <= 0) break;
            std::string l1text = utf8_to_latin1(text).substr(0, maxChars);
            Paint_DrawString_EN(x, y, l1text.c_str(), &Font24, bgColor, fgColor);
            x+= l1text.length() * charWidth + 10;

        }
        y+= font.Height + 6;
    }

}


void DrawBattery(){
    
    int battery_level = axp2101_getBattPercent();
    
    for (int i = 0; i < (int)(((float)battery_level/100.0) * Paint.Width); i++)
    {
        Paint_SetPixel(i, 0, EPD_7IN3E_GREEN);
        Paint_SetPixel(i, 1, EPD_7IN3E_GREEN);
        Paint_SetPixel(i, 2, EPD_7IN3E_GREEN);
    }
    
}

void mainRoutine(void){
    
    wifi_init_sta();

    auto calendar = new EventCalendar();

    // Wait for Wi-Fi connection
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    initialize_sntp();
    // // Download file
    
    ESP_LOGI(TAG, "Set time to:");
    auto now = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(now);
    std::cout << std::ctime(&end_time) << std::endl;
     

    ClearImage();
    DownloadAbfall();
    DrawAbfall(calendar);

    DownloadWeather();
    DrawWeather(10, 400);
    
    DownloadJSON("https://feiertage-api.de/api/?jahr=2025&nur_land=BY");
    
    if(root != NULL){
        cJSON* entry;
        cJSON_ArrayForEach(entry, root) {
            
            std::string name(entry->string);
            std::string datestr(cJSON_GetObjectItem(entry, "datum")->valuestring);
            Date date;
            date.FromYYYY_MM_DD(datestr);
            calendar->addEvent(date, "#ffffff" + name);
        }
    }

    DrawCalendar(calendar, 10, 10);

    esp_wifi_stop();
    DrawBattery();
    ESP_LOGI(TAG, "Start painting");
    epaper_port_display(epd_blackImage); 
    
}

static void pwr_button_user_Task(void *arg) {
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(pwr_groups, set_bit_all, pdTRUE,
                                               pdFALSE, pdMS_TO_TICKS(2000));
        if (get_bit_button(even, 0)) // Immediately enter low-power mode
        {
            esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);     
            const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;
            const uint64_t ext_wakeup_pin_3_mask = 1ULL << ext_wakeup_pin_3;
            ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_1_mask | ext_wakeup_pin_3_mask, ESP_EXT1_WAKEUP_ANY_LOW)); 
            ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ext_wakeup_pin_3));
            ESP_ERROR_CHECK(rtc_gpio_pullup_en(ext_wakeup_pin_3));
            esp_sleep_enable_timer_wakeup(basic_rtc_set_time);
            //axp_basic_sleep_start();
            vTaskDelay(pdMS_TO_TICKS(500));
            
            ESP_LOGI(TAG, "Starting deep sleep (power button)");
                
            esp_deep_sleep_start(); 
        }
    }
}

static void boot_button_user_Task(void *arg) {
    
    uint8_t *wakeup_arg = (uint8_t *) arg;
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(boot_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (get_bit_button(even, 0)) {
            if (*wakeup_arg == 0) {
                if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, 2000)) {                       
                    xEventGroupSetBits(Green_led_Mode_queue, set_bit_button(6));
                    Green_led_arg                   = 1;
                    mainRoutine();
                    xSemaphoreGive(epaper_gui_semapHandle); 
                    Green_led_arg = 0;
                    xSemaphoreGive(sleep_Semp); 
                    Basic_sleep_arg = 1;
                }
            }
        }
    }
}

void User_Martin_mode_app_init(void) {
    
    ESP_LOGI(TAG, "Entering Martin mode");
    
    //Initialize NVS
    sleep_Semp  = xSemaphoreCreateBinary();
    xEventGroupSetBits(Red_led_Mode_queue, set_bit_button(0));
    xTaskCreate(boot_button_user_Task, "boot_button_user_Task", 6 * 1024, &wakeup_basic_flag, 3, NULL);
    xTaskCreate(pwr_button_user_Task, "pwr_button_user_Task", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(default_sleep_user_Task, "default_sleep_user_Task", 4 * 1024, &Basic_sleep_arg, 3, NULL); 
    get_wakeup_gpio();
    return;
    
     
}
// void PrintAllData(const char *partition){
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ESP_ERROR_CHECK(nvs_flash_init());
//     }

//     nvs_iterator_t it;
//     esp_err_t res = nvs_entry_find(partition, NULL, NVS_TYPE_ANY, &it);

//     while (res == ESP_OK) {
//         nvs_entry_info_t info;
//         nvs_entry_info(it, &info);
//         ESP_LOGI(TAG, "Key: %s, Type: %d", info.key, info.type);
        
//         // Open the namespace to read the value
//         nvs_handle_t handle;
//         if (nvs_open(info.namespace_name, NVS_READONLY, &handle) == ESP_OK) {
         
//             switch (info.type) {
//                 case NVS_TYPE_I32: {
//                     int32_t val;
//                     if (nvs_get_i32(handle, info.key, &val) == ESP_OK) {
//                         ESP_LOGI(TAG, "  Value (int32): %d", val);
//                     }
//                     break;
//                 }
//                 case NVS_TYPE_STR: {
//                     size_t required_size;
//                     if (nvs_get_str(handle, info.key, NULL, &required_size) == ESP_OK) {
//                         char *buf = static_cast<char*>(malloc(required_size));
//                         if (nvs_get_str(handle, info.key, buf, &required_size) == ESP_OK) {
//                             ESP_LOGI(TAG, "  Value (string): %s", buf);
//                         }
//                         free(buf);
//                     }
//                     break;
//                 }
//                 case NVS_TYPE_BLOB: {
//                     size_t blob_size;
//                     if (nvs_get_blob(handle, info.key, NULL, &blob_size) == ESP_OK) {
//                         uint8_t *blob = static_cast<uint8_t*>(malloc(blob_size));
//                         if (nvs_get_blob(handle, info.key, blob, &blob_size) == ESP_OK) {
//                             ESP_LOGI(TAG, "  Value (blob, %d bytes)", blob_size);
//                             // You can hex‑dump blob here if needed
//                         }
//                         free(blob);
//                     }
//                     break;
//                 }
//                 default:
//                     ESP_LOGI(TAG, "  Value type not handled yet");
//                     break;
//             }
//             nvs_close(handle);
//         }

//         res = nvs_entry_next(&it);
//     }

//     nvs_release_iterator(it);
// }
