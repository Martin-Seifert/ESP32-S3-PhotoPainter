#ifndef WIFI_H
#define WIFI_H


#include <cJSON.h>
#include <string>
#include "esp_err.h"

esp_err_t InitWifi();
cJSON* DownloadJSON(std::string url);
void StopWifi(); 

#endif