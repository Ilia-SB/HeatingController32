#ifndef UTILS_H_
#define UTILS_H_

#include <Arduino.h>

void byteArrayToHexString(const uint8_t *bytes, const uint8_t size, String &hexString);
void byteArrayToHexCString(const uint8_t* bytes, const uint8_t size, char* hexCString);
bool hexCStringToByteArray(const char* hexCString, byte* bytes, uint8_t len);
template<typename T> bool compareArrays (const T arr1[], const T arr2[], const uint8_t& len ) {
    for (uint8_t i=0; i<len; i++) {
        if (arr1[i] != arr2[i])
            return false;
    }
    return true;
}
#endif