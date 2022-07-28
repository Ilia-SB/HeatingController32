#include <Utils.h>

void byteArrayToHexString(const uint8_t *bytes, const uint8_t size, String &hexString) {
    for (uint8_t i=0; i<size; i++) {
        if (bytes[i] < 0x10)
            hexString += "0";
        hexString += String(bytes[i], (uint8_t)16);
        hexString += " ";
    }
    hexString.remove(size * 3 - 1);
    hexString.toUpperCase();
}