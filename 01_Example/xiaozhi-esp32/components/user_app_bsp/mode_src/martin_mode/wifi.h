#ifndef WIFI_H
#define WIFI_H


#include <cJSON.h>
#include <string>

void InitWifi();
cJSON* DownloadJSON(std::string url);
void StopWifi(); 

#endif