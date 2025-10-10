# Debug System Refactoring - Runtime Functions with Persistent Settings

**Date:** 2025-10-10  
**Type:** Major Refactoring

## Overview

Refactored the debug output system from compile-time preprocessor macros to runtime functions with persistent configuration. TCP debug output is now configurable at runtime via web interface.

## Changes Made

### 1. Settings Class Updates (`include/Settings.h`, `src/Settings.cpp`)

**Added fields:**
- `bool debugSerial` - Enable/disable serial debug output
- `bool debugTcp` - Enable/disable TCP debug output

**Modified fields (repurposed for debug configuration):**
- `String tcpUrl` - TCP server IP address for debug output
- `uint16_t tcpPort` - TCP port for debug output

**Added constants:**
- `SETTINGS_DEBUG_SERIAL "debugSerial"`
- `SETTINGS_DEBUG_TCP "debugTcp"`

**Modified constants (repurposed):**
- `SETTINGS_TCP_URL "tcpUrl"`
- `SETTINGS_TCP_PORT "tcpPort"`

### 2. Config File Updates (`include/Config.h`)

**Changes:**
- Incremented `SETTINGS_VERSION` from 2 to 3 (triggers settings migration)
- Removed `DEBUG` preprocessor define
- TCP constants remain for debug output:
  - `TCP_URL` (default "192.168.1.3")
  - `TCP_PORT` (default 8085)

### 3. Debug Functions (`include/DebugPrint.h`, `src/main.cpp`)

**Before:** Preprocessor macros that conditionally compile based on `#ifdef DEBUG`
```cpp
#if defined(DEBUG)
    #define DEBUG_PRINT(x) Serial.print(x); if(tcpClient.connected()) tcpClient.print(x);
    #define DEBUG_PRINTLN(x) Serial.println(x); if(tcpClient.connected()) tcpClient.println(x);
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
#endif
```

**After:** Runtime functions that check settings flags
```cpp
// Declarations (include/DebugPrint.h)
void debugPrint(const String& msg);
void debugPrint(const char* msg);
void debugPrint(int val);
// ... additional overloads (18 total function declarations)
void debugStack();

// Implementations (src/main.cpp, lines 209-384, after callback functions)
void debugPrint(const String& msg) {
    if (settings.debugSerial) {
        Serial.print(msg);
    }
    if (settings.debugTcp && tcpClient.connected()) {
        tcpClient.print(msg);
    }
}
```

**Code organization:**
- Declarations in `include/DebugPrint.h` (proper header file)
- Implementations in `src/main.cpp` after callback functions (lines 209-384)
- Clean separation: interface in header, implementation in source
- Follows standard C/C++ convention
- All debug output sent immediately (no buffering)
- TCP connection checked before each send

**Available debug functions:**
- `debugPrint()` - Multiple overloads for String, char*, int, uint, long, ulong, float
- `debugPrintln()` - Multiple overloads with newline
- `debugPrintDec()` - Print integer in decimal format
- `debugPrintHex()` - Print integer in hex format
- `debugPrintArray()` - Print array elements space-separated
- `debugStack()` - Print FreeRTOS stack high water mark

### 4. Network Changes (`src/main.cpp`)

**TCP Client for Debug:**
- `WiFiClient tcpClient` used for debug output
- `tcpConnect()` function establishes connection to debug server
- TCP connection initialized in `setup()` if `settings.debugTcp` is enabled
- TCP cleanup added to `taskSystem()` restart logic (flush and close before restart)

**TCP Implementation:**
- Connects to configured IP address and port
- Maintains stateful connection
- Debug output sent immediately (no buffering)
- Checks `tcpClient.connected()` before each send
- TCP provides ordered, reliable delivery (unlike UDP)

### 5. Web Interface Updates

**Settings page (`data/settings.html` via `webServerPlaceholderProcessor`):**

Added form fields:
- "Serial debug" - Checkbox to enable/disable serial output
- "TCP debug" - Checkbox to enable/disable TCP output  
- "TCP debug url" - Text input for TCP server IP address
- "TCP debug port" - Text input for TCP server port

**Form handler (`processSettingsForm`):**
- Reads debug checkbox states from POST parameters
- Saves to settings and triggers reboot (settings take effect after restart)

