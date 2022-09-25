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


class HeaterItem
{
	//variables
public:
	static constexpr uint8_t UNCONFIGURED = 255;

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
protected:
private:
	float temperature = 0;
	float targetTemperature = 0;
	float delta = 0;
	float temperatureAdjust = 0;

	//functions
public:
	HeaterItem();
	~HeaterItem();
	bool operator>(const HeaterItem& c);
	//HeaterItem& operator=( const HeaterItem &c );
	void setTemperature(float temp);
	float getTemperature();
	//void getTemperatureBytes(byte* array);
	void setTargetTemperature(float temp);
	float getTargetTemperature();
	void setTemperatureAdjust(float temp);
	float getTemperatureAdjust();
	void getTemperatureAdjustBytes(byte* array);
	float getDelta();
	void getAddressString(String& string, const char* format);
	bool setIsAuto(const char* val);
	void getIsAutoCStr(char* val);
	bool setIsOn(const char* val);
	void getIsOnCStr(char* val);
	bool setPriority(const char* val);
	void getPriorityCStr(char* val);
	bool setTargetTemperature(const char* val);
	void getTargetTemperatureCStr(char* val);
	bool setSensor(const char* val);
	void getSensorCStr(char* val);
	bool setPort(const char* val);
	void getPortCStr(char* val);
	bool setTemperatureAdjust(const char* val);
	void getTemperatureAdjustCStr(char* val);
	bool setConsumption(const char* val);
	void getConsumptionCStr(char* val);
	bool setIsEnaled(const char* val);
	void getIsEnabledCStr(char* val);

	static void sortHeaters(HeaterItem **array, int size);
protected:
private:
	HeaterItem(const HeaterItem& c);
}; //HeaterItem

#endif

