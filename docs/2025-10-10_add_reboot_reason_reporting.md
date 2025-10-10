# ESP32 Reboot Reason Reporting

**Date:** 2025-10-10  
**Type:** Feature Addition  
**Files Modified:** `src/main.cpp`

## Overview

Added ESP32 reboot reason reporting at startup to help diagnose unexpected reboots, watchdog timeouts, brownouts, and other reset conditions.

## Changes Made

### 1. Added ESP32 System Header

```cpp
#include <esp_system.h>
```

Added to the includes section in `src/main.cpp` to access the `esp_reset_reason()` function.

### 2. Created Helper Function

Added `getResetReason()` function before `setup()`:

```cpp
const char* getResetReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "Unknown";
        case ESP_RST_POWERON:    return "Power-on reset";
        case ESP_RST_EXT:        return "External pin reset";
        case ESP_RST_SW:         return "Software reset";
        case ESP_RST_PANIC:      return "Exception/panic";
        case ESP_RST_INT_WDT:    return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Task watchdog";
        case ESP_RST_WDT:        return "Other watchdog";
        case ESP_RST_DEEPSLEEP:  return "Deep sleep wake-up";
        case ESP_RST_BROWNOUT:   return "Brownout reset";
        case ESP_RST_SDIO:       return "SDIO reset";
        default:                 return "Unknown";
    }
}
```

### 3. Added Reboot Reason Reporting

Added in `setup()` function after "Debug mode" message:

```cpp
DEBUG_PRINT("Last reboot reason: "); DEBUG_PRINTLN(getResetReason());
```

## Output Example

When the ESP32 boots, the serial/TCP debug output will now include:

```
HeatingController32 [version] starting...
Debug mode
Last reboot reason: Power-on reset

Initializing with settings:
...
```

## Reset Reasons Covered

- **Power-on reset**: Normal power cycle
- **External pin reset**: Hardware reset button
- **Software reset**: ESP.restart() or similar
- **Exception/panic**: Crash/exception occurred
- **Interrupt watchdog**: Interrupt watchdog timeout
- **Task watchdog**: Task watchdog timeout (FreeRTOS task didn't yield)
- **Other watchdog**: Other watchdog timer reset
- **Deep sleep wake-up**: Woke from deep sleep
- **Brownout reset**: Voltage dropped below threshold
- **SDIO reset**: Reset via SDIO interface
- **Unknown**: Reason not determined

## Testing Recommendations

1. **Normal boot**: Verify "Power-on reset" appears on first boot
2. **Reset button**: Verify "External pin reset" when using reset button
3. **Software reset**: Call `ESP.restart()` and verify "Software reset"
4. **Watchdog timeout**: Create intentional infinite loop to trigger watchdog and verify appropriate watchdog message
5. **Brownout**: Test with borderline power supply to verify brownout detection

## Benefits

- **Diagnostics**: Quickly identify the cause of unexpected reboots
- **Debugging**: Helps track down watchdog timeouts and crashes
- **Monitoring**: Can be logged via MQTT for remote monitoring
- **Reliability**: Better understanding of system stability issues

## Notes

- The reboot reason is reported using the existing DEBUG_PRINT macros, so it will appear in both Serial output and TCP debug output (when connected)
- The information is also available to MQTT debug if MQTT_DEBUG is enabled