### 6. Macro Replacement

Systematically replaced all debug macro calls throughout `src/main.cpp`:
- `DEBUG_PRINT(x)` → `debugPrint(x)` (99+ occurrences)
- `DEBUG_PRINTLN(x)` → `debugPrintln(x)`
- `DEBUG_PRINTDEC(x)` → `debugPrintDec(x)`
- `DEBUG_PRINTHEX(x)` → `debugPrintHex(x)`
- `DEBUG_PRINT_ARRAY(x, len)` → `debugPrintArray(x, len)`
- `DEBUG_STACK` → `debugStack()`

### 7. Settings Persistence

**Default values (when settings file doesn't exist):**
- `debugSerial = true` - Serial debug enabled by default
- `debugTcp = true` - TCP debug enabled by default
- `tcpUrl = "192.168.1.3"` - Default TCP server address
- `tcpPort = 8085` - Default TCP port

**Loading behavior:**
- Settings version mismatch triggers automatic deletion and recreation with defaults
- Missing debug fields in existing settings file default to `true` (backward compatible)
- Missing TCP URL/port default to configured constants

**Saving behavior:**
- All four debug settings persisted to LittleFS (debugSerial, debugTcp, tcpUrl, tcpPort)
- Survive reboots
- Configurable via web interface

## Benefits

1. **No recompilation needed** - Debug output can be toggled at runtime via web interface
2. **Persistent configuration** - Settings survive reboots
3. **Selective output** - Can enable only serial, only TCP, both, or neither
4. **Ordered delivery** - TCP guarantees message order (unlike UDP)
5. **Reliable connection** - TCP ensures all debug messages arrive
6. **Better separation** - Debug system is independent of build configuration

## Migration Notes

- **Settings version bump** - Existing settings files will be auto-deleted and recreated with defaults
- **Users must reconfigure** - All settings (heater configs, MQTT settings, etc.) will need to be re-entered via web interface
- **TCP debug configurable** - TCP debug server address and port configurable via web interface
- **TCP defaults** - Default 192.168.1.3:8085, both configurable via web interface

## Testing Recommendations

1. **Serial output**
   - Enable serial debug only, disable TCP debug
   - Verify output appears on Serial Monitor
   - Verify no network traffic

2. **TCP output**
   - Enable TCP debug only, disable serial debug
   - Configure TCP debug url to your PC's IP
   - Run TCP server (e.g., `nc -l 8085` or `socat TCP-LISTEN:8085,fork -`)
   - Verify debug messages received at TCP server
   - Verify no serial output
   - Test connection failure handling

3. **Both enabled**
   - Enable both serial and TCP debug
   - Verify output on both channels simultaneously

4. **Both disabled**
   - Disable both debug options
   - Verify no debug output on either channel
   - Verify system continues to function normally

5. **Settings persistence**
   - Configure debug settings via web interface
   - Reboot controller
   - Verify settings retained after reboot
   - Verify TCP reconnects automatically after reboot

6. **Web interface**
   - Verify checkboxes reflect current state
   - Verify TCP URL and port fields show current values
   - Verify changing settings triggers reboot

## Backward Compatibility

- **Breaking change** - Settings version incremented, all settings reset
- **No code compatibility** - All `DEBUG_*` macros replaced with function calls
- **Build system** - No longer needs `DEBUG` preprocessor flag

## Related Files

- `include/Config.h` - SETTINGS_VERSION, UDP_PORT constant
- `include/Settings.h` - Debug settings fields and constants
- `include/DebugPrint.h` - Function declarations
- `src/Settings.cpp` - Settings class (no changes needed)
- `src/main.cpp` - Debug function implementations, macro replacements
- `data/settings.html` - Web interface (modified via placeholder processor)

## Notes

- Debug functions check `tcpClient.connected()` before attempting TCP send
- TCP connection established at startup if `settings.debugTcp` is enabled
- **No buffering** - Each debug call sends immediately over TCP
- TCP guarantees ordered delivery (messages arrive in the order sent)
- TCP connection gracefully closed during restart (flush, FIN, 2-second wait)
- Debug stack function includes "Free stack: " prefix for consistency
- Settings default to enabled if not present (backward compatible upgrade path)
- TCP connection state maintained by WiFiClient class

