
#include "wifi.h"
#include "private.h"
#include <unordered_map>
#include <string>
#include <format>
#include "GUI_Paint.h"
#include "encoding.h"
#include "epaper_port.h"
#include <ostream>
#include <iostream>
#include <iomanip> 
#include "calendar.h"

static cJSON* root;

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

    root = DownloadJSON(std::format("https://api.open-meteo.com/v1/forecast?latitude={}&longitude={}&models=icon_seamless&hourly=precipitation,rain,showers,temperature_2m,relative_humidity_2m,weather_code&timezone=auto&forecast_days=2", lat, lon));
    
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
                if(first && isCurrentTimeHigherThan(datetime)){
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