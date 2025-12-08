#include <chrono>
#include <cJSON.h>
#include <cstdio>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <vector>
#include <cstring> 


#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

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
#include "wifi.h"
#include "weather.h"
#include "encoding.h"

#include "wifi_configuration_ap.h"

#define ext_wakeup_pin_1 GPIO_NUM_0 
#define ext_wakeup_pin_2 GPIO_NUM_5 
#define ext_wakeup_pin_3 GPIO_NUM_4 



static const char *TAG = "MartinMode";

static uint8_t *epd_blackImage = NULL; 
static uint32_t Imagesize;             


static RTC_DATA_ATTR unsigned long long basic_rtc_set_time = 60ULL*60*1000*1000;// User sets the wake-up time in microseconds. // The default is 60 seconds. It is awakened by a timer.

static uint8_t           Basic_sleep_arg = 0; // Parameters for low-power tasks
static SemaphoreHandle_t sleep_Semp;          // Binary call low-power task
 
static uint8_t           wakeup_basic_flag = 0;


static cJSON* root = NULL;





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
            
            auto& wifi_ap = WifiConfigurationAp::GetInstance();
            wifi_ap.SetSsidPrefix("ESP32");
            wifi_ap.Start();
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
                
                if(esp_sleep_enable_timer_wakeup(basic_rtc_set_time) != ESP_OK){
                    ESP_LOGE(TAG, "Deep sleep duration out of range");
                }   
                //axp_basic_sleep_start(); 
                ESP_LOGI(TAG, "Starting deep sleep (sleep task) %u micros", (uint32_t)basic_rtc_set_time);
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




void DownloadAbfall(void) {
    
    
    std::string fromDate = getDateString(0);
    std::string toDate   = getDateString(30);

    std::string url = "https://aht1gh-api.sqronline.de/api/modules/abfall/webshow?"\
                    "module_division_uuid=fde08d95-111b-11ef-bbd4-b2fd53c2005a"\
                    "&cityIds=" + selectedCityId +
                    "&areaIds=" + selectedAreaId +
                    "&fromDate=" + fromDate +
                    "&toDate=" + toDate;
    
    root = DownloadJSON(url);
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
    
    InitWifi();

    auto calendar = new EventCalendar();

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
    
    root = DownloadJSON("https://feiertage-api.de/api/?jahr=2025&nur_land=BY");
    
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

    StopWifi();
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
            if(esp_sleep_enable_timer_wakeup(basic_rtc_set_time) != ESP_OK){
                ESP_LOGE(TAG, "Deep sleep duration out of range");
            }
            //axp_basic_sleep_start();
            vTaskDelay(pdMS_TO_TICKS(500));
            
            ESP_LOGI(TAG, "Starting deep sleep (power button) %u micros", (uint32_t)basic_rtc_set_time);
                
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