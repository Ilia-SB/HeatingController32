/*
    Ethernet:   http://arduino.ru/forum/apparatnye-voprosy/podklyuchenie-ethernet-lan8720-i-esp32-devkit-c-esp32-devkit-v1
                https://sautter.com/blog/ethernet-on-esp32-using-lan8720/
*/

#include <version.h>
#include <Arduino.h>
#include "Config.h"
#include "Utils.h"
#include "Settings.h"
#include "HeaterItem.h"
#include "MqttInterface.h"
#include "DebugPrint.h"
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ETH.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32TimerInterrupt.h>
#include <ElegantOTA.h>
#include <esp_system.h>
#include <time.h>

TaskHandle_t hndlSystem;
TaskHandle_t hndlMain;

SemaphoreHandle_t mutex = NULL;

void taskSystem(void* pvParameters);
void taskMain(void* pvParameters);

WiFiClient ethClient;
WiFiClient tcpClient;
static bool ethConnected = false;

AsyncWebServer server(80);

PubSubClient mqttClient(ethClient);

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

uint16_t outputs = 0;

uint16_t currentConsumption[3];

uint8_t sensorsCount;
DeviceAddress sensorAddresses[MAX_NUMBER_OF_SENSORS];
float temperatures[MAX_NUMBER_OF_SENSORS];

uint8_t unconnectedSensorsCount = 0;
HeaterItem* unconnectedSensors[NUMBER_OF_HEATERS];

uint8_t unconfiguredSensorsCount = 0;
DeviceAddress* unconfiguredSensors[MAX_NUMBER_OF_SENSORS];

Settings settings;

HeaterItem heaterItems[NUMBER_OF_HEATERS];

uint8_t ledMode = 0;

ESP32Timer iTimer(1);
ESP32_ISR_Timer ISR_Timer;
#define ITIMER_INTERVAL_MS  1

bool IRAM_ATTR TimerHandler(void* timerNo) {
    ISR_Timer.run();
    return true;
}

typedef void (*irqCallback)  ();

#define TIMER_NUM_MQTT_LED_BLINK         0
#define TIMER_NUM_ETHERNET_LED_BLINK     1
#define TIMER_NUM_ONEWIRE_LED_BLINK      2

#define NUMBER_OF_ISR_TIMERS 3 //number of previous defines

void mqttLedBlink(void);
void ethernetLedBlink(void);
void oneWireLedBlink(void);
void oneWireLedOff(void);
void requestTemperatures(void);
void readTemperatures(void);

irqCallback isrTimerCallbacks[NUMBER_OF_ISR_TIMERS] = {
    mqttLedBlink,
    ethernetLedBlink,
    oneWireLedBlink
};

bool flagEmergency[NUMBER_OF_PHASES] = {false,false,false};

bool flagConsumptionDataReceived = false;
unsigned long consumptionDataReceived[NUMBER_OF_PHASES] = {0ul,0ul,0ul};
unsigned long emergencyHandled[NUMBER_OF_PHASES] = {0ul,0ul,0ul};

bool heatersInitialized = false;

bool flagRestartNow = false;
bool flagProcessHeatersNow = false;


void ethernetLed(uint8_t);
void mqttLed(uint8_t);
void oneWireLed(uint8_t);
void updateOutputs(uint16_t);
void setPorts(boolean[]);
void processCommand(char*, char*, char*);
void mqttCallback(char*, byte*, const unsigned int);
void subscribeToExternalSensors(void);
bool tcpConnect(void);
bool mqttConnect(void);
void WiFiEvent(WiFiEvent_t);
String webServerPlaceholderProcessor(const String&);
void oneWireBlinkDetectedSensors(uint8_t);
void setDefaultSettings(Settings&);
void setDefaults(HeaterItem&);
void itemToJson(HeaterItem&, StaticJsonDocument<JSON_DOCUMENT_SIZE>&, bool);
void getItemFilename(uint8_t, String&);
void getSettingsFilename(String&);
void saveSettings(Settings&);
void loadSettings(Settings&);
void saveState(HeaterItem&);
void loadState(HeaterItem&);
void processSettingsForm(AsyncWebServerRequest*);
void processControlForm(AsyncWebServerRequest*);
void reportHeatersState(void);
void reportHeaterState(HeaterItem&);
void reportTemperatures(void);
void getConsumptionData(const char*);
void initHeaters(void);
void initHeater(HeaterItem& heater);
bool checkSensorConnected(HeaterItem& heater);
bool checkSensorConfigured(DeviceAddress* sensor);
void processHeaters(void);
uint16_t calculateHeatersConsumption(uint8_t);
void sanityCheckHeater(HeaterItem&);
void deleteSettings(void);
void reboot(AsyncWebServerRequest*);
void onOtaStart(void);
void onOtaEnd(bool);

void heaterItemOutputCallback(uint8_t, bool);
void heaterItemNotificationCallback(HeaterItem& heater);

void ethernetLed(uint8_t mode) {
    digitalWrite(ETHERNET_LED, mode);
}

void mqttLed(uint8_t mode) {
    digitalWrite(MQTT_LED, mode);
}

void oneWireLed(uint8_t mode) {
    digitalWrite(ONEWIRE_LED, mode);
}

void mqttLedBlink()
{
    static bool toggle = false;

    mqttLed(toggle);
    toggle = !toggle;
}

void ethernetLedBlink()
{
    static bool toggle = false;

    ethernetLed(toggle);
    toggle = !toggle;
}

void oneWireLedBlink()
{
    static bool toggle = false;

    oneWireLed(toggle);
    toggle = !toggle;
}

void oneWireLedOff() {
    oneWireLed(LOW);
}

void updateOutputs(uint16_t outputs) {
    digitalWrite(LS_STB, LOW);
    shiftOut(LS_DATA, LS_CLK, MSBFIRST, outputs >> 8);
    shiftOut(LS_DATA, LS_CLK, MSBFIRST, outputs);
    digitalWrite(LS_STB, HIGH);
}

void heaterItemOutputCallback(uint8_t port, bool state) {
    if (state == true)
        bitSet(outputs, port - 1); //physical port numbers are 1-based, thus -1
    else
        bitClear(outputs, port - 1);
    updateOutputs(outputs);
}

void heaterItemNotificationCallback(HeaterItem& heater) {
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    reportHeaterState(heater);
}

// Helper function to safely write to TCP client
// Returns false if write failed or timed out
inline bool tcpSafeWrite(const String& msg) {
    if (!tcpClient.connected()) {
        return false;
    }
    // availableForWrite() checks if there's buffer space
    // This prevents blocking on full buffers
    if (tcpClient.availableForWrite() < msg.length()) {
        return false; // Skip this write to avoid blocking
    }
    size_t written = tcpClient.print(msg);
    return (written == msg.length());
}

inline bool tcpSafeWrite(const char* msg) {
    if (!tcpClient.connected()) {
        return false;
    }
    size_t len = strlen(msg);
    if (tcpClient.availableForWrite() < len) {
        return false;
    }
    size_t written = tcpClient.print(msg);
    return (written == len);
}

// Debug output functions
void debugPrint(const String& msg) {
    if (settings.debugSerial) {
        Serial.print(msg);
    }
    if (settings.debugTcp) {
        tcpSafeWrite(msg);
    }
}

void debugPrint(const char* msg) {
    if (settings.debugSerial) {
        Serial.print(msg);
    }
    if (settings.debugTcp) {
        tcpSafeWrite(msg);
    }
}

