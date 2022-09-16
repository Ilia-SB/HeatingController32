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

WiFiClient ethClient;
static bool ethConnected = false;

AsyncWebServer server(80);

PubSubClient mqttClient(ethClient);

OneWire oneWire(SENSOR);
DallasTemperature sensors(&oneWire);

byte outputs = 0;

uint16_t currentConsumption[3];

uint8_t sensorsCount;
DeviceAddress sensorAddresses[16];
float temperatures[16];

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
#define TIMER_NUM_REQUEST_TEMPERATURES   3


#define NUMBER_OF_ISR_TIMERS 4 //number of previous defines

void mqttLedBlink(void);
void ethernetLedBlink(void);
void oneWireLedBlink(void);
void oneWireLedOff(void);
void requestTemperaturesISR(void);
void requestTemperatures(void);
void readTemperaturesISR(void);
void readTemperatures(void);

irqCallback isrTimerCallbacks[NUMBER_OF_ISR_TIMERS] = {
    mqttLedBlink,
    ethernetLedBlink,
    oneWireLedBlink,
    requestTemperaturesISR
};

volatile bool flagRequestTemperatures = false;
volatile bool flagReadTemperatures = false;

void ethernetLed(uint8_t);
void mqttLed(uint8_t);
void oneWireLed(uint8_t);
void updateOutputs(uint16_t);
void setPorts(boolean[]);
void processCommand(char*, char*, char*);
void mqttCallback(char*, byte*, const unsigned int);
bool mqttConnect(void);
void WiFiEvent(WiFiEvent_t);
String webServerPlaceholderProcessor(const String&);
void oneWireBlinkDetectedSensors(uint8_t);
void setDefaultSettings(Settings&);
void setDefaults(HeaterItem&);
void itemToJson(HeaterItem&, StaticJsonDocument<JSON_DOCUMENT_SIZE>&);
void saveSettings(Settings&);
void loadSettings(Settings&);
void saveState(HeaterItem&);
void loadState(HeaterItem&);
void loadState(HeaterItem&, uint8_t);
void processSettingsForm(AsyncWebServerRequest*);
void reportHeatersState(void);
void reportTemperatures(void);
void getConsumptionData(const char*);


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
        char idx = phase + 1 + 48; //convert to ascii
        strncat(key, &idx, 1);
        DEBUG_PRINT(key); DEBUG_PRINT(" ");
        currentConsumption[phase] = (uint16_t)(doc[key].as<float>() * 1000);
        DEBUG_PRINTLN(currentConsumption[phase]);
    }
}

