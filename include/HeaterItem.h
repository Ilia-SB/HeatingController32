// HeaterItem.h
//TODO: Callback for state changes


#ifndef _HEATERITEM_h
#define _HEATERITEM_h

#include <Arduino.h>
#include "MqttInterface.h"
#include "Utils.h"
#include "Config.h"

#define SENSOR_ADDR_LEN	8
#define SENSOR_ADDR_UNCONFIGURED {0,0,0,0,0,0,0,0};

class HeaterItem
{
	typedef void (*OutputCallBack)(uint8_t, bool);
	typedef void (*NotificationCallBack)(HeaterItem& heater);

	//variables
public:
	static constexpr uint8_t UNCONFIGURED = 255;
	enum DALLAS_SENSOR {
		SENSOR_NOT_CONNECTED	= -127,
		SENSOR_READ_ERROR		= 85
	};

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
	boolean wantsOn = false;
	byte priority = 100;
	boolean isConnected = false;
	boolean actualState = false;
	uint8_t tempReadErrors = 0;

	float temperature = 0;
	float targetTemperature = 0;
	float delta = 0;
	float temperatureAdjust = 0;
	float hysteresis = 0;

	OutputCallBack outputCallback;
	NotificationCallBack notificationCallback;

	//functions
public:
	HeaterItem();
	~HeaterItem();
	bool operator>(const HeaterItem& c);
	//HeaterItem& operator=( const HeaterItem &c );
	void setName(const String& s);
	String getName(void);
	void setSubtopic(const String& s);
	String getSubtopic(void);
	void setAddress(const uint8_t a);
	uint8_t getAddress(void);
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
	void setIsAuto(const bool b);
	bool setIsAuto(const char* val);
	bool getIsAuto(void);
	void getIsAutoCStr(char* val);
	void setWantsOn(const bool b);
	bool setWantsOn(const char* val);
	bool getWantsOn(void);
	bool setActualState(const bool);
	bool getActualState(void);
	void setPriority(const uint8_t p);
	bool setPriority(const char* val);
	byte getPriority(void);
	void getPriorityCStr(char* val);
	bool setTargetTemperature(const char* val);
	void getTargetTemperatureCStr(char* val);
	void setSensorAddress(const byte* p);
	bool setSensorAddress(const char* val);
	byte* getSensorAddress(void);
	void getSensorAddressCStr(char* val);
	void setPort(const uint8_t p);
	bool setPort(const char* val);
	byte getPort(void);
	void getPortCStr(char* val);
	void setPhase(const uint8_t p);
	bool setPhase(const char* val);
	uint8_t getPhase(void);
	void getPhaseCStr(char* val);
	bool setTemperatureAdjust(const char* val);
	void getTemperatureAdjustCStr(char* val);
	void setPowerConsumption(const uint16_t p);
	bool setPowerConsumption(const char* val);
	uint16_t getPowerConsumption(void);
	void getPowerConsumptionCStr(char* val);
	void setIsEnabled(const bool b);
	bool setIsEnaled(const char* val);
	bool getIsEnabled(void);
	void getIsEnabledCStr(char* val);
	bool setHysteresis(const float);
	float getHysteresis(void);
	void setIsConnected(const bool b);
	bool getIsConnected(void);
	void tempReadError(void);
	void setTempReadErrors(uint8_t);
	uint8_t getTempReadErrors(void);

	void setOutputCallBack(OutputCallBack);
	void setNotificationCallBack(NotificationCallBack);

	static void sortHeaters(HeaterItem **array, int size);
	static void sortHeatersByPowerConsumption(HeaterItem **array, int size);
protected:
private:
	HeaterItem(const HeaterItem& c);
	void processTemperature(void);
}; //HeaterItem

#endif