void debugPrint(int val) {
    if (settings.debugSerial) {
        Serial.print(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 10) {
        tcpClient.print(val);
    }
}

void debugPrint(unsigned int val) {
    if (settings.debugSerial) {
        Serial.print(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 10) {
        tcpClient.print(val);
    }
}

void debugPrint(long val) {
    if (settings.debugSerial) {
        Serial.print(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 15) {
        tcpClient.print(val);
    }
}

void debugPrint(unsigned long val) {
    if (settings.debugSerial) {
        Serial.print(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 15) {
        tcpClient.print(val);
    }
}

void debugPrint(float val) {
    if (settings.debugSerial) {
        Serial.print(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 15) {
        tcpClient.print(val);
    }
}

void debugPrintln() {
    if (settings.debugSerial) {
        Serial.println();
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 2) {
        tcpClient.println();
    }
}

void debugPrintln(const String& msg) {
    if (settings.debugSerial) {
        Serial.println(msg);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > msg.length() + 2) {
        tcpClient.println(msg);
    }
}

void debugPrintln(const char* msg) {
    if (settings.debugSerial) {
        Serial.println(msg);
    }
    if (settings.debugTcp) {
        size_t len = strlen(msg);
        if (tcpClient.connected() && tcpClient.availableForWrite() > len + 2) {
            tcpClient.println(msg);
        }
    }
}

void debugPrintln(int val) {
    if (settings.debugSerial) {
        Serial.println(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 12) {
        tcpClient.println(val);
    }
}

void debugPrintln(unsigned int val) {
    if (settings.debugSerial) {
        Serial.println(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 12) {
        tcpClient.println(val);
    }
}

void debugPrintln(long val) {
    if (settings.debugSerial) {
        Serial.println(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 17) {
        tcpClient.println(val);
    }
}

void debugPrintln(unsigned long val) {
    if (settings.debugSerial) {
        Serial.println(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 17) {
        tcpClient.println(val);
    }
}

void debugPrintln(float val) {
    if (settings.debugSerial) {
        Serial.println(val);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 17) {
        tcpClient.println(val);
    }
}

void debugPrintDec(int val) {
    if (settings.debugSerial) {
        Serial.print(val, DEC);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 10) {
        tcpClient.print(val, DEC);
    }
}

void debugPrintHex(int val) {
    if (settings.debugSerial) {
        Serial.print(val, HEX);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 10) {
        tcpClient.print(val, HEX);
    }
}

void debugPrintArray(uint8_t* arr, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (settings.debugSerial) {
            Serial.print(arr[i]);
            Serial.print(" ");
        }
        if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > 10) {
            tcpClient.print(arr[i]);
            tcpClient.print(" ");
        }
    }
}

void debugStack() {
    String msg = "Free stack: ";
    msg += String(uxTaskGetStackHighWaterMark(NULL));
    if (settings.debugSerial) {
        Serial.println(msg);
    }
    if (settings.debugTcp && tcpClient.connected() && tcpClient.availableForWrite() > msg.length() + 2) {
        tcpClient.println(msg);
    }
}

void setPorts(boolean ports[NUMBER_OF_PORTS]) {
    uint16_t output = 0;
    for (uint8_t i=0; i<NUMBER_OF_PORTS; i++) {
        if (ports[i])
            bitSet(output, i);
    }
    updateOutputs(output);
}

void getConsumptionData(const char* rawData) {
    StaticJsonDocument<JSON_DOCUMENT_SIZE_ENERGY_METER> doc;
    deserializeJson(doc, rawData);

    for (uint8_t phase=0; phase<NUMBER_OF_PHASES; phase++) {
        char key[5] = "POW";
        char idx = phase + 1 + 48; //convert (phase+1) to ascii
        strncat(key, &idx, 1);
        //debugPrint(key); debugPrint(" ");
        if (doc.containsKey(key)) {
            debugPrintln("Received consumption data");
            flagConsumptionDataReceived = true;
            currentConsumption[phase] = (uint16_t)(doc[key].as<float>() * 1000);
            consumptionDataReceived[phase] = millis();
            bool emergency = false;
            //debugPrintln(currentConsumption[phase]);
            if (currentConsumption[phase] > settings.consumptionLimit[phase]) {
                flagEmergency[phase] = true;
                emergency = true;
            } else {
                flagEmergency[phase] = false;
            }
            // Only process heaters immediately if there's an emergency
            if (emergency) {
                flagProcessHeatersNow = true;
            }
        }
    }
}

void processCommand(char* item, char* command, char* payload) {
    char statusTopic[64];
    char val[32];

    debugPrint("Received command: ");debugPrint(item);debugPrint(" -> ");debugPrint(command);debugPrint(" : ");debugPrintln(payload);
    if (strcasecmp("settings", item) == 0) {
        strcpy(statusTopic, STATUS_TOPIC);
        strcat(statusTopic, "/settings/");
        
        if (strcasecmp(command, HYSTERESIS) == 0) {
            if (settings.setHysteresis(payload)) {
                settings.getHysteresisCStr(val);
                strcat(statusTopic, HYSTERESIS);
                mqttClient.publish(statusTopic, val, false);
                saveSettings(settings);
            }
        }
        if (char* underscore = strcasestr(command, "_")) {
            char _command[strlen(command)];
            char _phase[2];
            memcpy(_command, command, underscore - command);
            _command[underscore - command] = '\0';
            strcpy(_phase, underscore + 1);
            uint8_t phase = (uint8_t)atoi(_phase);
            
            if (strcasecmp(_command, CONSUMPTION_LIMIT) == 0) {
                if (settings.setConsumptionLimit(payload, phase)) {
                    settings.getConsumptionLimitCStr(val, phase);
                    strcat(statusTopic, CONSUMPTION_LIMIT);
                    mqttClient.publish(statusTopic, val, false);
                    saveSettings(settings);
                }
            }
        }
        flagRestartNow = true;
        return;
    }

    uint8_t heaterNum = 0;
    bool found = false;
    for(uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if (strcasecmp(heaterItems[i].getSubtopic().c_str(), item) == 0) {
            found = true;
            heaterNum = i;
            break;
        }
    }

    if (!found) {
        return;
    }

    strcpy(statusTopic, STATUS_TOPIC);
    strcat(statusTopic, "/");
    strcat(statusTopic, heaterItems[heaterNum].getSubtopic().c_str());
    strcat(statusTopic, "/");

    HeaterItem* heater = &heaterItems[heaterNum];
    bool save = false;
    if (strcasecmp(command, IS_AUTO) == 0) {
        heater->setIsAuto(payload);
        save = true;
    }
    if (strcasecmp(command, IS_ON) == 0) {
        if (heater->getIsAuto() == false)
            heater->setWantsOn(payload);
    }
    if (strcasecmp(command, PRIORITY) == 0) {
        heater->setPriority(payload);
        save = true;
    }
    if (strcasecmp(command, TARGET_TEMPERATURE) == 0) {
        heater->setTargetTemperature(payload);
        save = true;
    }
    if (strcasecmp(command, SENSOR) == 0) {
        heater->setSensorAddress(payload);
        save = true;
    }
    if (strcasecmp(command, PORT) == 0) {
        heater->setPort(payload);
        save = true;
    }
    if (strcasecmp(command, PHASE) == 0) {
        heater->setPhase(payload);
        save = true;
    }
    if (strcasecmp(command, TEMPERATURE_ADJUST) == 0) {
        heater->setTemperatureAdjust(payload);
        save = true;
    }
    if (strcasecmp(command, USE_EXTERNAL_SENSOR) == 0) {
        heater->setUseExternalSensor(payload);
        save = true;
        if (mqttClient.connected()) {
            subscribeToExternalSensors();
        }
    }
    if (strcasecmp(command, EXTERNAL_SENSOR_TOPIC) == 0) {
        heater->setExternalSensorTopic(payload);
        save = true;
        if (mqttClient.connected()) {
            subscribeToExternalSensors();
        }
    }
    if (strcasecmp(command, CONSUMPTION) == 0) {
        heater->setPowerConsumption(payload);
        save = true;
    }
    if (strcasecmp(command, IS_ENABLED) == 0) {
        heater->setIsEnaled(payload);
        save = true;
    }

    heater->setIsConnected(checkSensorConnected(*heater));
    sanityCheckHeater(*heater);
    if (save) {
        saveState(*heater);
    }
    reportHeaterState(*heater);
    // Signal taskSystem to call processHeaters() immediately after MQTT callback returns
    flagProcessHeatersNow = true;
}

void mqttCallback(char* topic, byte* payload, const unsigned int len) {
    if (len >= MQTT_MAX_PACKET_SIZE) {
        return;
    }

    char payloadCopy[len + 1];
    memcpy(payloadCopy, payload, len);
    payloadCopy[len] = '\0';
    
    //Energy meter
    if (strcasecmp(topic, ENERGY_METER_TOPIC) == 0) {
        strupr(payloadCopy);
        getConsumptionData(payloadCopy);
        return;
    }

    //Check for external sensor topics
    for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
        if (heaterItems[i].getUseExternalSensor() && 
            strlen(heaterItems[i].getExternalSensorTopic()) > 0 &&
            strcasecmp(topic, heaterItems[i].getExternalSensorTopic()) == 0) {
            float temp = strtof(payloadCopy, nullptr);
            heaterItems[i].updateExternalSensorTemp(temp);
            debugPrint("External sensor update for heater ");
            debugPrint(i);
            debugPrint(": ");
            debugPrintln(temp);
            return;
        }
    }

    strupr(payloadCopy);
    //Controller commands
    uint8_t firstSlash = 0;
    uint8_t secondSlash = 0;
    for (uint8_t i=strlen(topic)-1; i--;) {
        if (topic[i] == '/') {
            if (firstSlash == 0) {
                firstSlash = i;
            } else {
                secondSlash = i;
                break;
            }
        }
    }

    if (firstSlash == 0 || secondSlash == 0) {
        return; //incorrect topic
    }

    char command[strlen(topic) - firstSlash];
    char item[firstSlash - secondSlash];
    memcpy(command, &topic[firstSlash + 1], strlen(topic) - firstSlash - 1);
    command[strlen(topic) - firstSlash - 1] = '\0';
    memcpy(item, &topic[secondSlash + 1], firstSlash - secondSlash - 1);
    item[firstSlash - secondSlash - 1] = '\0';

    processCommand(item, command, payloadCopy);
}

void subscribeToExternalSensors() {
    for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
        if (heaterItems[i].getUseExternalSensor() && 
            strlen(heaterItems[i].getExternalSensorTopic()) > 0) {
            mqttClient.subscribe(heaterItems[i].getExternalSensorTopic());
            debugPrint("Subscribed to external sensor: ");
            debugPrintln(heaterItems[i].getExternalSensorTopic());
        }
    }
}

bool tcpConnect() {
    if (!ethConnected) {
        return false;
    }

    if (tcpClient.connect(settings.tcpUrl.c_str(), settings.tcpPort)) {
        Serial.println("TCP client connected.");
        tcpClient.setNoDelay(true);
        // Set socket timeout to prevent blocking indefinitely
        // This prevents task watchdog crashes when TCP server is slow
        tcpClient.setTimeout(100); // 100ms timeout for write operations
        return true;
    } else {
        Serial.println("TCP client connect failed.");
        return false;
    }
}

bool mqttConnect() {
    if (!ethConnected) {
        mqttLed(LOW);
        return false;
    }
    ISR_Timer.enable(TIMER_NUM_MQTT_LED_BLINK);
    mqttClient.setServer(settings.mqttUrl.c_str(), settings.mqttPort);
    mqttClient.setCallback(mqttCallback);
    if (mqttClient.connect(HOSTNAME, LWT_TOPIC, 0, true, "Offline")) {
        debugPrintln("MQTT connected");
        mqttClient.subscribe(COMMAND_TOPIC.c_str());
        mqttClient.subscribe(ENERGY_METER_TOPIC);
        if (heatersInitialized) {
            subscribeToExternalSensors();
        }
        mqttClient.publish(LWT_TOPIC, "Online", true);
        ISR_Timer.disable(TIMER_NUM_MQTT_LED_BLINK);
        mqttLed(HIGH);
        if (heatersInitialized)
            reportHeatersState();
        return true;
    }
    else {
        debugPrintln("MQTT connect failed");
        ISR_Timer.disable(TIMER_NUM_MQTT_LED_BLINK);
        mqttLed(LOW);
        return false;
    }
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        debugPrintln("ETH Started");
        ETH.setHostname(HOSTNAME);
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        debugPrintln("ETH Connected");
        ethConnected = true;
        ISR_Timer.changeInterval(TIMER_NUM_ETHERNET_LED_BLINK, LED_BLINK_MEDIUM);
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        debugPrint("ETH MAC: ");
        debugPrint(ETH.macAddress());
        debugPrint(", IPv4: ");
        debugPrint(ETH.localIP());
        if (ETH.fullDuplex()) {
            debugPrint(", FULL_DUPLEX");
        }
        debugPrint(", ");
        debugPrint(ETH.linkSpeed());
        debugPrintln("Mbps");
        ethConnected = true;
        ISR_Timer.disable(TIMER_NUM_ETHERNET_LED_BLINK);
        ethernetLed(HIGH);
        mqttConnect();
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        ethConnected = false;
        ethernetLed(LOW);
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        ethConnected = false;
        ethernetLed(LOW);
        break;
    default:
        break;
    }
}

String webServerPlaceholderProcessor(const String& placeholder) {
    String retValue = "";
    if (placeholder.equals("SENSOR_ADDR")) {
        retValue += "<p>Sensors count: " + String(sensorsCount) + "</p>";
        for (uint8_t i = 0; i < sensorsCount; i++) {
            retValue += "<p>";
            String sensorAddress;
            byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensorAddress);
            retValue += sensorAddress;
            retValue += " : ";
            retValue += String(temperatures[i]);
            retValue += "</p>";
        }
    }
    if (placeholder.equals("BUILD_NO")) {
        retValue = VERSION;
    }
    if (placeholder.equals("ITEMS_SETTINGS")) {
        String sensors[sensorsCount];
        for (uint8_t i=0; i<sensorsCount; i++) {
            byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensors[i]);
        }
        for (uint8_t i=0; i< NUMBER_OF_HEATERS; i++){
            String itemNum;
            if (i < 10)
                itemNum += "0";
            itemNum += String(i);

            retValue += "<p onclick=\"showHideItem('";
            retValue += itemNum;
            retValue += "')\" class=\"item\">";
            retValue += heaterItems[i].getName();
            retValue += "</p><form style=\"color:#eaeaea;\" method=\"post\" action=\"/settings\"><fieldset id=\"item_";
            retValue += itemNum;
            retValue += "\" style=\"display: none;\">";
            retValue += "<input type=\"hidden\" name=\"item\" value=\"";
            retValue += String(i);
            retValue += "\">";
            retValue += "<div style=\"color:#eaeaea;text-align: left;\"><table class=\"center\"><tbody><tr><td class=\"name\">Name</td><td class=\"value\"><input type=\"text\" name=\"name\" value=\"";
            retValue += heaterItems[i].getName();
            retValue += "\"></td></tr><tr><td class=\"name\">Sensor</td><td class=\"value\"><select name=\"sensor\">";
            retValue += "<option value=\"";
            String setSensor;
            byteArrayToHexString(heaterItems[i].getSensorAddress(), SENSOR_ADDR_LEN, setSensor);
            retValue += String(-1);
            retValue += "\">";
            retValue += "* ";
            retValue += setSensor;
            retValue += "</option>";
            for (uint8_t j=0; j<sensorsCount; j++){ 
                retValue += "<option value=\"";
                retValue += String(j);
                retValue += "\">";
                retValue += sensors[j];
                retValue += "</option>";
            }
            retValue += "</td></tr><tr><td class=\"name\">Subtopic</td><td class=\"value\"><input type=\"text\" name=\"subtopic\" value=\"";
            retValue += heaterItems[i].getSubtopic();
            retValue += "\"></td></tr><tr><td class=\"name\">Port</td><td class=\"value\"><select name=\"port\">";
            for (uint8_t j=1; j<NUMBER_OF_HEATERS+1; j++) {
                retValue += "<option value=\"";
                retValue += String(j);
                retValue += "\"";
                if (j==heaterItems[i].getPort()) {
                    retValue += " selected=\"selected\"";
                }
                retValue += ">";
                retValue += String(j);
                retValue += "</option>";
            }
            retValue += "</td></tr><tr><td class=\"name\">Phase</td><td class=\"value\"><select name=\"phase\">";
            for (uint8_t j=1; j<NUMBER_OF_PHASES+1; j++) {
                retValue += "<option value=\"";
                retValue += String(j);
                retValue += "\"";
                if (j==heaterItems[i].getPhase()) {
                    retValue += " selected=\"selected\"";
                }
                retValue += ">";
                retValue += String(j);
                retValue += "</option>";
            }
            retValue += "</td></tr><tr><td class=\"name\">Consumption</td><td class=\"value\"><input type=\"text\" name=\"consumption\" value=\"";
            retValue += String(heaterItems[i].getPowerConsumption());
            retValue += "\"></td></tr><tr><td class=\"name\">Priority</td><td class=\"value\"><input type=\"text\" name=\"priority\" value=\"";
            retValue += String(heaterItems[i].getPriority());
            retValue += "\"></td></tr><tr><td class=\"name\">Temperature adjust</td><td class=\"value\"><input type=\"text\" name=\"temperatureAdjust\" value=\"";
            retValue += String(heaterItems[i].getTemperatureAdjust());
            retValue += "\"></td></tr><tr><td class=\"name\">Use external sensor</td><td class=\"value\"><input type=\"checkbox\" name=\"useExternalSensor\"";
            retValue += heaterItems[i].getUseExternalSensor()?" checked":"";
            retValue += "></td></tr><tr><td class=\"name\">External sensor topic</td><td class=\"value\"><input type=\"text\" name=\"externalSensorTopic\" value=\"";
            retValue += heaterItems[i].getExternalSensorTopic();
            retValue += "\"></td></tr></tbody></table><button name=\"save\" type=\"submit\" class=\"bgrn\">Save</button></div></fieldset></form>";
        }
    }
    if (placeholder.equals("ITEMS_CONTROL")) {
        for (uint8_t i=0; i< NUMBER_OF_HEATERS; i++){
            String itemNum;
            if (i < 10)
                itemNum += "0";
            itemNum += String(i);

            retValue += "<div  class=\"item\" onclick=\"showHideItem('";
            retValue += itemNum;
            retValue += "')\"><p class=\"main\">";
            retValue += heaterItems[i].getName();
            retValue += " <span style=\"font-size:0.8em\">";
            retValue += String(heaterItems[i].getTemperature());
            retValue += "</span></p><p class=\"details\">";
            retValue += heaterItems[i].getIsEnabled()?"Enabled | ":"Disabled | ";
            retValue += heaterItems[i].getIsAuto()?"Auto | ":"Manual | ";
            retValue += heaterItems[i].getActualState()?"On | ":"Off | ";
            retValue += "Sensor: ";
            retValue += String(heaterItems[i].getRawSensorTemperature());
            retValue += " | Adjust: ";
            retValue += String(heaterItems[i].getTemperatureAdjust());
            if (heaterItems[i].getUseExternalSensor()) {
                retValue += " | Ext: ";
                retValue += heaterItems[i].isExternalSensorActive() ? "Active" : "Timeout";
            }
            retValue += "</p></div><form style=\"color:#eaeaea;\" method=\"post\" action=\"/control\"><fieldset id=\"item_";
            retValue += itemNum;
            retValue += "\" style=\"display: none;\">";
            retValue += "<input type=\"hidden\" name=\"item\" value=\"";
            retValue += String(i);
            retValue += "\">";
            retValue += "<div style=\"color:#eaeaea;text-align:left;\"><table class=\"center\"><tbody>";
            retValue += "<tr><td class=\"name\">Target temperature</td><td class=\"value\"><input type=\"text\" name=\"targetTemperature\" value=\"";
            retValue += String(heaterItems[i].getTargetTemperature());
            retValue += "\"></td></tr>";
            retValue += "<tr><td class=\"name\">Enabled</td><td class=\"value\"><input type=\"checkbox\" name=\"isEnabled\"";
            retValue += heaterItems[i].getIsEnabled()?" checked":"";
            retValue += "></td></tr>";
            retValue += "<tr><td class=\"name\">Auto</td><td class=\"value\"><input type=\"checkbox\" name=\"isAuto\"";
            retValue += heaterItems[i].getIsAuto()?" checked":"";
            retValue += "></td></tr>";
            retValue += "<tr><td class=\"name\">On</td><td class=\"value\"><input type=\"checkbox\" name=\"isOn\"";
            retValue += heaterItems[i].getActualState()?" checked":"";
            retValue += "></td></tr>";
            retValue += "</tbody></table><button name=\"save\" type=\"submit\" class=\"bgrn\">Save</button></div></fieldset></form>";
        }
        return retValue;
    }
    if (placeholder.equals("GLOBAL_SETTINGS")) {
        retValue += "<tr><td class=\"name\">Hysteresis</td><td class=\"value\"><input type=\"text\" name=\"hysteresis\" value=\"";
        retValue += String(settings.hysteresis, 1U);
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">MQTT url</td><td class=\"value\"><input type=\"text\" name=\"mqttUrl\" value=\"";
        retValue += settings.mqttUrl;
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">MQTT port</td><td class=\"value\"><input type=\"text\" name=\"mqttPort\" value=\"";
        retValue += String(settings.mqttPort);
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">TCP debug url</td><td class=\"value\"><input type=\"text\" name=\"tcpUrl\" value=\"";
        retValue += settings.tcpUrl;
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">TCP debug port</td><td class=\"value\"><input type=\"text\" name=\"tcpPort\" value=\"";
        retValue += String(settings.tcpPort);
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">Serial debug</td><td class=\"value\"><input type=\"checkbox\" name=\"debugSerial\"";
        retValue += settings.debugSerial?" checked":"";
        retValue += "></td></tr>";
        retValue += "<tr><td class=\"name\">TCP debug</td><td class=\"value\"><input type=\"checkbox\" name=\"debugTcp\"";
        retValue += settings.debugTcp?" checked":"";
        retValue += "></td></tr>";
        retValue += "<tr><td class=\"name\">NTP Server</td><td class=\"value\"><input type=\"text\" name=\"ntpServer\" value=\"";
        retValue += settings.ntpServer;
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">GMT Offset (hours)</td><td class=\"value\"><input type=\"number\" name=\"gmtOffsetHours\" min=\"-12\" max=\"14\" value=\"";
        retValue += String(settings.gmtOffsetHours);
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">Daylight Saving (hours)</td><td class=\"value\"><input type=\"number\" name=\"daylightOffsetHours\" min=\"0\" max=\"1\" value=\"";
        retValue += String(settings.daylightOffsetHours);
        retValue += "\"></td></tr>";
        for (uint8_t i=1; i<NUMBER_OF_PHASES+1; i++) {
            retValue += "<tr><td class=\"name\">Phase ";
            retValue += String(i);
            retValue += " limit</td><td class=\"value\"><input type=\"text\" name=\"phase_";
            retValue += String(i);
            retValue += "\" value=\"";
            retValue += String(settings.consumptionLimit[i-1]);
            retValue += "\"></td></tr>";
        }
    }
    if (placeholder.equals("BACKUP_ITEM_FILE")) {
        for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
            String fileName;
            getItemFilename(i, fileName);
            if (LittleFS.exists(fileName)) {
                retValue += "<label><p style=\"line-height: 1.8;\"><input type=\"checkbox\" data-url=\"";
                retValue += fileName;
                retValue += "\" checked>";
                retValue += fileName;
                if (heaterItems[i].getName().length() > 0) {
                    retValue += " (";
                    retValue += heaterItems[i].getName();
                    retValue += ")";
                }
                retValue += "</p></label>";
            }
        }
    }
    if (placeholder.equals("UNCONFIGURED")) {
        for (uint8_t i=0; i<unconfiguredSensorsCount; i++) {
            String sensor;
            byteArrayToHexString(*unconfiguredSensors[i], SENSOR_ADDR_LEN, sensor);
            retValue += "<li>";
            retValue += sensor;
            retValue += "</li>";
        }
    }
    if (placeholder.equals("UNCONNECTED")) {
        for (uint8_t i=0; i<unconnectedSensorsCount; i++) {
            String sensor;
            byteArrayToHexString(unconnectedSensors[i]->getSensorAddress(), SENSOR_ADDR_LEN, sensor);
            retValue += "<li>";
            retValue += sensor;
            retValue += " (";
            retValue += unconnectedSensors[i]->getName();
            retValue += ")</li>";
        }
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
    debugPrintln("Requesting temperatures...");
    oneWireLed(HIGH);
    ISR_Timer.setTimeout(LED_BLINK_FAST, oneWireLedOff); //turn the led off for a while to indicate activity
    sensors.requestTemperatures();
}

void readTemperatures() {
    debugPrintln("Reading temperatures...");
    for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
        if (heaterItems[i].getIsConnected() == true) {
            float _temperature = sensors.getTempC(heaterItems[i].getSensorAddress());
            if ((int)_temperature != HeaterItem::SENSOR_NOT_CONNECTED && (int)_temperature != HeaterItem::SENSOR_READ_ERROR) {
                heaterItems[i].setTemperature(_temperature);
                debugPrint(heaterItems[i].getName());debugPrint(": ");debugPrintln(_temperature);
            } else {
                debugPrint("Error reading temperature for item ");debugPrintln(heaterItems[i].getName());
                heaterItems[i].tempReadError();
                if(heaterItems[i].getTempReadErrors() >= MAX_TEMP_READ_ERRORS) {
                    StaticJsonDocument<JSON_DOCUMENT_SIZE_SMALL> doc;
                    doc[TEMPERATURE_READ_ERRORS] = heaterItems[i].getTempReadErrors();
                    String mqttPayload;
                    serializeJson(doc, mqttPayload);

                    String mqttTopic;
                    mqttTopic += ERROR_TOPIC;
                    mqttTopic += "/";
                    mqttTopic += heaterItems[i].getSubtopic();
                    mqttTopic += "/STATE";

                    mqttClient.publish(mqttTopic.c_str(), mqttPayload.c_str(), false);
                    debugPrint("Heater ");debugPrint(heaterItems[i].getName());debugPrint(": number of temperature read errors since last report: ");debugPrintln(heaterItems[i].getTempReadErrors());
                    heaterItems[i].setTempReadErrors(0); //reset counter
                }
            }
        } else {
            heaterItems[i].setTemperature(0);
        }
    }
    reportTemperatures();
}

void setDefaultSettings(Settings& settings) {
    settings.hysteresis = DEFAULT_HYSTERESIS;
    settings.mqttUrl = MQTT_URL;
    settings.mqttPort = MQTT_PORT;
    settings.debugSerial = true;
    settings.debugTcp = true;
    settings.tcpUrl = TCP_URL;
    settings.tcpPort = TCP_PORT;
    settings.ntpServer = NTP_SERVER;
    settings.gmtOffsetHours = GMT_OFFSET_HOURS;
    settings.daylightOffsetHours = DAYLIGHT_OFFSET_HOURS;
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        settings.consumptionLimit[i] = CONSUMPTION_LIMITS[i];
    }
    saveSettings(settings);
}

