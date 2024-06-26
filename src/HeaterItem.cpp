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
	sensorTemperature = temp;
	if (!sensorStarted) {
		adjustedTemperature = sensorTemperature + temperatureAdjust + auxAdjust;
		sensorStarted = true;
	} else {
		adjustedTemperature = (sensorTemperature + temperatureAdjust + auxAdjust) * alpha + adjustedTemperature * (1.0f - alpha);
	}
	processTemperature();
}

float HeaterItem::getTemperature() {
	return adjustedTemperature;
}

float HeaterItem::getSensorTemperature() {
	return sensorTemperature + temperatureAdjust;
}

float HeaterItem::getRawSensorTemperature() {
	return sensorTemperature;
}

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

void HeaterItem::setAuxAdjust(const float adjust) {
	auxAdjust = adjust;
	processTemperature();
}

float HeaterItem::getAuxAdjust() {
	return auxAdjust;
}

bool HeaterItem::setAuxAdjust(const char* val) {
	float _auxAdjust = strtof(val, nullptr);
	setAuxAdjust(_auxAdjust);
	return true;
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
	if (isAuto == true && b == false) { //switching from auto to manual mode
		setWantsOn(false);
	}
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

bool HeaterItem::setWantsOn(const char* val) {
	if (isAuto == false) {
		if (strcmp(val, ON) == 0)
			setWantsOn(true);
		else if (strcmp(val, OFF) == 0)
			setWantsOn(false);
		else return false;
	}
	return false;
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
	setTemperatureAdjust(_temperatureAdjust);
	return true;
}

void HeaterItem::getTemperatureAdjustCStr(char* val) {
	dtostrf(temperatureAdjust, 5, 1, val);
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

void HeaterItem::sortHeatersByPowerConsumption(HeaterItem **array, int size) {
	int jump = size;
	bool swapped = true;
	HeaterItem *tmp;
	while(jump > 1 || swapped) {
		if (jump > 1)
			jump /= 1.24733;
		swapped = false;
		for (int i=0; i + jump < size; ++i) {
			if((*array[i + jump]).getPowerConsumption() > (*array[i]).getPowerConsumption()) {
				tmp = array[i];
				array[i] = array[i + jump];
				array[i + jump] = tmp;
				swapped = true;
			}
		}	
	}
}

void HeaterItem::setOutputCallBack(OutputCallBack callback) {
	outputCallback = callback;
}

void HeaterItem::setNotificationCallBack(NotificationCallBack callback) {
	notificationCallback = callback;
}

bool HeaterItem::setActualState(const bool state) {
	actualState = state;
	if (getIsEnabled() == true)
		outputCallback(port, actualState);
		notificationCallback(*this);
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

void HeaterItem::setName(const String& s) {
	name = s;
}

String HeaterItem::getName() {
	return name;
}

void HeaterItem::setSubtopic(const String& s) {
	subtopic = s;
}

String HeaterItem::getSubtopic() {
	return subtopic;
}

void HeaterItem::setAddress(const uint8_t a) {
	address = a;
}

uint8_t HeaterItem::getAddress() {
	return address;
}

bool HeaterItem::getIsAuto() {
	return isAuto;
}

void HeaterItem::setWantsOn(const bool b) {
	wantsOn = b;
}

bool HeaterItem::getWantsOn() {
	return wantsOn;
}

void HeaterItem::setPriority(const uint8_t p) {
	priority = p;
}

byte HeaterItem::getPriority() {
	return priority;
}

void HeaterItem::setSensorAddress(const byte* p) {
	memcpy(sensorAddress, p, SENSOR_ADDR_LEN);
}

byte* HeaterItem::getSensorAddress() {
	return sensorAddress;
}

void HeaterItem::setPort(const uint8_t p) {
	setActualState(false);
	port = p;
}

byte HeaterItem::getPort() {
	return port;
}

void HeaterItem::setPhase(const uint8_t p) {
	phase = p;
}

bool HeaterItem::setPhase(const char* val) {
	byte _phase = atoi(val);
	if (_phase < 0 || _phase > NUMBER_OF_PHASES - 1)
		return false;

	setPhase(_phase);
	return true;
}

void HeaterItem::getPhaseCStr(char* val) {
	itoa(phase, val, 10);
}

uint8_t HeaterItem::getPhase() {
	return phase;
}

void HeaterItem::setPowerConsumption(const uint16_t p) {
	powerConsumption = p;
}

uint16_t HeaterItem::getPowerConsumption() {
	return powerConsumption;
}

void HeaterItem::setIsEnabled(const bool b) {
	setActualState(false);
	setWantsOn(false);
	isEnabled = b;
	if (getIsAuto() == true) {
		processTemperature();
	}
}

bool HeaterItem::getIsEnabled() {
	return isEnabled;
}

void HeaterItem::setIsConnected(const bool b) {
	isConnected = b;
}

bool HeaterItem::getIsConnected() {
	return isConnected;
}

void HeaterItem::tempReadError() {
	tempReadErrors++;
}

uint8_t HeaterItem::getTempReadErrors(void) {
	return tempReadErrors;
}

void HeaterItem::setTempReadErrors(uint8_t errors) {
	tempReadErrors = errors;
}

void HeaterItem::setUsesAuxAdjust(const bool b) {
	usesAuxAdjust = b;
}

bool HeaterItem::getUsesAuxAdjust(void) {
	return usesAuxAdjust;
}

void HeaterItem::processTemperature() {
	if (isAuto) {
		delta = targetTemperature - getTemperature();
		if (delta > 0) {
			wantsOn = true;
		} else if (delta < -hysteresis) {
			wantsOn = false;
		}
	}
}