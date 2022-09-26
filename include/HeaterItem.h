// HeaterItem.h
//TODO: Callback for state changes
//TODO: Callcack for physical output

#ifndef _HEATERITEM_h
#define _HEATERITEM_h

#include <Arduino.h>
#include "MqttInterface.h"
#include "Utils.h"

//#define ADDR_LEN 7 //6 chars + /n
#define SENSOR_ADDR_LEN	8
#define SENSOR_ADDR_UNCONFIGURED {0,0,0,0,0,0,0,0};

typedef void (*OutputCB)(uint8_t, bool);

class HeaterItem
{
	//variables
public:
	static constexpr uint8_t UNCONFIGURED = 255;

protected:

private:
	String name = "";
	String subtopic = "";
	uint8_t address = 0;
	boolean isEnabled = false;
	byte sensorAddress[SENSOR_ADDR_LEN] = SENSOR_ADDR_UNCONFIGURED;
	byte port = UNCONFIGURED;
	byte phase = UNCONFIGURED;
	byte pin;
	boolean isAuto = false;
	uint16_t powerConsumption = 0;
	boolean isOn = false;
	boolean wantsOn = false;
	byte priority = 100;
	boolean isConnected = false;
	boolean actualState = false;

	float temperature = 0;
	float targetTemperature = 0;
	float delta = 0;
	float temperatureAdjust = 0;
	float hysteresis = 0;

	OutputCB setPortCallback;

	//functions
public:
	HeaterItem();
	~HeaterItem();
	bool operator>(const HeaterItem& c);
	//HeaterItem& operator=( const HeaterItem &c );
	void setName(const String& s) {name = s;}
	String getName(void) {return name;}
	void setSubtopic(const String& s) {subtopic = s;}
	String getSubtopic(void) {return subtopic;}
	void setAddress(const uint8_t a) {address = a;}
	uint8_t getAddress(void) {return address;}
	void setTemperature(const float temp);
	float getTemperature();
	//void getTemperatureBytes(byte* array);
	void setTargetTemperature(const float temp);
	float getTargetTemperature();
	void setTemperatureAdjust(const float temp);
	float getTemperatureAdjust();
	void getTemperatureAdjustBytes(byte* array);
	float getDelta();
	void getAddressString(String& string, const char* format);
	void setIsAuto(const bool);
	bool setIsAuto(const char* val);
	bool getIsAuto(void) {return isAuto;}
	void getIsAutoCStr(char* val);
	void setIsOn(const bool b) {isOn = b;}
	bool setIsOn(const char* val);
	bool getIsOn(void) {return isOn;}
	void getIsOnCStr(char* val);
	void setWantsOn(const bool b) {wantsOn = b;}
	bool getWantsOn(void) {return wantsOn;}
	bool setActualState(const bool);
	bool getActualState(void);
	void setPriority(const uint8_t p) {priority = p;}
	bool setPriority(const char* val);
	byte getPriority(void) {return priority;}
	void getPriorityCStr(char* val);
	bool setTargetTemperature(const char* val);
	void getTargetTemperatureCStr(char* val);
	void setSensorAddress(const byte* p) {memcpy(sensorAddress, p, SENSOR_ADDR_LEN);}
	bool setSensorAddress(const char* val);
	byte* getSensorAddress(void) {return sensorAddress;}
	void getSensorAddressCStr(char* val);
	void setPort(const uint8_t p) {port = p;}
	bool setPort(const char* val);
	byte getPort(void) {return port;}
	void getPortCStr(char* val);
	void setPhase(const uint8_t p) {phase = p;}
	uint8_t getPhase(void) {return phase;}
	bool setTemperatureAdjust(const char* val);
	void getTemperatureAdjustCStr(char* val);
	void setPowerConsumption(const uint16_t p) {powerConsumption = p;}
	bool setPowerConsumption(const char* val);
	uint16_t getPowerConsumption(void) {return powerConsumption;}
	void getPowerConsumptionCStr(char* val);
	void setIsEnabled(const bool b) {isEnabled = b;}
	bool setIsEnaled(const char* val);
	bool getIsEnabled(void) {return isEnabled;}
	void getIsEnabledCStr(char* val);
	void setOutputCB(OutputCB);
	bool setHysteresis(const float);
	float getHysteresis(void);
	void setIsConnected(const bool b) {isConnected = b;}
	bool getIsConnected(void) {return isConnected;}

	static void sortHeaters(HeaterItem **array, int size);
protected:
private:
	HeaterItem(const HeaterItem& c);
	void processTemperature(void);
}; //HeaterItem

#endif