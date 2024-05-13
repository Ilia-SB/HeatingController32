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

TaskHandle_t hndlSystem;
TaskHandle_t hndlMain;
TaskHandle_t hndlEmergency;
TaskHandle_t hndlProcessHeaters;

void taskSystem(void* pvParameters);
void taskEmergency(void* pvParameters);
void taskMain(void* pvParameters);
void taskProcessHeaters(void* pvParameters);

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

uint8_t unconfiguredSesorsCount = 0;
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

bool consumptionDataRecieved = false;
unsigned long consumptionDataReceived[NUMBER_OF_PHASES] = {0ul,0ul,0ul};
unsigned long emergencyHandled[NUMBER_OF_PHASES] = {0ul,0ul,0ul};

bool heatersInitialized = false;

bool flagRestartNow = false;


void ethernetLed(uint8_t);
void mqttLed(uint8_t);
void oneWireLed(uint8_t);
void updateOutputs(uint16_t);
void setPorts(boolean[]);
void processCommand(char*, char*, char*);
void mqttCallback(char*, byte*, const unsigned int);
bool mqttConnect(void);
bool tcpConnect(void);
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
        //DEBUG_PRINT(key); DEBUG_PRINT(" ");
        if (doc.containsKey(key)) {
            consumptionDataRecieved = true;
            currentConsumption[phase] = (uint16_t)(doc[key].as<float>() * 1000);
            consumptionDataReceived[phase] = millis();
            bool emergency = false;
            //DEBUG_PRINTLN(currentConsumption[phase]);
            if (currentConsumption[phase] > settings.consumptionLimit[phase]) {
                flagEmergency[phase] = true;
                emergency = true;
            } else {
                flagEmergency[phase] = false;
            }
            if(emergency) {
                vTaskResume(hndlEmergency);
            }
        }
    }
}

