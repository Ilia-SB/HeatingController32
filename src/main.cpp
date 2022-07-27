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

ESP32Timer ethernetLedTimer(0);
ESP32Timer mqttLedTimer(1);
ESP32Timer oneWireLedTimer(2);

void ethernetLed(uint8_t mode) {
    digitalWrite(ETHERNET_LED, mode);
}

void mqttLed(uint8_t mode) {
    digitalWrite(MQTT_LED, mode);
}

void oneWireLed(uint8_t mode) {
    digitalWrite(ONEWIRE_LED, mode);
}

bool IRAM_ATTR mqttLedBlink(void* timerNo)
{
    static bool toggle = false;

    //timer interrupt toggles pin LED_BUILTIN
    mqttLed(toggle);
    toggle = !toggle;

    return true;
}

bool IRAM_ATTR ethernetLedBlink(void* timerNo)
{
    static bool toggle = false;

    //timer interrupt toggles pin LED_BUILTIN
    ethernetLed(toggle);
    toggle = !toggle;

    return true;
}

bool IRAM_ATTR oneWireLedBlink(void* timerNo)
{
    static bool toggle = false;

    //timer interrupt toggles pin LED_BUILTIN
    oneWireLed(toggle);
    toggle = !toggle;

    return true;
}

bool IRAM_ATTR oneWireLedOff(void* timerNo) {
    oneWireLed(LOW);
    return false;
}

void updateOutputs(uint16_t outputs) {
    digitalWrite(LS_STB, LOW);
    shiftOut(LS_DATA, LS_CLK, LSBFIRST, outputs);
    shiftOut(LS_DATA, LS_CLK, LSBFIRST, outputs >> 8);
    digitalWrite(LS_STB, HIGH);
}

bool mqttConnect() {
    mqttLedTimer.attachInterruptInterval(LED_BLINK_FAST, mqttLedBlink);
    if (!ethConnected) {
        mqttLedTimer.detachInterrupt();
        mqttLed(LOW);
        return false;
    }
    if (mqttClient.connect(HOSTNAME)) {
        DEBUG_PRINTLN("MQTT connected");
        mqttClient.subscribe(COMMAND_TOPIC);
        mqttLedTimer.detachInterrupt();
        mqttLed(HIGH);
        return true;
    }
    else {
        DEBUG_PRINTLN("MQTT connect failed");
        mqttLedTimer.detachInterrupt();
        mqttLed(LOW);
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
    case ARDUINO_EVENT_ETH_START:
        DEBUG_PRINTLN("ETH Started");
        ETH.setHostname(HOSTNAME);
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        DEBUG_PRINTLN("ETH Connected");
        ethConnected = true;
        ethernetLedTimer.detachInterrupt();
        ethernetLedTimer.attachInterruptInterval(LED_BLINK_MEDIUM, ethernetLedBlink);
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
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
        ethernetLedTimer.detachInterrupt();
        ethernetLed(HIGH);
        //mqttConnect();
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        ethConnected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        ethConnected = false;
        break;
    default:
        break;
    }
}

String byteArrayToHexString(const uint8_t* bytes, uint8_t len = 8) {
    String string = "";
    for (uint8_t i = 0; i < len; i++) {
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
    if (placeholder == "BUILD_NO") {
        retValue = VERSION;
    }
    return retValue;
}

void oneWireBlinkDetectedSensors(uint8_t sensorsCount) {
    oneWireLed(LOW);
    delay(500);
    for (uint8_t i = 0; i < sensorsCount; i++) {
        oneWireLed(HIGH);
        delay(500);
        oneWireLed(LOW);
        delay(500);
    }
    oneWireLed(LOW);
    delay(500);
}

void requestTemperatures() {
    /* TODO: Uncomment for production
    oneWireLed(HIGH);
    oneWireLedTimer.attachInterruptInterval(LED_BLINK_FAST, oneWireLedOff);
    */
    sensors.requestTemperatures();
}

void saveState(HeaterItem& heaterItem) {
    //String((unsigned int)bytes[i], (unsigned char)16U)
    char fileName[12] = "";
    strcat(fileName, "/item");
    strcat(fileName, heaterItem.address);
    
    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    
    StaticJsonDocument<256> doc;
    doc["address"] = heaterItem.address;
    doc["isEnabled"] = heaterItem.isEnabled;
    doc["sensorAddress"] = byteArrayToHexString(heaterItem.sensorAddress);
    doc["port"] = heaterItem.port;
    doc["isAuto"] = heaterItem.isAuto;
    doc["powerConsumption"] = heaterItem.powerConsumption;
    doc["isOn"] = heaterItem.isOn;
    doc["priority"] = heaterItem.priority;
    doc["isConnected"] = heaterItem.isConnected;
    doc["targetTemperature"] = heaterItem.getTargetTemperature();
    doc["temperatureAdjust"] = heaterItem.getTemperatureAdjust();

#ifdef DEBUG
    DEBUG_PRINT("Saving settings to: "); DEBUG_PRINTLN(fileName);
    char output[256];
    serializeJson(doc, output);
    DEBUG_PRINTLN(output);
#endif

    //serializeJson(doc, file);
    file.close();
}

void loadState() {

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

    updateOutputs(0);

    Serial.begin(115200);
    DEBUG_PRINTLN("HeatingController32 V1.0 starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    oneWireLedTimer.attachInterruptInterval(LED_BLINK_FAST, oneWireLedBlink);
    sensors.begin();
    sensorsCount = sensors.getDS18Count();
    oneWireLedTimer.detachInterrupt();
    if (sensorsCount > 0) {
        oneWireBlinkDetectedSensors(sensorsCount);
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
 

    ethernetLedTimer.attachInterruptInterval(LED_BLINK_FAST, ethernetLedBlink);
    WiFi.onEvent(WiFiEvent);
    ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/default.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/default.css", "text/css");
    });
    AsyncElegantOTA.begin(&server);
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

    requestTemperatures();
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
}