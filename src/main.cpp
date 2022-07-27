/*
    Ethernet:   http://arduino.ru/forum/apparatnye-voprosy/podklyuchenie-ethernet-lan8720-i-esp32-devkit-c-esp32-devkit-v1
                https://sautter.com/blog/ethernet-on-esp32-using-lan8720/
*/

#include <version.h>
#include <Arduino.h>
#include "Config.h"
#include "HeaterItem.h"
#include "DebugPrint.h"
#include <SPIFFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ETH.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32TimerInterrupt.h>
#include <AsyncElegantOTA.h>
#include <ArduinoJson.h>

/*
#include <MQTTClient.h>
#include <MQTT.h>
*/
boolean toggle = true;
uint16_t o = 1;

void updateOutputs(uint16_t outputs) {
    DEBUG_PRINTHEX(outputs);DEBUG_PRINTLN();
    digitalWrite(LS_STB, LOW);
    shiftOut(LS_DATA, LS_CLK, MSBFIRST, outputs >> 8);
    shiftOut(LS_DATA, LS_CLK, MSBFIRST, outputs);
    digitalWrite(LS_STB, HIGH);
}

void setPorts(boolean ports[16]) {
    uint16_t output = 0;
    for (uint8_t i=0; i<16; i++) {
        if (ports[i])
            bitSet(output, i);
    }
    updateOutputs(output);
}

void setup()
{
    pinMode(LS_DATA, OUTPUT);
    pinMode(LS_CLK, OUTPUT);
    pinMode(LS_STB, OUTPUT);
    pinMode(LS_OE, OUTPUT);

    digitalWrite(LS_OE, HIGH);

    Serial.begin(115200);
    DEBUG_PRINTLN("HeatingController32 V1.0 starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    boolean ports[] = {1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1};
    setPorts(ports);

}

// Add the main program code into the continuous loop() function
void loop()
{
    //updateOutputs(opt++);
    /*
    updateOutputs(o);
    o = o << 1;
    if (o == 0)
        o = 1;
    delay(1000);
    */
}