void processCommand(char* item, char* command, char* payload) {
    char statusTopic[64];
    char val[32];

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
        ESP.restart();
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

    if (strcasecmp(command, IS_AUTO) == 0) {
        heater->setIsAuto(payload);
    }
    if (strcasecmp(command, IS_ON) == 0) {
        if (heater->getIsAuto() == false)
            heater->setWantsOn(payload);
    }
    if (strcasecmp(command, PRIORITY) == 0) {
        heater->setPriority(payload);
    }
    if (strcasecmp(command, TARGET_TEMPERATURE) == 0) {
        heater->setTargetTemperature(payload);
    }
    if (strcasecmp(command, SENSOR) == 0) {
        heater->setSensorAddress(payload);
    }
    if (strcasecmp(command, PORT) == 0) {
        heater->setPort(payload);
    }
    if (strcasecmp(command, PHASE) == 0) {
        heater->setPhase(payload);
    }
    if (strcasecmp(command, TEMPERATURE_ADJUST) == 0) {
        heater->setTemperatureAdjust(payload);
    }
    if (strcasecmp(command, AUX_ADJUST) == 0) {
        heater->setAuxAdjust(payload);
        heater->setUsesAuxAdjust(true);
    }
    if (strcasecmp(command, CONSUMPTION) == 0) {
        heater->setPowerConsumption(payload);
    }
    if (strcasecmp(command, IS_ENABLED) == 0) {
        heater->setIsEnaled(payload);
    }

    heater->setIsConnected(checkSensorConnected(*heater));
    sanityCheckHeater(*heater);
    saveState(*heater);
    reportHeaterState(*heater);
    vTaskResume(hndlProcessHeaters);
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

    //Energy meter
    if (strcasecmp(topic, ENERGY_METER_TOPIC) == 0) {
        getConsumptionData(payloadCopy);
        return;
    }

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

bool tcpConnect() {
    if (!ethConnected) {
        return false;
    }

    if (tcpClient.connect(settings.tcpUrl.c_str(), settings.tcpPort)) {
        Serial.println("TCP client connected.");
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
        DEBUG_PRINTLN("MQTT connected");
        mqttClient.subscribe(COMMAND_TOPIC.c_str());
        mqttClient.subscribe(ENERGY_METER_TOPIC);
        mqttClient.publish(LWT_TOPIC, "Online", true);
        ISR_Timer.disable(TIMER_NUM_MQTT_LED_BLINK);
        mqttLed(HIGH);
        if (heatersInitialized)
            reportHeatersState();
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
            retValue += " | Aux: ";
            retValue += String(heaterItems[i].getAuxAdjust());
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
        retValue += "<tr><td class=\"name\">TCP url</td><td class=\"value\"><input type=\"text\" name=\"tcpUrl\" value=\"";
        retValue += settings.tcpUrl;
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">TCP port</td><td class=\"value\"><input type=\"text\" name=\"tcpPort\" value=\"";
        retValue += String(settings.tcpPort);
        retValue += "\"></td></tr>";
        retValue += "<tr><td class=\"name\">Use TCP</td><td class=\"value\"><input type=\"checkbox\" name=\"useTcp\"";
        retValue += settings.useTcp?" checked":"";
        retValue += "></td></tr>";
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
            if (SPIFFS.exists(fileName)) {
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
        for (uint8_t i=0; i<unconfiguredSesorsCount; i++) {
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
    DEBUG_PRINTLN("Requesting temperatures...");
    oneWireLed(HIGH);
    ISR_Timer.setTimeout(LED_BLINK_FAST, oneWireLedOff); //turn the led off for a while to indicate activity
    sensors.requestTemperatures();
}

void readTemperatures() {
    DEBUG_PRINTLN("Reading temperatures...");
    for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
        if (heaterItems[i].getIsConnected() == true) {
            float _temperature = sensors.getTempC(heaterItems[i].getSensorAddress());
            if ((int)_temperature != HeaterItem::SENSOR_NOT_CONNECTED && (int)_temperature != HeaterItem::SENSOR_READ_ERROR) {
                heaterItems[i].setTemperature(_temperature);
                DEBUG_PRINT(heaterItems[i].getName());DEBUG_PRINT(": ");DEBUG_PRINTLN(_temperature);
            } else {
                DEBUG_PRINT("Error reading temperature for item ");DEBUG_PRINTLN(heaterItems[i].getName());
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
                    DEBUG_PRINT("Heater ");DEBUG_PRINT(heaterItems[i].getName());DEBUG_PRINT(": number of temperature read errors since last report: ");DEBUG_PRINTLN(heaterItems[i].getTempReadErrors());
                    heaterItems[i].setTempReadErrors(0); //reset counter
                }
            }
        } else {
            heaterItems[i].setTemperature(0);
        }

    }
    reportTemperatures();
    vTaskResume(hndlProcessHeaters);
}

void setDefaultSettings(Settings& settings) {
    settings.hysteresis = DEFAULT_HYSTERESIS;
    settings.mqttUrl = MQTT_URL;
    settings.mqttPort = MQTT_PORT;
    settings.tcpUrl = TCP_URL;
    settings.tcpPort = TCP_PORT;
    settings.useTcp = false;
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
    heaterItem.setAuxAdjust(0.0f);
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
    if (forReport)
        doc["auxAdjust"] = heaterItem.getAuxAdjust();
        doc["sensorTemperature"] = heaterItem.getSensorTemperature();
        doc["temperature"] = heaterItem.getTemperature();
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
    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
    doc[SETTINGS_SETTINGS_VERSION] = SETTINGS_VERSION;
    doc[SETTINGS_HYSTERESIS] = settings.hysteresis;
    doc[SETTINGS_MQTT_URL] = settings.mqttUrl;
    doc[SETTINGS_MQTT_PORT] = settings.mqttPort;
    doc[SETTINGS_TCP_URL] = settings.tcpUrl;
    doc[SETTINGS_TCP_PORT] = settings.tcpPort;
    doc[SETTINGS_USE_TCP] = settings.useTcp;
    JsonArray consumptionLimit = doc.createNestedArray("consumptionLimit");
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
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
    String fileName;
    getItemFilename(heaterItem.getAddress(), fileName);
    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    itemToJson(heaterItem, doc, false);

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
    String fileName;
    getSettingsFilename(fileName);
    if (!SPIFFS.exists(fileName)) {
        setDefaultSettings(settings);
    }
    else {
        File file = SPIFFS.open(fileName, FILE_READ, true);

        StaticJsonDocument<JSON_DOCUMENT_SIZE_SETTINGS> doc;
        deserializeJson(doc, file);

        if (!doc.containsKey(SETTINGS_SETTINGS_VERSION) || doc[SETTINGS_SETTINGS_VERSION] != SETTINGS_VERSION) {
            DEBUG_PRINTLN("Settings version mismatch.");
            deleteSettings();
        }

        settings.hysteresis = doc[SETTINGS_HYSTERESIS].as<float>();
        settings.mqttUrl = doc[SETTINGS_MQTT_URL].as<String>();
        settings.mqttPort = doc[SETTINGS_MQTT_PORT].as<uint16_t>();
        settings.tcpUrl = doc[SETTINGS_TCP_URL].as<String>();
        settings.tcpPort = doc[SETTINGS_TCP_PORT].as<uint16_t>();
        settings.useTcp = doc[SETTINGS_USE_TCP].as<bool>();
        JsonArray consumptionLimit = doc[SETTINGS_CONSUMPTION_LIMIT];
        for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
            settings.consumptionLimit[i] = consumptionLimit.getElement(i).as<uint16_t>();
        }
    }
}

void deleteSettings() {
    DEBUG_PRINTLN("Deleting global settings.");
    String fileName;
    getSettingsFilename(fileName);
    SPIFFS.remove(fileName);
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        String fileName;
        getItemFilename(i, fileName);
        if (SPIFFS.exists(fileName)) {
            DEBUG_PRINT("Deleting settings for ");DEBUG_PRINT(fileName);DEBUG_PRINTLN(".");
            SPIFFS.remove(fileName);
        }
    }
}

