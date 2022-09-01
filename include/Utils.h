#ifndef UTILS_H_
#define UTILS_H_

#include <Arduino.h>

void byteArrayToHexString(const uint8_t *bytes, const uint8_t size, String &hexString);
void byteArrayToHexCString(const uint8_t* bytes, const uint8_t size, char* hexCString);
bool hexCStringToByteArray(const char* hexCString, byte* bytes, uint8_t len);
#endif