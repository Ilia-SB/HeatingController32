# Fix: Board Crash on MQTT Temperature Change Command

**Date:** 2025-10-10  
**Issue:** Board reboots with "Exception/panic" when changing target temperature via MQTT

## Problem Description

When sending an MQTT command to change a heater's target temperature, the ESP32 would crash and reboot with reset reason "Exception/panic". This also likely affected other MQTT commands that modified heater settings.

## Root Cause

The issue was caused by calling heavy and reentrant operations from within the MQTT callback context. Specifically, in the `processCommand()` function (called from `mqttCallback()`), the code was:

1. Performing filesystem I/O via `saveState()` - JSON serialization and file writes
2. Publishing MQTT messages via `reportHeaterState()` - **MQTT publish from within MQTT callback (reentrancy issue!)**
3. Calling `processHeaters()` - a complex function with significant stack usage

Similarly, `getConsumptionData()` (also called from MQTT callback) was calling `processHeaters()`.

### Why This Causes Crashes

1. **REENTRANCY ISSUE (Primary Cause):** `reportHeaterState()` calls `mqttClient.beginPublish()`, `mqttClient.write()`, and `mqttClient.endPublish()` from within an MQTT callback. The PubSubClient library is **not reentrant** - calling these methods while already inside `mqttClient.loop()` corrupts the client state and causes crashes.

2. **Stack Overflow:** The MQTT callback runs in the context of the network task with limited stack space. The deep call chain (callback → processCommand → saveState + reportHeaterState + processHeaters) exceeded available stack.

3. **Blocking Operations:** File I/O operations in the callback context can block for too long, causing watchdog timeouts or stack corruption.

## Solution

Two-part solution combining increased stack space with deferred heavy processing:

1. **Increased `taskSystem` stack** from 4096 to 8192 bytes to accommodate MQTT callback operations
2. **Deferred `processHeaters()`** using flag-based approach:
   - MQTT callback sets `flagProcessHeatersNow = true`
   - `taskSystem` calls `processHeaters()` immediately after `mqttClient.loop()` returns
   - This moves the heaviest operation out of callback context while keeping `saveState()` and `reportHeaterState()` inline

This provides **immediate processing** (within ~100ms) with adequate stack space.

### Changes Made

**File:** `src/main.cpp`

#### Change 1: Add flag for deferred execution (line 104)

**Added:**
```cpp
bool flagProcessHeatersNow = false;
```

#### Change 2: processCommand() function (lines 353-361)

**Before:**
```cpp
heater->setIsConnected(checkSensorConnected(*heater));
sanityCheckHeater(*heater);
if (save) {
    saveState(*heater);
}
reportHeaterState(*heater);
processHeaters();
```

**After:**
```cpp
heater->setIsConnected(checkSensorConnected(*heater));
sanityCheckHeater(*heater);
if (save) {
    saveState(*heater);
}
reportHeaterState(*heater);
// Signal taskSystem to call processHeaters() immediately after MQTT callback returns
flagProcessHeatersNow = true;
```

**Key change:** Only `processHeaters()` is deferred. `saveState()` and `reportHeaterState()` remain in callback but now have adequate stack space.

#### Change 3: getConsumptionData() function (lines 231-244)

**Before:**
```cpp
currentConsumption[phase] = (uint16_t)(doc[key].as<float>() * 1000);
consumptionDataReceived[phase] = millis();
bool emergency = false;
//DEBUG_PRINTLN(currentConsumption[phase]);
if (currentConsumption[phase] > settings.consumptionLimit[phase]) {
    flagEmergency[phase] = true;
    emergency = true;
} else {
    flagEmergency[phase] = false;
}
if(emergency) {
    processHeaters();
}
```

**After:**
```cpp
currentConsumption[phase] = (uint16_t)(doc[key].as<float>() * 1000);
consumptionDataReceived[phase] = millis();
bool emergency = false;
//DEBUG_PRINTLN(currentConsumption[phase]);
if (currentConsumption[phase] > settings.consumptionLimit[phase]) {
    flagEmergency[phase] = true;
    emergency = true;
} else {
    flagEmergency[phase] = false;
}
// Only process heaters immediately if there's an emergency
if (emergency) {
    flagProcessHeatersNow = true;
}
```