void loadState(HeaterItem& heaterItem) {
    String fileName;
    getItemFilename(heaterItem.getAddress(), fileName);
    if(!SPIFFS.exists(fileName)) {
        setDefaults(heaterItem);
    }
    else {
        File file = SPIFFS.open(fileName, FILE_READ, true);    

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
        if (request->hasParam(SETTINGS_USE_TCP, true)) { //if checkbox checked, request has param, otherwise not. no need to check the value
            settings.useTcp = true;
        } else {
            settings.useTcp = false;
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

    saveState(heaterItems[itemNo]);
    request->send(SPIFFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
    vTaskResume(hndlProcessHeaters);
}

void reboot(AsyncWebServerRequest* request) {
    //TODO: fancy javascript for redirect
    request->send(SPIFFS, "/rebooting.html");
    flagRestartNow = true;
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
    request->send(SPIFFS, "/control.html", String(), false, webServerPlaceholderProcessor);
    vTaskResume(hndlProcessHeaters);
}

void reportTemperatures() {
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        if(heaterItems[i].getIsEnabled() == true) {
            reportHeaterState(heaterItems[i]);
        }
    }
}

void reportHeatersState() {
    DEBUG_PRINTLN("Reporting heaters state...");
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
    DEBUG_PRINTLN("Initializing heaters...");
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        heaterItems[i].setAddress(i);
        initHeater(heaterItems[i]);
    }
    
    requestTemperatures();
    delay(READ_SENSORS_DELAY);
    readTemperatures();
    reportHeatersState();
    heatersInitialized = true;
    DEBUG_PRINTLN("Heaters initialized");
}

void initHeater(HeaterItem& heater) {
    heater.setUsesAuxAdjust(false);
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
    if (!heater.getIsConnected() && !heater.getUsesAuxAdjust()) {
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
    unconfiguredSensors[unconfiguredSesorsCount++] = sensor;
    return false;
}

void processHeatersOutput(HeaterItem* heater) {
    DEBUG_PRINT("| ");DEBUG_PRINT(heater->getName());DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getIsAuto()?"Auto  ":"Manual");DEBUG_PRINT("\t| ");DEBUG_PRINT(String(heater->getTemperature(),2));
    DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getTargetTemperature());DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getDelta());DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getPowerConsumption());
    DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getActualState()?"On":"Off");DEBUG_PRINT("\t| ");DEBUG_PRINT(heater->getWantsOn()?"Yes":"No");DEBUG_PRINT("\t| ");
}