void setDefaults(HeaterItem& heaterItem) {
    heaterItem.setName("Heater " + String(heaterItem.getAddress()));
    String addr;
    heaterItem.getAddressString(addr, "%06d");
    heaterItem.setSubtopic("item_" + addr);
    heaterItem.setTargetTemperature(5.0f);
    heaterItem.setTemperatureAdjust(0.0f);
}

void itemToJson(HeaterItem& heaterItem, StaticJsonDocument<JSON_DOCUMENT_SIZE>& doc, bool forReport) {
    doc["name"] = heaterItem.getName();
    doc["address"] = heaterItem.getAddress();
    doc["subtopic"] = heaterItem.getSubtopic();
    if (forReport)
        doc["isEnabled"] = heaterItem.getIsEnabled()?ON:OFF;
    else
        doc["isEnabled"] = heaterItem.getIsEnabled();
    JsonArray sensorAddress = doc.createNestedArray("sensorAddress");
    for (uint8_t i=0; i<SENSOR_ADDR_LEN; i++) {
        sensorAddress.add(heaterItem.getSensorAddress()[i]);
    }
    char addr[3*SENSOR_ADDR_LEN];
    heaterItem.getSensorAddressCStr(addr);
    doc["sensorAddressString"] = addr;
    doc["port"] = heaterItem.getPort();
    doc["phase"] = heaterItem.getPhase();
    if (forReport)
        doc["isAuto"] = heaterItem.getIsAuto()?ON:OFF;
    else
        doc["isAuto"] = heaterItem.getIsAuto();
    doc["powerConsumption"] = heaterItem.getPowerConsumption();
    if (forReport)
        doc["isOn"] = heaterItem.getWantsOn()?ON:OFF;
    else
        doc["isOn"] = heaterItem.getWantsOn();
    doc["priority"] = heaterItem.getPriority();
    doc["targetTemperature"] = heaterItem.getTargetTemperature();
    doc["temperatureAdjust"] = heaterItem.getTemperatureAdjust();
    doc["useExternalSensor"] = heaterItem.getUseExternalSensor();
    doc["externalSensorTopic"] = heaterItem.getExternalSensorTopic();
    if (forReport) {
        doc["sensorTemperature"] = heaterItem.getSensorTemperature();
        doc["temperature"] = heaterItem.getTemperature();
        doc["externalSensorActive"] = heaterItem.isExternalSensorActive();
    }
}