void processCommand(char* item, char* command, char* payload) {
    char statusTopic[64];
    char val[32];

    if (strcasecmp("settings", item) == 0) {
        strcpy(statusTopic, STATUS_TOPIC);
        strcat(statusTopic, "/settings/");
        
        if (strcasecmp(command, SET_HYSTERESIS) == 0) {
            if (settings.setHysteresis(payload)) {
                settings.getHysteresisCStr(val);
                strcat(statusTopic, SET_HYSTERESIS);
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
            
            if (strcasecmp(_command, SET_CONSUMPTION_LIMIT) == 0) {
                if (settings.setConsumptionLimit(payload, phase)) {
                    settings.getConsumptionLimitCStr(val, phase);
                    strcat(statusTopic, SET_CONSUMPTION_LIMIT);
                    mqttClient.publish(statusTopic, val, false);
                    saveSettings(settings);
                }
            }
        }
        ESP.restart();
        return;
    }

    uint8_t heaterNum = 0;
    bool found = false;
    for(uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if (strcasecmp(heaterItems[i].subtopic.c_str(), item) == 0) {
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
    strcat(statusTopic, heaterItems[heaterNum].subtopic.c_str());
    strcat(statusTopic, "/");

    HeaterItem* heater = &heaterItems[heaterNum];

    if (strcasecmp(command, SET_IS_AUTO) == 0) {
        if (heater->setIsAuto(payload)) {
            heater->getIsAutoCStr(val);
            strcat(statusTopic, SET_IS_AUTO);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_IS_ON) == 0) {
        if (heater->setIsOn(payload)) {
            heater->getIsOnCStr(val);
            strcat(statusTopic, SET_IS_ON);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_PRIORITY) == 0) {
        if (heater->setPriority(payload)) {
            heater->getPriorityCStr(val);
            strcat(statusTopic, SET_PRIORITY);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_TARGET_TEMPERATURE) == 0) {
        if (heater->setTargetTemperature(payload)) {
            heater->getTargetTemperatureCStr(val);
            strcat(statusTopic, SET_TARGET_TEMPERATURE);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_SENSOR) == 0) {
        DEBUG_PRINT("setSensor: "); DEBUG_PRINTLN(payload);
        if (heater->setSensor(payload)) {
            heater->getSensorCStr(val);
            strcat(statusTopic, SET_SENSOR);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_PORT) == 0) {
        if (heater->setPort(payload)) {
            heater->getPortCStr(val);
            strcat(statusTopic, SET_PORT);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_TEMPERATURE_ADJUST) == 0) {
        if (heater->setTemperatureAdjust(payload)) {
            heater->getTemperatureAdjustCStr(val);
            strcat(statusTopic, SET_TEMPERATURE_ADJUST);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_CONSUMPTION) == 0) {
        if (heater->setConsumption(payload)) {
            heater->getConsumptionCStr(val);
            strcat(statusTopic, SET_CONSUMPTION);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
    if (strcasecmp(command, SET_IS_ENABLED) == 0) {
        if (heater->setIsEnaled(payload)) {
            heater->getIsEnabledCStr(val);
            strcat(statusTopic, SET_IS_ENABLED);
            mqttClient.publish(statusTopic, val, false);
            saveState(*heater);
        }
    }
}

void mqttCallback(char* topic, byte* payload, const unsigned int len) {
    if (len < MQTT_MAX_PACKET_SIZE) {
        payload[len] = '\0';
    }
    else {
        return;
    }

    char payloadCopy[len + 1];
    strcpy(payloadCopy, (char*)payload);
    strupr(payloadCopy);

    if (strcasecmp(topic, ENERGY_METER_TOPIC) == 0) {
        getConsumptionData(payloadCopy);
        return;
    }

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

bool mqttConnect() {
    if (!ethConnected) {
        mqttLed(LOW);
        return false;
    }
    ISR_Timer.enable(TIMER_NUM_MQTT_LED_BLINK);
    mqttClient.setServer(MQTT_URL, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    if (mqttClient.connect(HOSTNAME)) {
        DEBUG_PRINTLN("MQTT connected");
        mqttClient.subscribe(COMMAND_TOPIC.c_str());
        mqttClient.subscribe(ENERGY_METER_TOPIC);
        ISR_Timer.disable(TIMER_NUM_MQTT_LED_BLINK);
        mqttLed(HIGH);
        return true;
    }
    else {
        DEBUG_PRINTLN("MQTT connect failed");
        ISR_Timer.disable(TIMER_NUM_MQTT_LED_BLINK);
        mqttLed(LOW);
        return false;
    }
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
        ISR_Timer.changeInterval(TIMER_NUM_ETHERNET_LED_BLINK, LED_BLINK_MEDIUM);
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
            retValue += heaterItems[i].name;
            retValue += "</p><form style=\"color:#eaeaea;\" method=\"post\" action=\"/settings\"><fieldset id=\"item_";
            retValue += itemNum;
            retValue += "\" style=\"display: none;\">";
            retValue += "<input type=\"hidden\" name=\"item\" value=\"";
            retValue += String(i);
            retValue += "\">";
            retValue += "<div style=\"color:#eaeaea;text-align: left;\"><table><tbody><tr><td class=\"name\">Name</td><td class=\"value\"><input type=\"text\" name=\"name\" value=\"";
            retValue += heaterItems[i].name;
            retValue += "\"></td></tr><tr><td class=\"name\">Sensor</td><td class=\"value\"><select name=\"sensor\">";
            retValue += "<option value=\"";
            String setSensor;
            byteArrayToHexString(heaterItems[i].sensorAddress, SENSOR_ADDR_LEN, setSensor);
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
            retValue += heaterItems[i].subtopic;
            retValue += "\"></td></tr><tr><td class=\"name\">Port</td><td class=\"value\"><select name=\"port\">";
            for (uint8_t j=1; j<NUMBER_OF_HEATERS+1; j++) {
                retValue += "<option value=\"";
                retValue += String(j);
                retValue += "\"";
                if (j==heaterItems[i].port) {
                    retValue += " selected=\"selected\"";
                }
                retValue += ">";
                retValue += String(j);
                retValue += "</option>";
            }
            retValue += "</td></tr><tr><td class=\"name\">Phase</td><td class=\"value\"><select name=\"phase\">";
            for (uint8_t j=0; j<NUMBER_OF_PHASES; j++) {
                retValue += "<option value=\"";
                retValue += String(j);
                retValue += "\"";
                if (j==heaterItems[i].phase) {
                    retValue += " selected=\"selected\"";
                }
                retValue += ">";
                retValue += String(j);
                retValue += "</option>";
            }
            retValue += "</td></tr><tr><td class=\"name\">Consumption</td><td class=\"value\"><input type=\"text\" name=\"consumption\" value=\"";
            retValue += String(heaterItems[i].powerConsumption);
            retValue += "\"></td></tr><tr><td class=\"name\">Priority</td><td class=\"value\"><input type=\"text\" name=\"priority\" value=\"";
            retValue += String(heaterItems[i].priority);
            retValue += "\"></td></tr></tbody></table><button name=\"save\" type=\"submit\" class=\"bgrn\">Save</button></div></fieldset></form>";
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

void requestTemperaturesISR(void) {
    flagRequestTemperatures = true;
}

void requestTemperatures() {
    oneWireLed(HIGH);
    ISR_Timer.setTimeout(LED_BLINK_FAST, oneWireLedOff); //turn the led off for a while to indicate activity
    sensors.requestTemperatures();
    ISR_Timer.setTimeout(READ_SENSORS_DELAY, readTemperatures);
    flagRequestTemperatures = false;
}

void readTemperaturesISR() {
    flagReadTemperatures = true;
}

void readTemperatures() {
    for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
        float _temperature = sensors.getTempC(heaterItems[i].sensorAddress);
        //TODO: check the value for errors and report
        heaterItems[i].setTemperature(_temperature);
    }
    flagReadTemperatures = false;
    reportTemperatures();
}

void reportTemperatures() {
    //TODO: report to mqtt
}

void setDefaultSettings(Settings& settings) {
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        //settings.phases[i] = i;
        settings.consumptionLimit[i] = CONSUMPTION_LIMITS[i];
        settings.hysteresis = DEFAULT_HYSTERESIS;
    }
}

void setDefaults(HeaterItem& heaterItem) {
    heaterItem.name = "Heater " + String(heaterItem.address);
    String addr;
    heaterItem.getAddressString(addr, "%06d");
    heaterItem.subtopic = "item_" + addr;
    heaterItem.isEnabled = false;
    memset(heaterItem.sensorAddress, 0, SENSOR_ADDR_LEN);
    heaterItem.port = 0;
    heaterItem.phase = 1;
    heaterItem.isAuto = false;
    heaterItem.powerConsumption = 0;
    heaterItem.isOn = false;
    heaterItem.priority = 100;
    heaterItem.isConnected = false;
    heaterItem.setTargetTemperature(0.0f);
    heaterItem.setTemperatureAdjust(0.0f);
}

void itemToJson(HeaterItem& heaterItem, StaticJsonDocument<JSON_DOCUMENT_SIZE>& doc) {
    doc["name"] = heaterItem.name;
    doc["address"] = heaterItem.address;
    doc["subtopic"] = heaterItem.subtopic;
    doc["isEnabled"] = heaterItem.isEnabled;
    JsonArray sensorAddress = doc.createNestedArray("sensorAddress");
    for (uint8_t i=0; i<SENSOR_ADDR_LEN; i++) {
        sensorAddress.add(heaterItem.sensorAddress[i]);
    }
    char addr[3*SENSOR_ADDR_LEN];
    heaterItem.getSensorCStr(addr);
    doc["sensorAddressString"] = addr;
    doc["port"] = heaterItem.port;
    doc["phase"] = heaterItem.phase;
    doc["isAuto"] = heaterItem.isAuto;
    doc["powerConsumption"] = heaterItem.powerConsumption;
    doc["isOn"] = heaterItem.isOn;
    doc["priority"] = heaterItem.priority;
    doc["isConnected"] = heaterItem.isConnected;
    doc["targetTemperature"] = heaterItem.getTargetTemperature();
    doc["temperatureAdjust"] = heaterItem.getTemperatureAdjust();
}

void saveSettings(Settings& settings) {
    const char fileName[] = "/settings";
    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
    doc["hysteresis"] = settings.hysteresis;
    //JsonArray phases = doc.createNestedArray("phases");
    JsonArray consumptionLimit = doc.createNestedArray("consumptionLimit");
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        //phases.add(settings.phases[i]);
        consumptionLimit.add(settings.consumptionLimit[i]);
    }

#ifdef DEBUG
    DEBUG_PRINT("Saving settings to: "); DEBUG_PRINTLN(fileName);
    char output[JSON_DOCUMENT_SIZE_SETTINGS];
    serializeJson(doc, output);
    DEBUG_PRINTLN(output);
#endif
    serializeJson(doc, file);
    file.close();
}

void saveState(HeaterItem& heaterItem) {
    String fileName = "/item";
    fileName += String(heaterItem.address);

    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    itemToJson(heaterItem, doc);

#ifdef DEBUG
    DEBUG_PRINT("Saving settings to: "); DEBUG_PRINTLN(fileName);
    char output[JSON_DOCUMENT_SIZE];
    serializeJson(doc, output);
    DEBUG_PRINTLN(output);
#endif

    serializeJson(doc, file);
    file.close();
}

void loadSettings(Settings& settings) {
    const char fileName[] = "/settings";

    if (!SPIFFS.exists(fileName)) {
        setDefaultSettings(settings);
    }
    else {
        File file = SPIFFS.open(fileName, FILE_READ, true);

        StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
        deserializeJson(doc, file);

        settings.hysteresis = doc["hysteresis"].as<float>();
        //JsonArray phases = doc["phases"];
        JsonArray consumptionLimit = doc["consumptionLimit"];
        for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
            //settings.phases[i] = phases.getElement(i).as<uint8_t>();
            settings.consumptionLimit[i] = consumptionLimit.getElement(i).as<uint16_t>();
        }
    }
}

void loadState(HeaterItem& heaterItem) {
    String fileName = "/item";
    fileName += String(heaterItem.address);

    if(!SPIFFS.exists(fileName)) {
        setDefaults(heaterItem);
    }
    else {
        File file = SPIFFS.open(fileName, FILE_READ, true);    

        StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
        deserializeJson(doc, file);

        heaterItem.name = doc["name"].as<String>();
        heaterItem.address = doc["address"];
        heaterItem.subtopic = doc["subtopic"].as<String>();
        heaterItem.isEnabled = doc["isEnabled"];
        JsonArray sensorAddress = doc["sensorAddress"];
        for (uint8_t i=0; i<SENSOR_ADDR_LEN; i++) {
            heaterItem.sensorAddress[i] = sensorAddress.getElement(i).as<byte>();
        }
        heaterItem.port = doc["port"];
        heaterItem.phase = doc["phase"];
        heaterItem.isAuto = doc["isAuto"];
        heaterItem.powerConsumption = doc["powerConsumption"];
        heaterItem.isOn = doc["isOn"];
        heaterItem.priority = doc["priority"];
        heaterItem.isConnected = doc["isConnected"];
        heaterItem.setTargetTemperature(doc["targetTemperature"].as<float>());
        heaterItem.setTemperatureAdjust(doc["temperatureAdjust"].as<float>());
    }
}

void loadState(HeaterItem& heaterItem, uint8_t itemNumber) {
    heaterItem.address = itemNumber;
    loadState(heaterItem);
}

void processSettingsForm(AsyncWebServerRequest* request) {
    uint8_t itemNo=0;
    if (request->hasParam("item", true)) {
        String var = request->getParam("item", true)->value();
        itemNo = (uint8_t)(var.toInt());
    }
    else {
        return;
    }

    if (request->hasParam("name", true)) {
        heaterItems[itemNo].name = request->getParam("name", true)->value();
    }
    if (request->hasParam("sensor", true)) {
        int8_t sensorNum = (int8_t)(request->getParam("sensor", true)->value().toInt());
        if (sensorNum >=0) {
            memcpy(heaterItems[itemNo].sensorAddress, sensorAddresses[sensorNum], SENSOR_ADDR_LEN);
        }
    }
    if (request->hasParam("subtopic", true)) {
        heaterItems[itemNo].subtopic = request->getParam("subtopic", true)->value();
    }
    if (request->hasParam("port", true)) {
        heaterItems[itemNo].port = (uint8_t)(request->getParam("port", true)->value().toInt());
    }
    if (request->hasParam("phase", true)) {
        heaterItems[itemNo].phase = (uint8_t)(request->getParam("phase", true)->value().toInt());
    }
    if (request->hasParam("consumption", true)) {
        heaterItems[itemNo].powerConsumption = (uint16_t)(request->getParam("consumption", true)->value().toInt());
    }
    if (request->hasParam("priority", true)) {
        heaterItems[itemNo].priority = (uint8_t)(request->getParam("priority", true)->value().toInt());
    }

    saveState(heaterItems[itemNo]);
    request->send(SPIFFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
}

void reportHeatersState() {
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
        itemToJson(heaterItems[i], doc);
        String mqttTopic;
        mqttTopic += STATUS_TOPIC;
        mqttTopic += "/";
        mqttTopic += heaterItems[i].subtopic;
        mqttTopic += "/STATE";

        String mqttPayload;
        serializeJson(doc, mqttPayload);

        uint16_t maxPayloadSize = MQTT_MAX_PACKET_SIZE - MQTT_MAX_HEADER_SIZE - 2 - mqttTopic.length();
        if(mqttPayload.length() > maxPayloadSize) {
            mqttClient.beginPublish(mqttTopic.c_str(), mqttPayload.length(), false);
            for (uint16_t j=0; j<mqttPayload.length(); j++) {
                mqttClient.write((uint8_t)(mqttPayload.c_str()[j]));
            }
            mqttClient.endPublish();
        }
        else {
            mqttClient.publish(mqttTopic.c_str(), mqttPayload.c_str(), false);
        }
    }
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
    ISR_Timer.setInterval(TEMPERATURE_READ_INTERVAL, isrTimerCallbacks[TIMER_NUM_REQUEST_TEMPERATURES]);
    ISR_Timer.disableAll();

    Serial.begin(115200);
    DEBUG_PRINTLN("HeatingController32 V1.0 starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    //init settings
    loadSettings(settings);
    DEBUG_PRINTLN("Initializing with settings:");
    DEBUG_PRINT("Hysteresis: "); DEBUG_PRINTLN(settings.hysteresis);
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        DEBUG_PRINT("Phase ");DEBUG_PRINT(i); DEBUG_PRINT(": consumption limit: ");DEBUG_PRINTLN(settings.consumptionLimit[i]);
    }
    DEBUG_PRINTLN();

    //init heaterItems
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        loadState(heaterItems[i], i);
    }

    ISR_Timer.enable(TIMER_NUM_ONEWIRE_LED_BLINK);
    sensors.begin();
    sensorsCount = sensors.getDS18Count();
    ISR_Timer.disable(TIMER_NUM_ONEWIRE_LED_BLINK);
    if (sensorsCount > 0) {
        oneWireBlinkDetectedSensors(sensorsCount);
    }

    DEBUG_PRINT("Detected sensors: "); DEBUG_PRINTDEC(sensorsCount); DEBUG_PRINTLN();
    for (uint8_t i = 0; i < sensorsCount; i++) {
        sensors.getAddress(sensorAddresses[i], i);
        String sensorAddress;
        byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensorAddress);
        DEBUG_PRINT(sensorAddress);
        DEBUG_PRINT(" : ");
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
        DEBUG_PRINT(temperatures[i]); DEBUG_PRINTLN();
    }
    DEBUG_PRINTLN();
 
    ISR_Timer.enable(TIMER_NUM_ETHERNET_LED_BLINK);
    WiFi.onEvent(WiFiEvent);
    ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        String html;
        File root = SPIFFS.open("/");
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
    server.on("/default.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/default.css", "text/css");
    });
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/settings", HTTP_POST, processSettingsForm);

    server.onNotFound([](AsyncWebServerRequest* request) {
        int pos = request->url().lastIndexOf("/");
        String filename = request->url().substring(pos);
        request->send(SPIFFS, filename, "text/plain");
    });

    AsyncElegantOTA.begin(&server);
    server.begin();

    ISR_Timer.enable(TIMER_NUM_REQUEST_TEMPERATURES);

    reportHeatersState();
}

// Add the main program code into the continuous loop() function
void loop()
{
    if (mqttClient.connected()) {
        mqttClient.loop();
        //reportHeatersState();
    }
    else {
        mqttConnect();
    }

    if (flagRequestTemperatures)
        requestTemperatures();

    if (flagReadTemperatures)
        readTemperatures();
}