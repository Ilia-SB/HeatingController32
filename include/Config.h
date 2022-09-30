#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>

#define SETTINGS_VERSION	2

#ifdef DEBUG
	#undef DEBUG
#endif // !DEBUG
#define DEBUG

//Pins
#define ETHERNET_LED	14
#define MQTT_LED		12
#define ONEWIRE_LED		13

//Leds
#define LED_BLINK_FAST		125	//miliseconds
#define LED_BLINK_MEDIUM	250
#define LED_BLINK_SLOW		500

//Ethernet
#ifdef ETH_CLK_MODE
	#undef ETH_CLK_MODE
#endif
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT    //Clock pin
#define ETH_POWER_PIN   -1                      //Use internal APLL
#define ETH_TYPE        ETH_PHY_LAN8720
#define ETH_ADDR        1                       //0 or 1
#define ETH_MDC_PIN     23                      //I2C clock pin
#define ETH_MDIO_PIN    18                      //I2C IO pin

static const char* HOSTNAME = "HeatingController32";

//MQTT
static const char* MQTT_URL = "192.168.1.3";
static const int MQTT_PORT = 1883;
static const String COMMAND_TOPIC = "cmnd/heating/#";
static const char* STATUS_TOPIC = "tele/heating";
static const char* ENERGY_METER_TOPIC = "tele/energy_meter";
static const char* ERROR_TOPIC = "error/heating";

//TCP
static const char* TCP_URL = "192.168.1.3";
static const uint16_t TCP_PORT = 8085;

//JSON
#define JSON_DOCUMENT_SIZE 768
#define JSON_DOCUMENT_SIZE_SMALL 128
#define JSON_DOCUMENT_SIZE_SETTINGS 384
#define JSON_DOCUMENT_SIZE_ENERGY_METER 256

#define NUMBER_OF_HEATERS 16
#define NUMBER_OF_PORTS	  16
#define NUMBER_OF_PHASES  3
static const uint16_t CONSUMPTION_LIMITS[NUMBER_OF_PHASES] = {5000, 5000, 5000};
#define MAX_TEMP_READ_ERRORS	100

#define SENSOR_PIN 16 //Temperature sensors pin
#define MAX_NUMBER_OF_SENSORS 16

#define LS_DATA		15
#define LS_CLK		2
#define LS_STB		4
#define LS_OE		33

#define DEFAULT_TEMPERATURE 5
#define DEFAULT_TEMPERATURE_ADJUST 0
#define MAX_CONSUMPTION_LIMIT 6000
#define DEFAULT_HYSTERESIS 1.0f
#define TEMPERATURE_READ_INTERVAL 30000L /*3s*/
#define READ_SENSORS_DELAY 1000L /*ms*/

#endif /* CONFIG_H_ */