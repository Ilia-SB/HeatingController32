#ifndef CONFIG_H_
#define CONFIG_H_

#ifdef DEBUG
	#undef DEBUG
#endif // !DEBUG
#define DEBUG

//Pins
#define ETHERNET_LED	14
#define MQTT_LED		12
#define ONEWIRE_LED		13

//Leds
#define LED_BLINK_FAST		125000.0f	//microseconds
#define LED_BLINK_MEDIUM	250000.0f
#define LED_BLINK_SLOW		500000.0f

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

const char* HOSTNAME = "HeatingController32";

//MQTT
const char* MQTT_URL = "192.168.1.3";
const int MQTT_PORT = 1883;
const char* COMMAND_TOPIC = "test/cmnd/heating";
const char* STATUS_TOPIC = "test/tele/heating";

#define NUMBER_OF_HEATERS 10
//const int pins[NUMBER_OF_HEATERS] = { 4,5,6,7,8,9,17,16,15,14 }; /*pins that control relays*/

#define SENSOR 16 //Temperature sensors pin

#define LS_DATA		15
#define LS_CLK		2
#define LS_STB		4

//#define CM_SENSOR	3
//#define I2C_ADDRESS 0x01
//#define COMMAND_BUFFER_LEN 64
//#define RESPONSE_BUFFER_LEN 64
#define DEFAULT_TEMPERATURE 5
#define DEFAULT_TEMPERATURE_ADJUST 0
#define DEFAULT_CONSUMPTION_LIMIT 10000
#define MAX_CONSUMPTION_LIMIT 15000
#define DEFAULT_HYSTERESIS 1
#define PROCESSING_INTERVAL 30000UL /*3s*/
#define OFF_ON_DELAY 2000UL /*2s*/
#define READ_SENSORS_DELAY 1000UL /*ms*/

/*
const byte PROGMEM HEATER1[3] =   {0x00, 0x00, 0x00};
const byte PROGMEM HEATER2[3] =	{0x00, 0x00, 0x01};
const byte PROGMEM HEATER3[3] =	{0x00, 0x00, 0x02};
const byte PROGMEM HEATER4[3] =	{0x00, 0x00, 0x03};
const byte PROGMEM HEATER5[3] =	{0x00, 0x00, 0x04};
const byte PROGMEM HEATER6[3] =	{0x00, 0x00, 0x05};
const byte PROGMEM HEATER7[3] =	{0x00, 0x00, 0x06};
const byte PROGMEM HEATER8[3] =	{0x00, 0x00, 0x07};
const byte PROGMEM HEATER9[3] =	{0x00, 0x00, 0x08};
const byte PROGMEM HEATER10[3] = {0x00, 0x00, 0x09};
*/

const byte PROGMEM MODBUS_REQUEST[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };
//                                      addr   fnc  start_addr  read_bytes      crc
//Response: 0x01 0x03 0x02  0xXX 0xXX 0xYY 0xYY XXXX - consumption, YYYY - CRC

#define SENSOR_ADDRESS	0
#define IS_ENABLED		8
#define PORT			9
#define IS_AUTO			10
#define IS_ON			11
#define PRIORITY		12
#define TARGET_TEMP		13
#define TEMP_ADJUST		17
#define CONSUMPTION		21

#define ACTUAL_STATE    22 //only used for MQTT reporting, not for writitng to EEPROM
#define IS_CONNECTED    23 //same

#define INCORRECT_JSON  254
#define UNDEFINED		255

#define HEATER_RECORD_LEN	23 /*bytes*/

#define CONSUMPTION_LIMIT (HEATER_RECORD_LEN * NUMBER_OF_HEATERS)
#define HYSTERESYS (CONSUMPTION_LIMIT + 2)

#define EEPROM_WRITE_DELAY_TIME	5000 /*5 seconds*/
#endif /* CONFIG_H_ */