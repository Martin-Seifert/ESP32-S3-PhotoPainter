#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#include "system_info.h"

#include "user_app.h"

#define TAG "main"

extern "C" void app_main(void) {

    sleep(2); //add to debug reset reason

    esp_reset_reason_t reset_reason = esp_reset_reason();

    switch (reset_reason) {
         case ESP_RST_UNKNOWN:
            ESP_LOGI("RESET", "Reset reason cannot be determined");
            break;
        case ESP_RST_POWERON:
            ESP_LOGI("RESET", "Reset due to power-on event");
            break;
        case ESP_RST_EXT:
            ESP_LOGI("RESET", "Reset by external pin (not applicable for ESP32)");
            break;
        case ESP_RST_SW:
            ESP_LOGI("RESET", "Software reset via esp_restart");
            break;
        case ESP_RST_PANIC:
            ESP_LOGI("RESET", "Software reset due to exception/panic");
            break;
        case ESP_RST_INT_WDT:
            ESP_LOGI("RESET", "Reset (software or hardware) due to interrupt watchdog");
            break;
        case ESP_RST_TASK_WDT:
            ESP_LOGI("RESET", "Reset due to task watchdog");
            break;
        case ESP_RST_WDT:
            ESP_LOGI("RESET", "Reset due to other watchdogs");
            break;
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI("RESET", "Reset after exiting deep sleep mode");
            break;
        case ESP_RST_BROWNOUT:
            ESP_LOGI("RESET", "Brownout reset (software or hardware)");
            break;
        case ESP_RST_SDIO:
            ESP_LOGI("RESET", "Reset over SDIO");
            break;
        case ESP_RST_USB:
            ESP_LOGI("RESET", "Reset by USB peripheral");
            break;
        case ESP_RST_JTAG:
            ESP_LOGI("RESET", "Reset by JTAG");
            break;
        case ESP_RST_EFUSE:
            ESP_LOGI("RESET", "Reset due to efuse error");
            break;
        case ESP_RST_PWR_GLITCH:
            ESP_LOGI("RESET", "Reset due to power glitch detected");
            break;
        case ESP_RST_CPU_LOCKUP:
            ESP_LOGI("RESET", "Reset due to CPU lock up (double exception)");
            break;
        default:
            ESP_LOGI("RESET", "Unknown reset reason");
            break;
    }

    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_handle_t my_handle;
    ret = nvs_open("PhotoPainter", NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(ret);
    uint8_t read_value = 0;
    ret                = nvs_get_u8(my_handle, "PhotPainterMode", &read_value);
    if (ret != ESP_OK) {
        ret = nvs_set_u8(my_handle, "PhotPainterMode", 0x03);
        ESP_ERROR_CHECK(ret);
        nvs_commit(my_handle); //Submit the revisions
        ret = nvs_get_u8(my_handle, "PhotPainterMode", &read_value);
    }
    uint8_t Mode_value;
    ret = nvs_get_u8(my_handle, "Mode_Flag", &Mode_value);
    if (ret != ESP_OK) {
        ret = nvs_set_u8(my_handle, "Mode_Flag", 0x01);
        ESP_ERROR_CHECK(ret);
        nvs_commit(my_handle); //Submit the revisions
        ret = nvs_get_u8(my_handle, "Mode_Flag", &Mode_value);
    }
    nvs_close(my_handle); //Close handle
    ESP_LOGI("Mode_value", "%d", Mode_value);
    /*Button Press Task Creation*/
    if (User_Mode_init() == 0) {
        ESP_LOGE("init", "init Failure");
        return;
    }

    if (read_value == 0x03) {
        printf("Enter xiaozhi mode\n");
        //Launch the application
        //auto &app = Application::GetInstance();
        //app.Start();
        User_Martin_mode_app_init();
    } else if (read_value == 0x01) {
        printf("Enter Basic mode\n");
        User_Basic_mode_app_init();
    } else if (read_value == 0x02) {
        printf("Enter Network mode\n");
        User_Network_mode_app_init();
    } else if (read_value == 0x04) {
        printf("Enter Mode Selection\n");
        Mode_Selection_Init();
    }
}