void getItemFilename(uint8_t i, String& fileName) {
    fileName = "/item";
    fileName += String(i);
    fileName += ".cfg";
}

void getSettingsFilename(String& fileName) {
    fileName = "/settings.cfg";
}

void saveSettings(Settings& settings) {
    String fileName;
    getSettingsFilename(fileName);
    File file = LittleFS.open(fileName, FILE_WRITE, true);
    StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
    doc[SETTINGS_SETTINGS_VERSION] = SETTINGS_VERSION;
    doc[SETTINGS_HYSTERESIS] = settings.hysteresis;
    doc[SETTINGS_MQTT_URL] = settings.mqttUrl;
    doc[SETTINGS_MQTT_PORT] = settings.mqttPort;
    doc[SETTINGS_DEBUG_SERIAL] = settings.debugSerial;
    doc[SETTINGS_DEBUG_TCP] = settings.debugTcp;
    doc[SETTINGS_TCP_URL] = settings.tcpUrl;
    doc[SETTINGS_TCP_PORT] = settings.tcpPort;
    doc[SETTINGS_NTP_SERVER] = settings.ntpServer;
    doc[SETTINGS_GMT_OFFSET] = settings.gmtOffsetHours;
    doc[SETTINGS_DAYLIGHT_OFFSET] = settings.daylightOffsetHours;
    JsonArray consumptionLimit = doc.createNestedArray("consumptionLimit");
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        consumptionLimit.add(settings.consumptionLimit[i]);
    }

    debugPrint("Saving settings to: "); debugPrintln(fileName);
    char output[JSON_DOCUMENT_SIZE_SETTINGS];
    serializeJson(doc, output);
    debugPrintln(output);
    serializeJson(doc, file);
    file.close();
}

