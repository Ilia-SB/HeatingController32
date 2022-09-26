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
	processTemperature();
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

void HeaterItem::setTargetTemperature(const float temp) {
	targetTemperature = temp;
	processTemperature();
}

float HeaterItem::getTargetTemperature() {
	return targetTemperature;
}

void HeaterItem::setTemperatureAdjust(const float temp) {
	temperatureAdjust = temp;
	processTemperature();
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

void HeaterItem::setIsAuto(const bool b) {
	isAuto = b;
	processTemperature();
}

bool HeaterItem::setIsAuto(const char* val) {
	if (strcmp(val, ON) == 0)
		setIsAuto(true);
	else if (strcmp(val, OFF) == 0)
		setIsAuto(false);
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

	if (strcmp(val, ON) == 0) {
		setIsOn(true);
		setWantsOn(true);
	}
	else if (strcmp(val, OFF) == 0) {
		setIsOn(false);
		setWantsOn(false);
	}
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

bool HeaterItem::setSensorAddress(const char* val) {
	return hexCStringToByteArray(val, sensorAddress, SENSOR_ADDR_LEN);
}

void HeaterItem::getSensorAddressCStr(char* val) {
	byteArrayToHexCString(sensorAddress, SENSOR_ADDR_LEN, val);
}

bool HeaterItem::setPort(const char* val) {
	byte _port = atoi(val);
	if (_port <=0)
		return false;

	setPort(_port);
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

bool HeaterItem::setPowerConsumption(const char* val) {
	uint16_t _powerConsumption = atoi(val);
	if (_powerConsumption <= 0)
		return false;
	setPowerConsumption(_powerConsumption);
	return true;
}

void HeaterItem::getPowerConsumptionCStr(char* val) {
	itoa(powerConsumption, val, 10);
}

bool HeaterItem::setIsEnaled(const char* val) {
	if (strcmp(val, ON) == 0)
		setIsEnabled(true);
	else if (strcmp(val, OFF) == 0)
		setIsEnabled(false);
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

void HeaterItem::sortHeaters(HeaterItem **array, int size) {
	int jump = size;
	bool swapped = true;
	HeaterItem *tmp;
	while(jump > 1 || swapped) {
		if (jump > 1)
			jump /= 1.24733;
		swapped = false;
		for (int i=0; i + jump < size; ++i) {
			if(*array[i + jump] > *array[i]) {
				tmp = array[i];
				array[i] = array[i + jump];
				array[i + jump] = tmp;
				swapped = true;
			}
		}	
	}
}

void HeaterItem::setOutputCB(OutputCB callback) {
	setPortCallback = callback;
}

bool HeaterItem::setActualState(const bool state) {
	actualState = state;
	setPortCallback(port, actualState);
	return true;
}

bool HeaterItem::getActualState() {
	return actualState;
}

bool HeaterItem::setHysteresis(const float h) {
	hysteresis = h;
	processTemperature();
	return true;
}

float HeaterItem::getHysteresis() {
	return hysteresis;
}

void HeaterItem::processTemperature() {
	delta = targetTemperature - getTemperature();
	if (isAuto) {
		if (delta > 0) {
			wantsOn = true;
		} else if (delta < -hysteresis) {
			wantsOn = false;
		}
	}
}