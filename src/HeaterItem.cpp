// 
// 
// 

#include "HeaterItem.h"


// default constructor
HeaterItem::HeaterItem()
{
} //HeaterItem

// default destructor
HeaterItem::~HeaterItem()
{
} //~HeaterItem

bool HeaterItem::operator>(const HeaterItem& c) {
	if (priority < c.priority) {
		return true;
	}
	if (priority > c.priority) {
		return false;
	}

	//if priority == c.priority
	if (delta > c.delta) {
		return true;
	}
	//otherwise return false;
	return false;
}

void HeaterItem::setTemperature(float temp) {
	temperature = temp;
	delta = targetTemperature - getTemperature();
}

float HeaterItem::getTemperature() {
	return temperature + temperatureAdjust;
}

//void HeaterItem::getTemperatureBytes(byte* array) {
//	float temp = getTemperature();
//	if (temp < 0) {
//		array[0] = 1;
//	}
//	else {
//		array[0] = 0;
//	}
//	array[1] = abs(temp);
//	array[2] = abs(temp) * 100 - array[1] * 100;
//}

void HeaterItem::setTargetTemperature(float temp) {
	targetTemperature = temp;
	delta = targetTemperature - getTemperature();
}

float HeaterItem::getTargetTemperature() {
	return targetTemperature;
}

void HeaterItem::setTemperatureAdjust(float temp) {
	temperatureAdjust = temp;
	delta = targetTemperature - getTemperature();
}

float HeaterItem::getTemperatureAdjust() {
	return temperatureAdjust;
}

void HeaterItem::getTemperatureAdjustBytes(byte* array) {
	if (temperatureAdjust < 0) {
		array[0] = 1;
	}
	else {
		array[0] = 0;
	}
	array[1] = abs(temperatureAdjust);
	array[2] = abs(temperatureAdjust) * 100 - array[1] * 100;
}

float HeaterItem::getDelta() {
	return delta;
}


void HeaterItem::getAddressString(String& string, const char* format) {
	char buffer[32];
	sprintf(buffer, format, address);
	string = String(buffer);
}

bool HeaterItem::setIsAuto(String& val) {
	val.toUpperCase();
	if (val.equals("ON")) {
		isAuto = true;
		return true;
	} else if (val.equals("OFF")) {
		isAuto = false;
		return true;
	} else {
		return false;
	}
}

String HeaterItem::getIsAutoStr() {
	if (isAuto)
		return String("ON");
	else
		return (String("OFF"));
}

bool setIsOn(String val);
String getIsOnStr();
bool setPriority(String val);
String getPriorityStr();
bool setTargetTemperature(String val);
String getTargetTemperatureStr();
bool setSensor(String val);
String getSensorStr();
bool setPort(String val);
String getPortStr();
bool setTemperatureAdjust(String val);
String getTemperatureAdjustStr();
bool setConsumption(String val);
String getConsumptionStr();
bool setIsEnaled(String val);
String getIsEnabledStr();
//void HeaterItem::byteToHexStr(const byte* value, uint8_t size, char* str, uint8_t* len) {
//	uint8_t t;
//	for (int i = 0; i < size; i++) {
//		t = value[i] / 16;
//		if (t < 10) {
//			*str = 48 + t;
//		}
//		else {
//			*str = 55 + t;
//		}
//		str++;
//
//		t = value[i] % 16;
//		if (t < 10) {
//			*str = 48 + t;
//		}
//		else {
//			*str = 55 + t;
//		}
//		str++;
//	}
//
//	*str = '\0';
//	*len = size * 2;
//}