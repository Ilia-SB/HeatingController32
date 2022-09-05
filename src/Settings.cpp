#include "Settings.h"

bool Settings::setHysteresis(const char* val) {
    float _hysteresis = strtof(val, nullptr);
    if (_hysteresis <= 0.0f)
        return false;
    hysteresis = _hysteresis;
    return true;
}

void Settings::getHysteresisCStr(char* val) {
    dtostrf(hysteresis, 3, 1, val);
}

bool Settings::setConsumptionLimit(const char* val, uint8_t phase) {
    uint16_t _consumptionLimit = atoi(val);
    if (_consumptionLimit <= 0)
        return false;

    consumptionLimit[phase] = _consumptionLimit;
    return true;
}

void Settings::getConsumptionLimitCStr(char* val, uint8_t phase) {
    itoa(consumptionLimit[phase], val, 10);
}