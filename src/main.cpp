/*
    Ethernet:   http://arduino.ru/forum/apparatnye-voprosy/podklyuchenie-ethernet-lan8720-i-esp32-devkit-c-esp32-devkit-v1
                https://sautter.com/blog/ethernet-on-esp32-using-lan8720/
*/



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

/*
#include <MQTTClient.h>
#include <MQTT.h>
*/

WiFiClient ethClient;
static bool ethConnected = false;

AsyncWebServer server(80);

PubSubClient mqttClient(ethClient);


OneWire oneWire(SENSOR);
DallasTemperature sensors(&oneWire);

byte outputs = 0;

uint8_t sensorsCount;
DeviceAddress sensorAddresses[16];
float temperatures[16];

uint8_t ledMode = 0;

ESP32Timer ITimer0(0);

void ethernetLed(uint8_t mode) {
    digitalWrite(ETHERNET_LED, mode);
}

void mqttLed(uint8_t mode) {
    digitalWrite(MQTT_LED, mode);
}

void oneWireLed(uint8_t mode) {
    digitalWrite(ONEWIRE_LED, mode);
}

void updateOutputs(byte outputs) {
    digitalWrite(LS_STB, LOW);
    shiftOut(LS_DATA, LS_CLK, LSBFIRST, outputs);
    digitalWrite(LS_STB, HIGH);
}

bool mqttConnect() {
    if (!ethConnected) {
        return false;
    }
    if (mqttClient.connect(HOSTNAME)) {
        DEBUG_PRINTLN("MQTT connected");
        mqttClient.subscribe(COMMAND_TOPIC);
        return true;
    }
    else {
        DEBUG_PRINTLN("MQTT connect failed");
        return false;
    }
}

void mqttCallback(char* topic, byte* payload, const unsigned int len) {
    DEBUG_PRINTLN(topic);

    if (len < MQTT_MAX_PACKET_SIZE) {
        payload[len] = '\0';
    }
    else {
        payload[len - 1] = '\0';
    }
    String pl = String((char*)payload);
    DEBUG_PRINTLN(pl);
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case SYSTEM_EVENT_ETH_START:
        DEBUG_PRINTLN("ETH Started");
        ETH.setHostname(HOSTNAME);
        break;
    case SYSTEM_EVENT_ETH_CONNECTED:
        DEBUG_PRINTLN("ETH Connected");
        ethConnected = true;
        break;
    case SYSTEM_EVENT_ETH_GOT_IP:
        DEBUG_PRINT("ETH MAC: ");
        DEBUG_PRINT(ETH.macAddress());
        DEBUG_PRINT(", IPv4: ");
        DEBUG_PRINT(ETH.localIP());
        if (ETH.fullDuplex()) {
            DEBUG_PRINT(", FULL_DUPLEX");
        }
        DEBUG_PRINT(", ");
        DEBUG_PRINT(ETH.linkSpeed());
        DEBUG_PRINTLN("Mbps");
        ethConnected = true;

        //mqttConnect();
        break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        ethConnected = false;
        break;
    case SYSTEM_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        ethConnected = false;
        break;
    default:
        break;
    }
}

String byteArrayToHexString(const uint8_t* bytes) {
    String string = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (bytes[i] < 16) string += "0";
        string += String((unsigned int)bytes[i], (unsigned char)16U) + " ";
    }
    string.toUpperCase();
    return string;
}

String webServerPlaceholderProcessor(const String& placeholder) {
    String retValue = "";
    if (placeholder == "SENSOR_ADDR") {
        retValue += "<p>Sensors count: " + String(sensorsCount) + "</p>";
        for (uint8_t i = 0; i < sensorsCount; i++) {
            retValue += "<p>" + byteArrayToHexString(sensorAddresses[i]) + " : " + String(temperatures[i]) + "</p>";
        }
    }
    return retValue;
}

bool IRAM_ATTR TimerHandler0(void* timerNo)
{
    static bool toggle = false;

    //timer interrupt toggles pin LED_BUILTIN
    oneWireLed(toggle);
    toggle = !toggle;

    return true;
}

void setup()
{
    pinMode(ETHERNET_LED, OUTPUT);
    pinMode(MQTT_LED, OUTPUT);
    pinMode(ONEWIRE_LED, OUTPUT);
    pinMode(SENSOR, OUTPUT);
    pinMode(LS_DATA, OUTPUT);
    pinMode(LS_CLK, OUTPUT);
    pinMode(LS_STB, OUTPUT);

    ethernetLed(LOW);
    mqttLed(LOW);
    oneWireLed(LOW);

    ITimer0.attachInterrupt(0.5, TimerHandler0);
    
    updateOutputs(0);

    Serial.begin(115200);
    DEBUG_PRINTLN("HeatingController32 V1.0 starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    sensors.begin();
    sensorsCount = sensors.getDS18Count();
    if (sensorsCount > 0) {
        ITimer0.detachInterrupt();
        oneWireLed(HIGH);
    }

    DEBUG_PRINT("Detected sensors: "); DEBUG_PRINTDEC(sensorsCount); DEBUG_PRINTLN();
    for (uint8_t i = 0; i < sensorsCount; i++) {
        sensors.getAddress(sensorAddresses[i], i);
        DEBUG_PRINT(byteArrayToHexString(sensorAddresses[i]));
        DEBUG_PRINT(" : ");
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
        DEBUG_PRINT(temperatures[i]); DEBUG_PRINTLN();
    }
    DEBUG_PRINTLN();
 
    WiFi.onEvent(WiFiEvent);
    ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/default.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/default.css", "text/css");
    });
    server.begin();

    mqttClient.setServer(MQTT_URL, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttConnect();
}

// Add the main program code into the continuous loop() function
void loop()
{
    if (mqttClient.connected()) {
        mqttClient.loop();
        mqttClient.publish(COMMAND_TOPIC, "test");
    }
    else {
        //mqttConnect();
    }

    sensors.requestTemperatures();
    for (uint8_t i = 0; i < sensorsCount; i++) {
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
    }
    delay(1000);

    if (outputs == 0) {
        outputs = 255;
        updateOutputs(outputs);
    }
    else {
        outputs = 0;
        updateOutputs(outputs);
    }

/*
    switch (ledMode)
    {
    case 0:
        DEBUG_PRINT(ledMode);
        digitalWrite(ETHERNET_LED, HIGH);
        digitalWrite(MQTT_LED, LOW);
        digitalWrite(ONEWIRE_LED, LOW);
        ledMode++;
        break;
    case 1:
        DEBUG_PRINT(ledMode);
        digitalWrite(ETHERNET_LED, LOW);
        digitalWrite(MQTT_LED, HIGH);
        digitalWrite(ONEWIRE_LED, LOW);
        ledMode++;
        break;
    case 2:
        DEBUG_PRINT(ledMode);
        digitalWrite(ETHERNET_LED, LOW);
        digitalWrite(MQTT_LED, LOW);
        digitalWrite(ONEWIRE_LED, HIGH);
        ledMode = 0;
        break;
    default:
        break;
    }
*/
}