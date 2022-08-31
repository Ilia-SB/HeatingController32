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

bool HeaterItem::setIsAuto(const char* val) {
	if (strcmp(val, ON) == 0)
		isAuto = true;
	else if (strcmp(val, OFF) == 0)
		isAuto = true;
	else
		return false;
	
	return true;
}

void HeaterItem::getIsAutoCStr(char* val) {
	if (isAuto)
		strcpy(val, ON);
	else
		strcpy(val, OFF);
}

bool HeaterItem::setIsOn(const char* val) {
	if (isAuto)
		return false;
	if (strcmp(val, ON) == 0)
		isOn = true;
	else if (strcmp(val, OFF) == 0)
		isOn = false;
	else
		return false;

	return true;
}

void HeaterItem::getIsOnCStr(char* val) {
	if (isOn)
		strcpy(val, ON);
	else
		strcpy(val, OFF);
}

bool HeaterItem::setPriority(const char* val) {
	byte _priority = atoi(val); 
	
	if (_priority <= 0) {
		return false;
	}
	priority = _priority;
	return true;
}

void HeaterItem::getPriorityCStr(char* val) {
	itoa(priority, val, 10);
}

bool HeaterItem::setTargetTemperature(const char* val) {
	float _temperature = strtof(val, nullptr);
	if (_temperature <= 0.0f)
		return false;
	setTargetTemperature(_temperature);
	return true;
}

void HeaterItem::getTargetTemperatureCStr(char* val) {
	dtostrf((double)targetTemperature, 5, 1, val);
}

bool HeaterItem::setSensor(byte* val) {
	memcpy(sensorAddress, val, SENSOR_ADDR_LEN);
	return true;
}

void HeaterItem::getSensorCStr(char* val) {
	byteArrayToHexCString(sensorAddress, SENSOR_ADDR_LEN, val);
}

bool HeaterItem::setPort(const char* val) {
	byte _port = atoi(val);
	if (_port <=0)
		return false;

	port = _port;
	return true;
}

void HeaterItem::getPortCStr(char* val) {
	itoa(port, val, 10);
}

bool HeaterItem::setTemperatureAdjust(const char* val) {
	float _temperatureAdjust = strtof(val, nullptr);
	if (_temperatureAdjust <= 0.0f)
		return false;
	setTemperatureAdjust(_temperatureAdjust);
	return true;
}

void HeaterItem::getTemperatureAdjustCStr(char* val) {
	dtostrf((double)temperatureAdjust, 5, 1, val);
}

bool HeaterItem::setConsumption(const char* val) {
	uint16_t _powerConsumption = atoi(val);
	if (_powerConsumption <= 0)
		return false;
	powerConsumption = _powerConsumption;
	return true;
}

void HeaterItem::getConsumptionCStr(char* val) {
	itoa(powerConsumption, val, 10);
}

bool HeaterItem::setIsEnaled(const char* val) {
	if (strcmp(val, ON) == 0)
		isEnabled = true;
	else if (strcmp(val, OFF) == 0)
		isEnabled = false;
	else
		return false;

	return true;
}

void HeaterItem::getIsEnabledCStr(char* val) {
	if (isEnabled)
		strcpy(val, ON);
	else
		strcpy(val, OFF);
}