void processHeaters() {
    if (flagRestartNow)
        return;
    DEBUG_PRINTLN("Processing heaters...");
    for (uint8_t phase=0; phase<NUMBER_OF_PHASES; phase++) {
        DEBUG_PRINT("Phase "); DEBUG_PRINT(phase + 1);
        int16_t availablePower = 0;
        bool usingEstimatedConsumption = false;
        if (millis() - consumptionDataReceived[phase] < 5000) { //if data from the energy meter is not older than 5 sec.
            DEBUG_PRINT(". Using measured power consumption. ");
            availablePower = settings.consumptionLimit[phase] - currentConsumption[phase];
            DEBUG_PRINT("Available power: ");DEBUG_PRINT(availablePower);DEBUG_PRINT(" = ");DEBUG_PRINT(settings.consumptionLimit[phase]);DEBUG_PRINT(" - ");DEBUG_PRINT(currentConsumption[phase]);
        } else {
            DEBUG_PRINT(". Using estimated power consumption (");DEBUG_PRINT(millis() - consumptionDataReceived[phase]);DEBUG_PRINT("ms since last power reading). ")
            availablePower = settings.consumptionLimit[phase] - calculateHeatersConsumption(phase);
            usingEstimatedConsumption = true;
            DEBUG_PRINT("Available power: ");DEBUG_PRINT(availablePower);DEBUG_PRINT(" = ");DEBUG_PRINT(settings.consumptionLimit[phase]);DEBUG_PRINT(" - ");DEBUG_PRINT(calculateHeatersConsumption(phase));
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
        
        DEBUG_PRINT(". Auto heaters count: ");DEBUG_PRINT(autoHeatersNum);DEBUG_PRINT(", manual heaters count: ");DEBUG_PRINTLN(manualHeatersNum);

        //turn off
        //manual heaters
        DEBUG_PRINTLN("Manual -> Off");
        for (uint8_t i=0; i<manualHeatersNum; i++) {
            HeaterItem* heater = manualHeaters[i];
            processHeatersOutput(heater);
            if (heater->getActualState() == true && heater->getWantsOn() == false) {
                heater->setActualState(false);
                availablePower += heater->getPowerConsumption();
                DEBUG_PRINT("turned OFF by user.");
            }
            DEBUG_PRINTLN();
        }
        //auto heaters
        DEBUG_PRINTLN("Auto -> Off");
        for (uint8_t i=0; i<autoHeatersNum; i++) {
            HeaterItem* heater = autoHeaters[i];
            processHeatersOutput(heater);
            if (heater->getActualState() == true && heater->getWantsOn() == false) {
                heater->setActualState(false);
                availablePower += heater->getPowerConsumption();
                DEBUG_PRINT("turned OFF. Target temp reached.");
            } else {
                DEBUG_PRINT("nothing to do.");
            }
            DEBUG_PRINTLN();
        }
        //emergency
        if (flagEmergency[phase]) {
            DEBUG_PRINTLN("Phase is in emergency state");
            /*
            if (usingEstimatedConsumption == false) {
                if (consumptionDataReceived[phase] < emergencyHandled[phase]) {
                    DEBUG_PRINTLN("No new consumption data received since last emergency. Not taking actions.");
                    flagEmergency[phase] = false;
                    return;
                }
            }
            */

            //auto heaters
            DEBUG_PRINTLN("Emergency auto -> Off");
            HeaterItem::sortHeatersByPowerConsumption(autoHeaters, autoHeatersNum);
            for (uint8_t i=autoHeatersNum; (availablePower < 0) && (i-- > 0);) {
                HeaterItem* heater = autoHeaters[i];
                processHeatersOutput(heater);
                if (heater->getActualState() == true) {
                    heater->setActualState(false);
                    availablePower += heater->getPowerConsumption();
                    DEBUG_PRINT("turned OFF. Not enough power.");
                }
                DEBUG_PRINTLN();
            }
            //manual heaters
            DEBUG_PRINTLN("Emergency manual -> Off");
            HeaterItem::sortHeatersByPowerConsumption(manualHeaters, manualHeatersNum);
            for (uint8_t i=manualHeatersNum; (availablePower < 0) && (i-- > 0);) {
                HeaterItem* heater = manualHeaters[i];
                processHeatersOutput(heater);
                if (heater->getActualState() == true) {
                    heater->setActualState(false);
                    availablePower += heater->getPowerConsumption();
                    DEBUG_PRINT("turned OFF. Not enough power.");
                }
                DEBUG_PRINTLN();
            }
            emergencyHandled[phase] = millis();
            flagEmergency[phase] = false;
            DEBUG_PRINT("Emergency handled. Available power: ");DEBUG_PRINTLN(availablePower);
            //continue;
        }

        if (availablePower < 0) {
            DEBUG_PRINTLN("Available power is below 0. Makes no sense to continue.");
            continue;
        }

        //turn on
        //manual heaters
        DEBUG_PRINTLN("Manual -> On");
        for (uint8_t i=0; i<manualHeatersNum; i++) {
            HeaterItem* heater = manualHeaters[i];
            processHeatersOutput(heater);
            if (heater->getWantsOn() == true && heater->getActualState() == false) {
                if (heater->getPowerConsumption() < availablePower) {
                    heater->setActualState(true);
                    availablePower -= heater->getPowerConsumption();
                    DEBUG_PRINT("turned ON by user.");
                } else {
                    DEBUG_PRINT("failed to turn ON. Not enough power.");
                }
            }
            DEBUG_PRINTLN();
        }
        //auto heaters
        DEBUG_PRINTLN("Auto -> On");
        for (uint8_t i=0; i<autoHeatersNum; i++) {
            HeaterItem* heater = autoHeaters[i];
            processHeatersOutput(heater);
            if (heater->getWantsOn() == true && heater->getActualState() == false) {
                if (heater->getPowerConsumption() < availablePower) {
                    heater->setActualState(true);
                    availablePower -= heater->getPowerConsumption();
                    DEBUG_PRINT("turned ON.");
                } else {
                    DEBUG_PRINT("failed to turn ON. Not enough power.");
                }
            } else {
                DEBUG_PRINT("Nothing to do.");
            }
            DEBUG_PRINTLN();
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
        if (flagRestartNow) {
            if (mqttClient.connected())
                mqttClient.disconnect();
            if (tcpClient.connected())
                tcpClient.stop();
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP.restart();
        }
        if (mqttClient.connected()) {
            mqttClient.loop();
        }
        else {
            mqttConnect();
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void taskEmergency(void* pvParameters) {
    while(true) {
        processHeaters();
        vTaskSuspend(NULL);
    }
}

void taskMain(void* pvParameters) {
    while(true) {
        requestTemperatures();
        vTaskDelay(READ_SENSORS_DELAY / portTICK_PERIOD_MS);
        readTemperatures();
        processHeaters();
        vTaskDelay(TEMPERATURE_READ_INTERVAL / portTICK_PERIOD_MS);
    }
}

void taskProcessHeaters(void* pvParameters) {
    while(true) {
        processHeaters();
        vTaskSuspend(NULL);
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

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    //init settings
    loadSettings(settings);

    if (settings.useTcp) {
        auto now = millis();
        while(millis() - now < 2000) {
            if (tcpConnect()) {
                break;
            }
        }
    }
    DEBUG_PRINT("HeatingController32 Ver. ");DEBUG_PRINT(VERSION_SHORT);DEBUG_PRINTLN(" starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    DEBUG_PRINTLN("Initializing with settings:");
    DEBUG_PRINT("Hysteresis: "); DEBUG_PRINTLN(settings.hysteresis);
    DEBUG_PRINT("MQTT url: "); DEBUG_PRINTLN(settings.mqttUrl);
    DEBUG_PRINT("Mqtt port: "); DEBUG_PRINTLN(settings.mqttPort);
    DEBUG_PRINT("TCP url: "); DEBUG_PRINTLN(settings.tcpUrl);
    DEBUG_PRINT("TCP port: "); DEBUG_PRINTLN(settings.tcpPort);
    DEBUG_PRINT("Use TCP: "); DEBUG_PRINTLN(settings.useTcp);
    for (uint8_t i=0; i<NUMBER_OF_PHASES; i++) {
        DEBUG_PRINT("Phase ");DEBUG_PRINT(i); DEBUG_PRINT(": consumption limit: ");DEBUG_PRINTLN(settings.consumptionLimit[i]);
    }
    DEBUG_PRINTLN();

    ISR_Timer.enable(TIMER_NUM_ONEWIRE_LED_BLINK);
    sensors.begin();
    sensorsCount = sensors.getDS18Count();
    ISR_Timer.disable(TIMER_NUM_ONEWIRE_LED_BLINK);
#ifdef BLINK_ONEWIRE
    if (sensorsCount > 0) {
        oneWireBlinkDetectedSensors(sensorsCount);
    }
#endif

    DEBUG_PRINT("Detected sensors: "); DEBUG_PRINTDEC(sensorsCount); DEBUG_PRINTLN();
    for (uint8_t i = 0; i < sensorsCount; i++) {
        sensors.getAddress(sensorAddresses[i], i);
        String sensorAddress;
        byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensorAddress);
        DEBUG_PRINT(sensorAddress);
        DEBUG_PRINT(" : ");
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
        DEBUG_PRINT(temperatures[i]);
        DEBUG_PRINTLN();
    }
    DEBUG_PRINTLN();

    server.on("/default.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/default.css", "text/css");
    });
    server.on("/jquery-3.6.1.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/jquery-3.6.1.min.js", "text/javascript");
    });
    server.on("/jszip-utils.ie.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/jszip-utils.ie.min.js", "text/javascript");
    });
    server.on("/jszip-utils.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/jszip-utils.min.js", "text/javascript");
    });
    server.on("/jszip.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/jszip.min.js", "text/javascript");
    });
    server.on("/filesaver.min.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/filesaver.min.js", "text/javascript");
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/main.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/settings.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/control.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/sensors.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/backup", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/backup.html", String(), false, webServerPlaceholderProcessor);
    });
    server.on("/rebooting.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/rebooting.html", "text/html");
    });
    server.on("/settings", HTTP_POST, processSettingsForm);
    server.on("/control", HTTP_POST, processControlForm);
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* request) {
        reboot(request);
    });

    server.on("/files", HTTP_GET, [](AsyncWebServerRequest* request) {
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

    server.onNotFound([](AsyncWebServerRequest* request) {
        int pos = request->url().lastIndexOf("/");
        String filename = request->url().substring(pos);
        request->send(SPIFFS, filename, "text/plain");
    });

    AsyncElegantOTA.begin(&server);
    server.begin();

    mqttConnect();

    //init heaterItems
    initHeaters(); //will also fill unconnected sensors
    for (uint8_t i=0; i<sensorsCount; i++) {
        if (!checkSensorConfigured(&sensorAddresses[i])) {
            String sensor;
            byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensor);
            DEBUG_PRINT("Sensor ");
            DEBUG_PRINT(sensor);
            DEBUG_PRINTLN(" is connected but not configured.");
        }
    }

    auto now = millis();
    while (millis()-now < 5000) {
        if (mqttClient.connected())
            mqttClient.loop();
        if (consumptionDataRecieved)
        break;
    }

    DEBUG_PRINTLN("Starting tasks...");
    xTaskCreate(taskSystem, "System", 4096, NULL, 1, &hndlSystem);
    xTaskCreate(taskMain, "Main", 4096, NULL, 1, &hndlMain);
    xTaskCreate(taskEmergency, "Emergency", 4096, NULL, 3, &hndlEmergency);
    vTaskSuspend(hndlEmergency);
    xTaskCreate(taskProcessHeaters, "ProcessHeaters", 4096, NULL, 2, &hndlProcessHeaters);
    vTaskSuspend(hndlProcessHeaters);
}

void loop() {}