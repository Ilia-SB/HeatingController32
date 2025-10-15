#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>

#define SETTINGS_VERSION	3

//#define BLINK_ONEWIRE

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
static const char* LWT_TOPIC = "tele/heating/LWT";
static const char* ENERGY_METER_TOPIC = "tele/energy_meter";
static const char* ERROR_TOPIC = "error/heating";

//JSON
#define JSON_DOCUMENT_SIZE 896
#define JSON_DOCUMENT_SIZE_SMALL 128
#define JSON_DOCUMENT_SIZE_SETTINGS 512
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
#define TEMPERATURE_READ_INTERVAL 30000L /*30s*/
#define READ_SENSORS_DELAY 1000L /*ms*/
#define EXTERNAL_SENSOR_TIMEOUT 180000L  /*3 minutes in milliseconds*/

//NTP
static const char* NTP_SERVER = "pool.ntp.org";
static const int GMT_OFFSET_HOURS = 3;       // Default: UTC (0 hours, use +/- for timezone)
static const int DAYLIGHT_OFFSET_HOURS = 0;  // Default: no daylight saving (usually 0 or 1)

//Reboot History
#define REBOOT_LOG_FILE "/reboot.log"
#define MAX_REBOOT_HISTORY_ENTRIES 50

//Debug System
#define DEBUG_BUFFER_SIZE 4096  // 4KB circular buffer
#define DEBUG_PAGE_UPDATE_INTERVAL 1000  // milliseconds - JavaScript polling interval
#define CONSUMPTION_DATA_TIMEOUT 5000    // milliseconds - energy meter data timeout

#endif /* CONFIG_H_ */