void saveState(HeaterItem& heaterItem) {
    String fileName;
    getItemFilename(heaterItem.getAddress(), fileName);
    File file = LittleFS.open(fileName, FILE_WRITE, true);
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    itemToJson(heaterItem, doc, false);

    debugPrint("Saving state to: "); debugPrintln(fileName);
    char output[JSON_DOCUMENT_SIZE];
    serializeJson(doc, output);
    debugPrintln(output);

    serializeJson(doc, file);
    file.close();
}

void loadSettings(Settings& settings) {
    String fileName;
    getSettingsFilename(fileName);
    if (!LittleFS.exists(fileName)) {
        setDefaultSettings(settings);
    }
    else {
        File file = LittleFS.open(fileName, FILE_READ, true);

        StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
        deserializeJson(doc, file);

        if (!doc.containsKey(SETTINGS_SETTINGS_VERSION) || doc[SETTINGS_SETTINGS_VERSION] != SETTINGS_VERSION) {
            debugPrintln("Settings version mismatch.");
            deleteSettings();
            setDefaultSettings(settings);
            return;
        }

        settings.hysteresis = doc[SETTINGS_HYSTERESIS].as<float>();
        settings.mqttUrl = doc[SETTINGS_MQTT_URL].as<String>();
        settings.mqttPort = doc[SETTINGS_MQTT_PORT].as<uint16_t>();
        settings.debugSerial = doc.containsKey(SETTINGS_DEBUG_SERIAL) ? doc[SETTINGS_DEBUG_SERIAL].as<bool>() : true;
        settings.debugTcp = doc.containsKey(SETTINGS_DEBUG_TCP) ? doc[SETTINGS_DEBUG_TCP].as<bool>() : true;
        settings.tcpUrl = doc.containsKey(SETTINGS_TCP_URL) ? doc[SETTINGS_TCP_URL].as<String>() : TCP_URL;
        settings.tcpPort = doc.containsKey(SETTINGS_TCP_PORT) ? doc[SETTINGS_TCP_PORT].as<uint16_t>() : TCP_PORT;
        settings.ntpServer = doc.containsKey(SETTINGS_NTP_SERVER) ? doc[SETTINGS_NTP_SERVER].as<String>() : NTP_SERVER;
        settings.gmtOffsetHours = doc.containsKey(SETTINGS_GMT_OFFSET) ? doc[SETTINGS_GMT_OFFSET].as<int>() : GMT_OFFSET_HOURS;
        settings.daylightOffsetHours = doc.containsKey(SETTINGS_DAYLIGHT_OFFSET) ? doc[SETTINGS_DAYLIGHT_OFFSET].as<int>() : DAYLIGHT_OFFSET_HOURS;
        JsonArray consumptionLimit = doc[SETTINGS_CONSUMPTION_LIMIT];
        for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
            settings.consumptionLimit[i] = consumptionLimit.getElement(i).as<uint16_t>();
        }
        file.close();
    }
}

void deleteSettings() {
    debugPrintln("Deleting global settings.");
    String fileName;
    getSettingsFilename(fileName);
    LittleFS.remove(fileName);
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        String fileName;
        getItemFilename(i, fileName);
        if (LittleFS.exists(fileName)) {
            debugPrint("Deleting settings for ");debugPrint(fileName);debugPrintln(".");
            LittleFS.remove(fileName);
        }
    }
}

void loadState(HeaterItem& heaterItem) {
    String fileName;
    getItemFilename(heaterItem.getAddress(), fileName);
    if(!LittleFS.exists(fileName)) {
        setDefaults(heaterItem);
    }
    else {
        File file = LittleFS.open(fileName, FILE_READ, true);    

        StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
        deserializeJson(doc, file);

        heaterItem.setName(doc["name"].as<String>());
        heaterItem.setAddress(doc["address"].as<uint8_t>());
        heaterItem.setSubtopic(doc["subtopic"].as<String>());
        heaterItem.setIsEnabled(doc["isEnabled"].as<bool>());
        JsonArray sensorAddress = doc["sensorAddress"];
        byte addr[SENSOR_ADDR_LEN];
        for (uint8_t i=0; i<SENSOR_ADDR_LEN; i++) {
            addr[i] = sensorAddress.getElement(i).as<byte>();
        }
        heaterItem.setSensorAddress(addr);
        heaterItem.setPort(doc["port"].as<uint8_t>());
        heaterItem.setPhase(doc["phase"].as<uint8_t>());

        heaterItem.setPowerConsumption(doc["powerConsumption"].as<uint16_t>());
        heaterItem.setWantsOn(doc["isOn"].as<bool>());
        heaterItem.setPriority(doc["priority"].as<uint8_t>());
        heaterItem.setTargetTemperature(doc["targetTemperature"].as<float>());
        heaterItem.setTemperatureAdjust(doc["temperatureAdjust"].as<float>());
        heaterItem.setIsAuto(doc["isAuto"].as<bool>());
        if (doc.containsKey("useExternalSensor")) {
            heaterItem.setUseExternalSensor(doc["useExternalSensor"].as<bool>());
        }
        if (doc.containsKey("externalSensorTopic")) {
            heaterItem.setExternalSensorTopic(doc["externalSensorTopic"].as<const char*>());
        }
    }
}

