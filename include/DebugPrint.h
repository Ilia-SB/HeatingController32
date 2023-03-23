/*
 * DebugPrint.h
 *
 * Created: 14.09.2016 16:43:29
 * Author: Ilia
 */


#ifndef DEBUGPRINT_H_
#define DEBUGPRINT_H_


#if defined(DEBUG)
    #define DEBUG_PRINT(x)			    Log.print (x);
    #define DEBUG_PRINTDEC(x)		    Log.print (x, DEC);
    #define DEBUG_PRINTHEX(x)		    Log.print (x, HEX);
    #define DEBUG_PRINTLN(x)		    Log.println (x);
    #define DEBUG_PRINT_ARRAY(x, len)   for(uint8_t i=0; i<len; i++) {Log.print(x[i]); Log.print(" ");}
    #define DEBUG_MEMORY()              Log.println();Log.print(F("!!! Free memory: "));Log.print(freeMemory());Log.println(F(" !!!"));Log.println();
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
