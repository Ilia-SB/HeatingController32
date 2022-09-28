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

void byteArrayToHexCString(const uint8_t* bytes, const uint8_t size, char* hexCString) {
    uint8_t pos = 0;
    for (uint8_t i=0; i<size; i++) {
        if (bytes[i] < 0x10) {
            hexCString[pos++] = '0';
            itoa(bytes[i], &hexCString[pos++], 16);
        } else {
            itoa(bytes[i], &hexCString[pos], 16);
            pos += 2;
        }
        hexCString[pos++] = ' ';
    }
    hexCString[pos - 1] = '\0';
    strupr(hexCString);
}

bool hexCStringToByteArray(const char* hexCString, byte* bytes, uint8_t len) {
    const char* pos = hexCString;
    for (uint8_t retPos = 0; retPos < len; retPos++) {
        if (sscanf(pos, "%2hhx", &bytes[retPos]) == 0) {
            return false;
        }
        pos += 3;
    }
    return true;
}