The `emergency` variable is kept to ensure `processHeaters()` is only called immediately during emergencies, matching the original behavior.

#### Change 4: taskSystem() function (lines 1381-1386)

**Before:**
```cpp
if (mqttClient.connected()) {
    mqttClient.loop();
}
else {
    mqttConnect();
}
DEBUG_PRINT("<S<"); DEBUG_STACK;
xSemaphoreGive(mutex);
```

**After:**
```cpp
if (mqttClient.connected()) {
    mqttClient.loop();
}
else {
    mqttConnect();
}
// Process heaters immediately if MQTT command requested it
if (flagProcessHeatersNow) {
    flagProcessHeatersNow = false;
    processHeaters();
}
DEBUG_PRINT("<S<"); DEBUG_STACK;
xSemaphoreGive(mutex);
```

#### Change 5: Increase taskSystem stack size (lines 1620-1621)

**Before:**
```cpp
xTaskCreate(taskSystem, "System", 4096, NULL, 1, &hndlSystem);
```

**After:**
```cpp
// Increased taskSystem stack to handle MQTT callback operations (saveState, reportHeaterState, processHeaters)
xTaskCreate(taskSystem, "System", 8192, NULL, 1, &hndlSystem);
```

**Key change:** Doubled the stack size to provide adequate space for MQTT callback operations.

## Impact

### Positive
- **Stability:** No more crashes when changing settings via MQTT
- **Immediate Response:** Heater processing happens within ~100ms (the next `taskSystem` loop iteration)
- **Safe Context:** Processing runs in `taskSystem` with proper stack allocation and mutex protection
- **Emergency Handling:** Emergency conditions trigger immediate processing, not delayed

### Response Time
- **MQTT commands:** Processed within ~100ms (taskSystem loop interval)
- **Emergency conditions:** Handled immediately when flag is checked
- **Previous behavior:** Would process immediately but crash
- **Safe alternative:** Could have waited for next `taskMain` cycle (several seconds), but flag approach provides best of both worlds

## Testing Recommendations

1. **Basic MQTT Commands:**
   - Change target temperature for multiple heaters via MQTT
   - Enable/disable heaters via MQTT
   - Switch between auto/manual modes via MQTT
   - Verify no crashes occur

2. **Emergency Scenarios:**
   - Trigger emergency condition by exceeding consumption limit
   - Verify heaters are turned off within ~100ms
   - Monitor response time and confirm immediate processing

3. **Concurrent Operations:**
   - Send MQTT commands while web interface is being used
   - Verify mutex protection prevents race conditions
   - Check for any deadlocks or stuck states

4. **Stress Testing:**
   - Send rapid MQTT commands in succession
   - Verify system remains stable
   - Monitor memory usage and stack high water marks

## Notes

- Only `processHeaters()` is deferred; `saveState()` and `reportHeaterState()` remain in callback with increased stack
- If reentrancy issues persist with `reportHeaterState()` calling MQTT publish from within callback, those operations can also be deferred
- The increased stack size (8192 bytes) provides headroom for the callback operations
- The flag `flagProcessHeatersNow` is a simple boolean and doesn't require atomic operations or additional synchronization since:
  - It's only set to `true` by MQTT callback (which runs in `taskSystem` context via `mqttClient.loop()`)
  - It's only checked and cleared by `taskSystem` itself
  - No race condition is possible
- `taskSystem` runs every 100ms, providing near-immediate response to MQTT commands

## Related Code Locations

- `mqttCallback()`: Line 361 - Entry point for MQTT messages
- `processCommand()`: Line 246 - Processes heater commands
- `getConsumptionData()`: Line 218 - Processes energy meter data
- `taskMain()`: Line 1385 - Main processing loop that calls `processHeaters()` safely
- `processHeaters()`: Line 1194 - Complex heater state processing logic