void processSettingsForm(AsyncWebServerRequest* request) {
    if (request->hasParam("global_settings", true)) {
        if (request->hasParam(SETTINGS_HYSTERESIS, true)) {
            settings.hysteresis = request->getParam(SETTINGS_HYSTERESIS, true)->value().toFloat();
        }
        if (request->hasParam(SETTINGS_MQTT_URL, true)) {
            settings.mqttUrl = request->getParam(SETTINGS_MQTT_URL, true)->value();
        }
        if (request->hasParam(SETTINGS_MQTT_PORT, true)) {
            settings.mqttPort = request->getParam(SETTINGS_MQTT_PORT, true)->value().toInt();
        }
        if (request->hasParam(SETTINGS_TCP_URL, true)) {
            settings.tcpUrl = request->getParam(SETTINGS_TCP_URL, true)->value();
        }
        if (request->hasParam(SETTINGS_TCP_PORT, true)) {
            settings.tcpPort = request->getParam(SETTINGS_TCP_PORT, true)->value().toInt();
        }
        if (request->hasParam(SETTINGS_DEBUG_SERIAL, true)) {
            settings.debugSerial = true;
        } else {
            settings.debugSerial = false;
        }
        if (request->hasParam(SETTINGS_DEBUG_TCP, true)) {
            settings.debugTcp = true;
        } else {
            settings.debugTcp = false;
        }
        if (request->hasParam(SETTINGS_NTP_SERVER, true)) {
            settings.ntpServer = request->getParam(SETTINGS_NTP_SERVER, true)->value();
        }
        if (request->hasParam(SETTINGS_GMT_OFFSET, true)) {
            settings.gmtOffsetHours = request->getParam(SETTINGS_GMT_OFFSET, true)->value().toInt();
        }
        if (request->hasParam(SETTINGS_DAYLIGHT_OFFSET, true)) {
            settings.daylightOffsetHours = request->getParam(SETTINGS_DAYLIGHT_OFFSET, true)->value().toInt();
        }
        for (uint8_t i=1; i<NUMBER_OF_PHASES+1; i++) {
            String paramName = "phase_";
            paramName += String(i);
            if (request->hasParam(paramName, true)) {
                settings.consumptionLimit[i-1] = request->getParam(paramName, true)->value().toInt();
            }
        }
        saveSettings(settings);
        reboot(request);
        return;
    }

    //####################################################### items #######################################################
    uint8_t itemNo=0;
    if (request->hasParam("item", true)) {
        String var = request->getParam("item", true)->value();
        itemNo = (uint8_t)(var.toInt());
    }
    else {
        return;
    }

    if (request->hasParam("name", true)) {
        heaterItems[itemNo].setName(request->getParam("name", true)->value());
    }
    if (request->hasParam("sensor", true)) {
        int8_t sensorNum = (int8_t)(request->getParam("sensor", true)->value().toInt());
        if (sensorNum >=0) {
            heaterItems[itemNo].setSensorAddress(sensorAddresses[sensorNum]);
        }
    }
    if (request->hasParam("subtopic", true)) {
        heaterItems[itemNo].setSubtopic(request->getParam("subtopic", true)->value());
    }
    if (request->hasParam("port", true)) {
        heaterItems[itemNo].setPort((uint8_t)(request->getParam("port", true)->value().toInt()));
    }
    if (request->hasParam("phase", true)) {
        heaterItems[itemNo].setPhase((uint8_t)(request->getParam("phase", true)->value().toInt()));
    }
    if (request->hasParam("consumption", true)) {
        heaterItems[itemNo].setPowerConsumption((uint16_t)(request->getParam("consumption", true)->value().toInt()));
    }
    if (request->hasParam("priority", true)) {
        heaterItems[itemNo].setPriority((uint8_t)(request->getParam("priority", true)->value().toInt()));
    }
    if (request->hasParam("temperatureAdjust", true)) {
        heaterItems[itemNo].setTemperatureAdjust(request->getParam("temperatureAdjust", true)->value().c_str());
    }
    if (request->hasParam("useExternalSensor", true)) {
        heaterItems[itemNo].setUseExternalSensor(true);
    } else {
        heaterItems[itemNo].setUseExternalSensor(false);
    }
    if (request->hasParam("externalSensorTopic", true)) {
        heaterItems[itemNo].setExternalSensorTopic(request->getParam("externalSensorTopic", true)->value().c_str());
    }

    saveState(heaterItems[itemNo]);
    if (mqttClient.connected()) {
        subscribeToExternalSensors();
    }
    request->send(LittleFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        processHeaters();
        xSemaphoreGive(mutex);
    }
}

void reboot(AsyncWebServerRequest* request) {
    //TODO: fancy javascript for redirect
    request->send(LittleFS, "/rebooting.html");
    flagRestartNow = true;
}

void onOtaStart() {
    debugPrintln("Starting firmware update...");
}

void onOtaEnd(bool success) {
    if (success) {
        debugPrintln("Firmware update successful");
        flagRestartNow = true;
    } else {
        debugPrintln("Firmware update failed");
    }
}

void processControlForm(AsyncWebServerRequest* request) {
    uint8_t itemNo = 0;
    if (request->hasParam("item", true)) {
        String var = request->getParam("item", true)->value();
        itemNo = (uint8_t)(var.toInt());
    }
    else {
        return;
    }

    if (request->hasParam("targetTemperature", true)) {
        heaterItems[itemNo].setTargetTemperature(request->getParam("targetTemperature", true)->value().c_str());
    }
    if (request->hasParam("isEnabled", true)) {
        heaterItems[itemNo].setIsEnabled(true);
    } else {
        heaterItems[itemNo].setIsEnabled(false);
    }
    if (request->hasParam("isAuto", true)) {
        heaterItems[itemNo].setIsAuto(true);
    } else {
        heaterItems[itemNo].setIsAuto(false);
    }
    if (request->hasParam("isOn", true)) {
        if (heaterItems[itemNo].getIsAuto() == false) {
            heaterItems[itemNo].setWantsOn(true);
        }
    } else {
        if (heaterItems[itemNo].getIsAuto() == false) {
            heaterItems[itemNo].setWantsOn(false);
        }
    }

    saveState(heaterItems[itemNo]);
    request->send(LittleFS, "/control.html", String(), false, webServerPlaceholderProcessor);
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        processHeaters();
        xSemaphoreGive(mutex);
    }
}

void reportTemperatures() {
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if(heaterItems[i].getIsEnabled() == true) {
            reportHeaterState(heaterItems[i]);
        }
    }
}

void reportHeatersState() {
    debugPrintln("Reporting heaters state...");
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        reportHeaterState(heaterItems[i]);
    }
}

void reportHeaterState(HeaterItem& heater) {
    if (!heatersInitialized) {
        return;
    }

    //TODO: move below code to functions
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    itemToJson(heater, doc, true);
    String mqttTopic;
    mqttTopic += STATUS_TOPIC;
    mqttTopic += "/";
    mqttTopic += heater.getSubtopic();
    mqttTopic += "/STATE";

    String mqttPayload;
    serializeJson(doc, mqttPayload);

    uint16_t maxPayloadSize = MQTT_MAX_PACKET_SIZE - MQTT_MAX_HEADER_SIZE - 2 - mqttTopic.length();
    if(mqttPayload.length() > maxPayloadSize) {
        mqttClient.beginPublish(mqttTopic.c_str(), mqttPayload.length(), true);
        for (uint16_t j=0; j<mqttPayload.length(); j++) {
            mqttClient.write((uint8_t)(mqttPayload.c_str()[j]));
        }
        mqttClient.endPublish();
    }
    else {
        mqttClient.publish(mqttTopic.c_str(), mqttPayload.c_str(), true);
    }
}

void initHeaters() {
    debugPrintln("Initializing heaters...");
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        heaterItems[i].setAddress(i);
        initHeater(heaterItems[i]);
    }
    
    requestTemperatures();
    delay(READ_SENSORS_DELAY);
    readTemperatures();
    processHeaters();
    heatersInitialized = true;
    debugPrintln("Heaters initialized");
}

void initHeater(HeaterItem& heater) {
    heater.setHysteresis(settings.hysteresis);
    heater.setOutputCallBack(heaterItemOutputCallback);
    heater.setNotificationCallBack(heaterItemNotificationCallback);
        loadState(heater);
        heater.setActualState(false);
        if (heater.getIsAuto()) {
            heater.setWantsOn(false);
        }
        heater.setIsConnected(checkSensorConnected(heater));
        
        sanityCheckHeater(heater);
}

void sanityCheckHeater(HeaterItem& heater) {
    if (!heater.getIsConnected() && !heater.isExternalSensorActive()) {
            heater.setIsAuto(false); //Items with no temperature sensor can't be in auto mode
    }
    if (heater.getPhase() == HeaterItem::UNCONFIGURED || heater.getPort() == HeaterItem::UNCONFIGURED) {
        heater.setIsEnabled(false); //Can't work with unconfigured items
    }
}

bool checkSensorConnected(HeaterItem& heater) {
    for (uint8_t i=0; i<sensorsCount; i++) {
        if (compareArrays(heater.getSensorAddress(), sensorAddresses[i], SENSOR_ADDR_LEN )) {
            return true;
        }
    }
    unconnectedSensors[unconnectedSensorsCount++] = &heater;
    return false;
}

bool checkSensorConfigured(DeviceAddress* sensor) {
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if (compareArrays(heaterItems[i].getSensorAddress(), *sensor, SENSOR_ADDR_LEN)) {
            return true;
        }
    }
    unconfiguredSensors[unconfiguredSensorsCount++] = sensor;
    return false;
}

void processHeatersOutput(HeaterItem* heater) {
    debugPrint("| ");debugPrint(heater->getName());debugPrint("\t| ");debugPrint(heater->getIsAuto()?"Auto  ":"Manual");debugPrint("\t| ");debugPrint(String(heater->getTemperature(),2));
    debugPrint("\t| ");debugPrint(heater->getTargetTemperature());debugPrint("\t| ");debugPrint(heater->getDelta());debugPrint("\t| ");debugPrint(heater->getPowerConsumption());
    debugPrint("\t| ");debugPrint(heater->getActualState()?"On":"Off");debugPrint("\t| ");debugPrint(heater->getWantsOn()?"Yes":"No");debugPrint("\t| ");
}

