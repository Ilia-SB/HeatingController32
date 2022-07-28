#include "Utils.h"

void byteArrayToHexString(const uint8_t *bytes, const uint8_t size, String &hexString) {
    for (uint8_t i=0; i<size; i++) {
        hexString += String(bytes[i], (uint8_t)16);
        hexString += " ";
    }
    hexString.remove(size - 1);
}