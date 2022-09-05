#ifndef _SETTINGS_h
#define _SETTINGS_h

#include <Arduino.h>
#include "Config.h"

class Settings
{
//variables
public:
    //uint8_t phases[NUMBER_OF_PHASES];
    uint16_t consumptionLimit[NUMBER_OF_PHASES];
    float hysteresis;

//functions
public:
    bool setHysteresis(const char* val);
    void getHysteresisCStr(char* val);
    bool setConsumptionLimit(const char* val, uint8_t phase);
    void getConsumptionLimitCStr(char* val, uint8_t phase);
};
#endif