void processHeaters() {
    if (flagRestartNow) {
        return;
    }
    debugPrintln("Processing heaters...");
    for (uint8_t phase=0; phase<NUMBER_OF_PHASES; phase++) {
        // Feed watchdog to prevent timeout during heavy debug output
        vTaskDelay(1 / portTICK_PERIOD_MS);
        
        debugPrint("Phase "); debugPrint(phase + 1);
        int16_t availablePower = 0;
        bool usingEstimatedConsumption = false;
        if (millis() - consumptionDataReceived[phase] < 5000) { //if data from the energy meter is not older than 5 sec.
            debugPrint(". Using measured power consumption. ");
            availablePower = settings.consumptionLimit[phase] - currentConsumption[phase];
            debugPrint("Available power: ");debugPrint(availablePower);debugPrint(" = ");debugPrint(settings.consumptionLimit[phase]);debugPrint(" - ");debugPrint(currentConsumption[phase]);
        } else {
            debugPrint(". Using estimated power consumption (");debugPrint(millis() - consumptionDataReceived[phase]);debugPrint("ms since last power reading). ");
            availablePower = settings.consumptionLimit[phase] - calculateHeatersConsumption(phase);
            usingEstimatedConsumption = true;
            debugPrint("Available power: ");debugPrint(availablePower);debugPrint(" = ");debugPrint(settings.consumptionLimit[phase]);debugPrint(" - ");debugPrint(calculateHeatersConsumption(phase));
            if (availablePower < 0) {
                flagEmergency[phase] = true;
            }
        }

        HeaterItem* manualHeaters[NUMBER_OF_HEATERS];
        uint8_t manualHeatersNum = 0;
        HeaterItem* autoHeaters[NUMBER_OF_HEATERS];
        uint8_t autoHeatersNum = 0;
        for(uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
            if(heaterItems[i].getIsEnabled() == true && (heaterItems[i].getPhase() - 1) == phase) {
                if (heaterItems[i].getIsAuto() == true) {
                    autoHeaters[autoHeatersNum++] = &heaterItems[i];
                } else {
                    manualHeaters[manualHeatersNum++] = &heaterItems[i];
                }
            }
        }
        HeaterItem::sortHeaters(manualHeaters, manualHeatersNum);
        HeaterItem::sortHeaters(autoHeaters, autoHeatersNum);
        
        debugPrint(". Auto heaters count: ");debugPrint(autoHeatersNum);debugPrint(", manual heaters count: ");debugPrintln(manualHeatersNum);

        //turn off
        //manual heaters
        debugPrintln("Manual -> Off");
        for (uint8_t i=0; i<manualHeatersNum; i++) {
            HeaterItem* heater = manualHeaters[i];
            processHeatersOutput(heater);
            if (heater->getActualState() == true && heater->getWantsOn() == false) {
                heater->setActualState(false);
                availablePower += heater->getPowerConsumption();
                debugPrint("turned OFF by user.");
            }
            debugPrintln();
        }
        //auto heaters
        debugPrintln("Auto -> Off");
        for (uint8_t i=0; i<autoHeatersNum; i++) {
            HeaterItem* heater = autoHeaters[i];
            processHeatersOutput(heater);
            if (heater->getActualState() == true && heater->getWantsOn() == false) {
                heater->setActualState(false);
                availablePower += heater->getPowerConsumption();
                debugPrint("turned OFF. Target temp reached.");
            } else {
                debugPrint("nothing to do.");
            }
            debugPrintln();
        }
        //emergency
        if (flagEmergency[phase]) {
            // Feed watchdog during emergency handling
            vTaskDelay(1 / portTICK_PERIOD_MS);
            
            debugPrintln("Phase is in emergency state");
            /*
            if (usingEstimatedConsumption == false) {
                if (consumptionDataReceived[phase] < emergencyHandled[phase]) {
                    debugPrintln("No new consumption data received since last emergency. Not taking actions.");
                    flagEmergency[phase] = false;
                    return;
                }
            }
            */

            //auto heaters
            debugPrintln("Emergency auto -> Off");
            HeaterItem::sortHeatersByPowerConsumption(autoHeaters, autoHeatersNum);
            for (uint8_t i=autoHeatersNum; (availablePower < 0) && (i-- > 0);) {
                HeaterItem* heater = autoHeaters[i];
                processHeatersOutput(heater);
                if (heater->getActualState() == true) {
                    heater->setActualState(false);
                    availablePower += heater->getPowerConsumption();
                    debugPrint("turned OFF. Not enough power.");
                }
                debugPrintln();
            }
            //manual heaters
            debugPrintln("Emergency manual -> Off");
            HeaterItem::sortHeatersByPowerConsumption(manualHeaters, manualHeatersNum);
            for (uint8_t i=manualHeatersNum; (availablePower < 0) && (i-- > 0);) {
                HeaterItem* heater = manualHeaters[i];
                processHeatersOutput(heater);
                if (heater->getActualState() == true) {
                    heater->setActualState(false);
                    availablePower += heater->getPowerConsumption();
                    debugPrint("turned OFF. Not enough power.");
                }
                debugPrintln();
            }
            emergencyHandled[phase] = millis();
            flagEmergency[phase] = false;
            debugPrint("Emergency handled. Available power: ");debugPrintln(availablePower);
            //continue;
        }

        if (availablePower < 0) {
            debugPrintln("Available power is below 0. Makes no sense to continue.");
            continue;
        }

        //turn on
        //manual heaters
        debugPrintln("Manual -> On");
        for (uint8_t i=0; i<manualHeatersNum; i++) {
            HeaterItem* heater = manualHeaters[i];
            processHeatersOutput(heater);
            if (heater->getWantsOn() == true && heater->getActualState() == false) {
                if (heater->getPowerConsumption() < availablePower) {
                    heater->setActualState(true);
                    availablePower -= heater->getPowerConsumption();
                    debugPrint("turned ON by user.");
                } else {
                    debugPrint("failed to turn ON. Not enough power.");
                }
            }
            debugPrintln();
        }
        //auto heaters
        debugPrintln("Auto -> On");
        for (uint8_t i=0; i<autoHeatersNum; i++) {
            HeaterItem* heater = autoHeaters[i];
            processHeatersOutput(heater);
            if (heater->getWantsOn() == true && heater->getActualState() == false) {
                if (heater->getPowerConsumption() < availablePower) {
                    heater->setActualState(true);
                    availablePower -= heater->getPowerConsumption();
                    debugPrint("turned ON.");
                } else {
                    debugPrint("failed to turn ON. Not enough power.");
                }
            } else {
                debugPrint("Nothing to do.");
            }
            debugPrintln();
        }
    }
}

uint16_t calculateHeatersConsumption(uint8_t phase) {
    uint16_t consumption = 0;
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if (heaterItems[i].getPhase() == phase + 1 && heaterItems[i].getIsEnabled() == true && heaterItems[i].getActualState() == true) {
            consumption += heaterItems[i].getPowerConsumption();
        }
    }
    return consumption;
}

void taskSystem(void* pvParameters) {
    while(true) {
        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
            debugPrint(">S>"); debugStack();
            ElegantOTA.loop();
            if (flagRestartNow) {
                // Gracefully disconnect MQTT
                if (mqttClient.connected()) {
                    mqttClient.disconnect();
                }
                
                // Gracefully close TCP connection
                if (tcpClient.connected()) {
                    tcpClient.stop();   // Initiate TCP close (FIN)
                }
                
                // Wait for TCP close handshake to complete
                // TCP requires FIN/ACK/FIN/ACK sequence
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                
                ESP.restart();
            }
            if (mqttClient.connected()) {
                mqttClient.loop();
            }
            else {
                mqttConnect();
            }
            // Process heaters immediately if MQTT command requested it
            if (flagProcessHeatersNow) {
                flagProcessHeatersNow = false;
                processHeaters();
            }
            debugPrint("<S<"); debugStack();
            xSemaphoreGive(mutex);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void taskMain(void* pvParameters) {
    while(true) {
        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
            debugPrint(">M>"); debugStack();
            requestTemperatures();
            vTaskDelay(READ_SENSORS_DELAY / portTICK_PERIOD_MS);
            readTemperatures();
            processHeaters();
            debugPrint("<M<"); debugStack();
            xSemaphoreGive(mutex);
        }
        vTaskDelay(TEMPERATURE_READ_INTERVAL / portTICK_PERIOD_MS);
    }
}

void initNTP() {
    debugPrint("Initializing NTP with server: "); debugPrintln(settings.ntpServer);
    long gmtOffsetSec = settings.gmtOffsetHours * 3600;
    int daylightOffsetSec = settings.daylightOffsetHours * 3600;
    configTime(gmtOffsetSec, daylightOffsetSec, settings.ntpServer.c_str());
}

String getFormattedTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "No time sync";
    }
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

uint32_t getNextBootNumber() {
    if (!LittleFS.exists(REBOOT_LOG_FILE)) {
        return 1;
    }
    
    File file = LittleFS.open(REBOOT_LOG_FILE, FILE_READ);
    if (!file) {
        return 1;
    }
    
    // Read the last line to get the last boot number
    String lastLine = "";
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) {
            lastLine = line;
        }
    }
    file.close();
    
    if (lastLine.length() == 0) {
        return 1;
    }
    
    // Parse boot number from format "N. [timestamp] reason"
    int dotIndex = lastLine.indexOf('.');
    if (dotIndex > 0) {
        uint32_t lastBootNum = lastLine.substring(0, dotIndex).toInt();
        return lastBootNum + 1;
    }
    
    return 1;
}

void appendRebootEntry(const String& timestamp, uint32_t bootNum, const char* reason) {
    File file = LittleFS.open(REBOOT_LOG_FILE, FILE_APPEND);
    if (!file) {
        debugPrintln("Failed to open reboot log for appending");
        return;
    }
    String entry = String(bootNum) + ". [" + timestamp + "] " + reason;
    file.println(entry);
    file.close();
    debugPrint("Reboot entry added: "); debugPrintln(entry);
}

