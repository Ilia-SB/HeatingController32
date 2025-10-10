/*
 * DebugPrint.h
 *
 * Created: 14.09.2016 16:43:29
 * Author: Ilia
 */


#ifndef DEBUGPRINT_H_
#define DEBUGPRINT_H_

#include <Arduino.h>

// Debug functions are now implemented as runtime functions in main.cpp
// Debug output can be configured via web interface (serial and UDP)

void debugPrint(const String& msg);
void debugPrint(const char* msg);
void debugPrint(int val);
void debugPrint(unsigned int val);
void debugPrint(long val);
void debugPrint(unsigned long val);
void debugPrint(float val);
void debugPrintln();
void debugPrintln(const String& msg);
void debugPrintln(const char* msg);
void debugPrintln(int val);
void debugPrintln(unsigned int val);
void debugPrintln(long val);
void debugPrintln(unsigned long val);
void debugPrintln(float val);
void debugPrintDec(int val);
void debugPrintHex(int val);
void debugPrintArray(uint8_t* arr, uint8_t len);
void debugStack();

#endif /* DEBUGPRINT_H_ */
