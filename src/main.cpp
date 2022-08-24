/*
    Ethernet:   http://arduino.ru/forum/apparatnye-voprosy/podklyuchenie-ethernet-lan8720-i-esp32-devkit-c-esp32-devkit-v1
                https://sautter.com/blog/ethernet-on-esp32-using-lan8720/
*/

#include <version.h>
#include <Arduino.h>
#include "Config.h"
#include "Utils.h"
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

HeaterItem heaterItems[NUMBER_OF_HEATERS];

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
                retValue += String(PHASES[j]);
                retValue += "\"";
                if (PHASES[j]==heaterItems[i].phase) {
                    retValue += " selected=\"selected\"";
                }
                retValue += ">";
                retValue += String(PHASES[j]);
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

void requestTemperatures() {
    /* TODO: Uncomment for production
    oneWireLed(HIGH);
    oneWireLedTimer.attachInterruptInterval(LED_BLINK_FAST, oneWireLedOff);
    */
    sensors.requestTemperatures();
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

void saveState(HeaterItem& heaterItem) {
    String fileName = "/item";
    fileName += String(heaterItem.address);

    File file = SPIFFS.open(fileName, FILE_WRITE, true);
    
    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    doc["name"] = heaterItem.name;
    doc["address"] = heaterItem.address;
    doc["subtopic"] = heaterItem.subtopic;
    doc["isEnabled"] = heaterItem.isEnabled;
    JsonArray sensorAddress = doc.createNestedArray("sensorAddress");
    for (uint8_t i=0; i<SENSOR_ADDR_LEN; i++) {
        sensorAddress.add(heaterItem.sensorAddress[i]);
    }
    doc["port"] = heaterItem.port;
    doc["phase"] = heaterItem.phase;
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

    serializeJson(doc, file);
    file.close();
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

    Serial.begin(115200);
    DEBUG_PRINTLN("HeatingController32 V1.0 starting...");
    DEBUG_PRINTLN("Debug mode");
    DEBUG_PRINTLN();

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    //init heaterItems
    for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
        loadState(heaterItems[i], i);
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
        String sensorAddress;
        byteArrayToHexString(sensorAddresses[i], SENSOR_ADDR_LEN, sensorAddress);
        DEBUG_PRINT(sensorAddress);
        DEBUG_PRINT(" : ");
        temperatures[i] = sensors.getTempC(sensorAddresses[i]);
        DEBUG_PRINT(temperatures[i]); DEBUG_PRINTLN();
    }
    DEBUG_PRINTLN();
 

    ethernetLedTimer.attachInterruptInterval(LED_BLINK_FAST, ethernetLedBlink);
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
}