void trimRebootHistory() {
    if (!LittleFS.exists(REBOOT_LOG_FILE)) {
        return;
    }
    
    File file = LittleFS.open(REBOOT_LOG_FILE, FILE_READ);
    if (!file) {
        return;
    }
    
    // Read all lines
    String lines[MAX_REBOOT_HISTORY_ENTRIES + 10]; // Buffer for extra entries
    int lineCount = 0;
    while (file.available() && lineCount < (MAX_REBOOT_HISTORY_ENTRIES + 10)) {
        lines[lineCount] = file.readStringUntil('\n');
        if (lines[lineCount].length() > 0) {
            lineCount++;
        }
    }
    file.close();
    
    // If we have more than MAX_REBOOT_HISTORY_ENTRIES, keep only the last MAX_REBOOT_HISTORY_ENTRIES
    if (lineCount > MAX_REBOOT_HISTORY_ENTRIES) {
        debugPrint("Trimming reboot history from "); debugPrint(lineCount);
        debugPrint(" to "); debugPrintln(MAX_REBOOT_HISTORY_ENTRIES);
        
        file = LittleFS.open(REBOOT_LOG_FILE, FILE_WRITE);
        if (!file) {
            debugPrintln("Failed to open reboot log for trimming");
            return;
        }
        
        int startIndex = lineCount - MAX_REBOOT_HISTORY_ENTRIES;
        for (int i = startIndex; i < lineCount; i++) {
            file.println(lines[i]);
        }
        file.close();
    }
}

void manageRebootHistory(uint32_t bootNum, const char* reason) {
    String timestamp = getFormattedTimestamp();
    appendRebootEntry(timestamp, bootNum, reason);
    trimRebootHistory();
}

const char* getResetReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "Unknown";
        case ESP_RST_POWERON:    return "Power-on reset";
        case ESP_RST_EXT:        return "External pin reset";
        case ESP_RST_SW:         return "Software reset";
        case ESP_RST_PANIC:      return "Exception/panic";
        case ESP_RST_INT_WDT:    return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Task watchdog";
        case ESP_RST_WDT:        return "Other watchdog";
        case ESP_RST_DEEPSLEEP:  return "Deep sleep wake-up";
        case ESP_RST_BROWNOUT:   return "Brownout reset";
        case ESP_RST_SDIO:       return "SDIO reset";
        default:                 return "Unknown";
    }
}

void setup()
{
    pinMode(ETHERNET_LED, OUTPUT);
    pinMode(MQTT_LED, OUTPUT);
    pinMode(ONEWIRE_LED, OUTPUT);
    pinMode(SENSOR_PIN, OUTPUT);
    pinMode(LS_DATA, OUTPUT);
    pinMode(LS_CLK, OUTPUT);
    pinMode(LS_STB, OUTPUT);
    pinMode(LS_OE, OUTPUT);

    updateOutputs(0);
    digitalWrite(LS_OE, HIGH); //enable output pins on shift register

    ethernetLed(LOW);
    mqttLed(LOW);
    oneWireLed(LOW);

    updateOutputs(0);

    iTimer.attachInterruptInterval(ITIMER_INTERVAL_MS * 1000, TimerHandler);

    ISR_Timer.setInterval(LED_BLINK_FAST, isrTimerCallbacks[TIMER_NUM_MQTT_LED_BLINK]);
    ISR_Timer.setInterval(LED_BLINK_FAST, isrTimerCallbacks[TIMER_NUM_ETHERNET_LED_BLINK]);
    ISR_Timer.setInterval(LED_BLINK_FAST, isrTimerCallbacks[TIMER_NUM_ONEWIRE_LED_BLINK]);
    ISR_Timer.disableAll();

    Serial.begin(115200);

    //start ethernet
    ISR_Timer.enable(TIMER_NUM_ETHERNET_LED_BLINK);
    WiFi.onEvent(WiFiEvent);
    ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    if (!LittleFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    //init settings
    loadSettings(settings);

    if (settings.debugTcp) {
        auto now = millis();
        while(millis() - now < 2000) {
            if (tcpConnect()) {
                break;
            }
        }
    }
    debugPrintln();debugPrint("HeatingController32 ");debugPrint(VERSION_SHORT);debugPrintln(" starting...");
    debugPrintln("Debug output enabled");
    
    // Get reboot reason
    const char* resetReason = getResetReason();
    debugPrint("Last reboot reason: "); debugPrintln(resetReason);
    
    // Get next boot number from reboot log
    uint32_t bootNumber = getNextBootNumber();
    debugPrint("Boot #"); debugPrintln(bootNumber);
    
    // Initialize NTP
    initNTP();
    
    // Wait for time sync (non-blocking with timeout)
    debugPrint("Waiting for NTP time sync");
    unsigned long ntpStart = millis();
    bool timeSynced = false;
    while (millis() - ntpStart < 3000) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            timeSynced = true;
            break;
        }
        debugPrint(".");
        delay(500);
    }
    debugPrintln();
    if (timeSynced) {
        debugPrint("Time synced: "); debugPrintln(getFormattedTimestamp());
    } else {
        debugPrintln("NTP sync timeout - logging with 'No time sync'");
    }
    
    // Manage reboot history
    manageRebootHistory(bootNumber, resetReason);
    
    debugPrintln();

    debugPrintln("Initializing with settings:");
    debugPrint("Hysteresis: "); debugPrintln(settings.hysteresis);
    debugPrint("MQTT url: "); debugPrintln(settings.mqttUrl);
    debugPrint("Mqtt port: "); debugPrintln(settings.mqttPort);
    debugPrint("TCP debug url: "); debugPrintln(settings.tcpUrl);
    debugPrint("TCP debug port: "); debugPrintln(settings.tcpPort);
    debugPrint("Serial debug: "); debugPrintln(settings.debugSerial);
    debugPrint("TCP debug: "); debugPrintln(settings.debugTcp);
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        debugPrint("Phase ");debugPrint(i); debugPrint(": consumption limit: ");debugPrintln(settings.consumptionLimit[i]);
    }
    debugPrintln();

    ISR_Timer.enable(TIMER_NUM_ONEWIRE_LED_BLINK);
    sensors.begin();
    sensorsCount = sensors.getDS18Count();
    ISR_Timer.disable(TIMER_NUM_ONEWIRE_LED_BLINK);
#ifdef BLINK_ONEWIRE
    if (sensorsCount > 0) {
        oneWireBlinkDetectedSensors(sensorsCount);
    }
#endif

    debugPrint("Detected sensors: "); debugPrintDec(sensorsCount); debugPrintln();
    for (uint8_t i = 0; i < sensorsCount; i++) {
        sensors.getAddress(sensorAddresses[i], i);
        String sensorAddress;
        byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensorAddress);
        debugPrint(sensorAddress);
        debugPrint(" : ");
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
        debugPrint(temperatures[i]);
        debugPrintln();
    }
    debugPrintln();

    server.on("/default.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/default.css", "text/css");
    });
    server.on("/jquery-3.6.1.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/jquery-3.6.1.min.js", "text/javascript");
    });
    server.on("/jszip-utils.ie.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/jszip-utils.ie.min.js", "text/javascript");
    });
    server.on("/jszip-utils.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/jszip-utils.min.js", "text/javascript");
    });
    server.on("/jszip.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/jszip.min.js", "text/javascript");
    });
    server.on("/filesaver.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/filesaver.min.js", "text/javascript");
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/main.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/control.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/sensors.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/backup", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/backup.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/rebooting.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/rebooting.html", "text/html");
    });
    server.on("/settings", HTTP_POST, processSettingsForm);
    server.on("/control", HTTP_POST, processControlForm);
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* request) {
        reboot(request);
    });
    server.on("/reboot.log", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists(REBOOT_LOG_FILE)) {
            request->send(LittleFS, REBOOT_LOG_FILE, "text/plain");
        } else {
            request->send(200, "text/plain", "No reboot history available");
        }
    });

    server.on("/files", HTTP_GET, [](AsyncWebServerRequest* request) {
        String html;
        File root = LittleFS.open("/");
        File file = root.openNextFile();
 
        while(file){
            html += "<a href=\"";
            html += file.name();
            html += "\">";
            html += file.name();
            html += "</a><br>";
            file = root.openNextFile();
        }
        request->send(200, "text/html", html);
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        int pos = request->url().lastIndexOf("/");
        String filename = request->url().substring(pos);
        request->send(LittleFS, filename, "text/plain");
    });

    ElegantOTA.onStart(onOtaStart);
    ElegantOTA.onEnd(onOtaEnd);
    ElegantOTA.begin(&server);
    server.begin();

    // Wait 1 minute to allow OTA firmware update in case board crashes after starting tasks
    debugPrintln("Upload firmware now...");
    auto now = millis();
    while(millis() - now < 60000) {
        ElegantOTA.loop();
    }

    //init heaterItems
    initHeaters(); //will also fill unconnected sensors
    
    for (uint8_t i=0; i<sensorsCount; i++) {
        if (!checkSensorConfigured(&sensorAddresses[i])) {
            String sensor;
            byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensor);
            debugPrint("Sensor ");
            debugPrint(sensor);
            debugPrintln(" is connected but not configured.");
        }
    }

    mqttConnect();

    //wait 5 seconds to get energy meter data from mqtt
    now = millis();
    while (millis()-now < 5000) {
        if (mqttClient.connected())
            mqttClient.loop();
        yield();
        if (flagConsumptionDataReceived)
            break;
    }

    debugPrintln("Starting tasks...");
    mutex = xSemaphoreCreateMutex();

    //stack size calculation based on empirical data
    xTaskCreate(taskSystem, "System", 12288, NULL, 1, &hndlSystem);  // Increased for external sensor MQTT handling
    xTaskCreate(taskMain, "Main", 4096, NULL, 1, &hndlMain);
}

void loop() {
}