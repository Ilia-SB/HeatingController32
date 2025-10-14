#ifndef _SETTINGS_h
#define _SETTINGS_h

#include <Arduino.h>
#include "Config.h"

#define SETTINGS_SETTINGS_VERSION   "settingsVersion"
#define SETTINGS_HYSTERESIS         "hysteresis"
#define SETTINGS_CONSUMPTION_LIMIT  "consumptionLimit"
#define SETTINGS_MQTT_URL           "mqttUrl"
#define SETTINGS_MQTT_PORT          "mqttPort"
#define SETTINGS_DEBUG_SERIAL       "debugSerial"
#define SETTINGS_NTP_SERVER         "ntpServer"
#define SETTINGS_GMT_OFFSET         "gmtOffsetHours"
#define SETTINGS_DAYLIGHT_OFFSET    "daylightOffsetHours"

class Settings
{
//variables
public:
    //uint8_t phases[NUMBER_OF_PHASES];
    uint16_t consumptionLimit[NUMBER_OF_PHASES];
    float hysteresis;
    String mqttUrl;
    uint16_t mqttPort;
    bool debugSerial;
    String ntpServer;
    int gmtOffsetHours;
    int daylightOffsetHours;
//functions
public:
    bool setHysteresis(const char* val);
    void getHysteresisCStr(char* val);
    bool setConsumptionLimit(const char* val, uint8_t phase);
    void getConsumptionLimitCStr(char* val, uint8_t phase);
};
#endif