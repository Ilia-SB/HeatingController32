/*
 * DebugPrint.h
 *
 * Created: 14.09.2016 16:43:29
 * Author: Ilia
 */


#ifndef DEBUGPRINT_H_
#define DEBUGPRINT_H_


#if defined(DEBUG)
    #define DEBUG_PRINT(x)			Serial.print (x); if(tcpClient.connected()) tcpClient.print(x);
    #define DEBUG_PRINTDEC(x)		Serial.print (x, DEC); if(tcpClient.connected()) tcpClient.print(x, DEC);
    #define DEBUG_PRINTHEX(x)		Serial.print (x, HEX); if(tcpClient.connected()) tcpClient.print(x, HEX);
    #define DEBUG_PRINTLN(x)		Serial.println (x); if(tcpClient.connected()) tcpClient.println(x);
    #define DEBUG_PRINT_ARRAY(x, len)   for(uint8_t i=0; i<len; i++) {Serial.print(x[i]);Serial.print(" "); if(tcpClient.connected()) {tcpClient.print(x[i]); tcpClient.print(" ");}}
    #define DEBUG_MEMORY()          Serial.println();Serial.print(F("!!! Free memory: "));Serial.print(freeMemory());Serial.println(F(" !!!"));Serial.println();
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTDEC(x)
    #define DEBUG_PRINTHEX(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_MEMORY()
#endif

#if defined(MQTT_DEBUG)
    #define DEBUG_PRINT_MQTT(x)          if(mqttClient.connected()) mqttClient.publish("test/heating/debug", x, false)
#else
    #define DEBUG_PRINT_MQTT(x)
#endif

#endif /* DEBUGPRINT_H_ */
