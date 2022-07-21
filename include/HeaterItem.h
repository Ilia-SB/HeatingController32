// HeaterItem.h

#ifndef _HEATERITEM_h
#define _HEATERITEM_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#define ADDR_LEN 6
#define SENSOR_ADDR_LEN	8

class HeaterItem
{
	//variables
public:
	char* address[ADDR_LEN];
	boolean isEnabled;
	byte sensorAddress[SENSOR_ADDR_LEN];
	byte port;
	byte pin;
	boolean isAuto;
	uint16_t powerConsumption;
	boolean isOn;
	boolean wantsOn;
	byte priority;
	boolean isConnected;
	boolean actualState;
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
	void getAddressString(char* str, uint8_t* len);
protected:
private:
	HeaterItem(const HeaterItem& c);
	//void byteToHexStr(const byte* value, uint8_t size, char* str, uint8_t* len);
}; //HeaterItem

#endif

