#include "Utils.h"

uint8_t byteArrayToHexString(const uint8_t* bytes, uint8_t len, char* output) {
    const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B','C','D','E','F'};
    uint8_t pos = 0;
    for (uint8_t i = 0; i < len; i++) {
        output[pos++] = hex[(bytes[i] & 0xF0) >> 4];
        output[pos++] = hex[(bytes[i] & 0xF)];
        output[pos++] = ' ';
    }
    output[pos-1] = '\n';
    return pos;
}