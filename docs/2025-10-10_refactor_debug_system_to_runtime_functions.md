# Debug System Refactoring - Runtime Functions with Persistent Settings

**Date:** 2025-10-10  
**Type:** Major Refactoring

## Overview

Refactored the debug output system from compile-time preprocessor macros to runtime functions with persistent configuration. Replaced TCP debug output with UDP broadcast for better reliability and reduced complexity.

## Changes Made

### 1. Settings Class Updates (`include/Settings.h`, `src/Settings.cpp`)

**Added fields:**
- `bool debugSerial` - Enable/disable serial debug output
- `bool debugUdp` - Enable/disable UDP debug output
- `String udpDebugAddress` - UDP server IP address for debug messages
- `uint16_t udpPort` - UDP port for debug messages

**Removed fields:**
- `String tcpUrl` - No longer using TCP for debug
- `uint16_t tcpPort` - No longer using TCP for debug
- `bool useTcp` - No longer using TCP for debug

**Added constants:**
- `SETTINGS_DEBUG_SERIAL "debugSerial"`
- `SETTINGS_DEBUG_UDP "debugUdp"`
- `SETTINGS_UDP_DEBUG_ADDRESS "udpDebugAddress"`
- `SETTINGS_UDP_PORT "udpPort"`

### 2. Config File Updates (`include/Config.h`)

**Changes:**
- Incremented `SETTINGS_VERSION` from 2 to 3 (triggers settings migration)
- Removed `DEBUG` preprocessor define
- Replaced TCP constants with UDP constants:
  - Removed: `TCP_URL`, `TCP_PORT`
  - Added: `UDP_DEBUG_ADDRESS` (default "192.168.1.3"), `UDP_PORT` (default 8086)

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

// Implementations (src/main.cpp, lines 209-480, after callback functions)
void debugPrint(const String& msg) {
    if (settings.debugSerial) {
        Serial.print(msg);
    }
    if (settings.debugUdp && ethConnected) {
        IPAddress udpAddress;
        if (udpAddress.fromString(settings.udpDebugAddress)) {
            udpClient.beginPacket(udpAddress, settings.udpPort);
            udpClient.print(msg);
            udpClient.endPacket();
        }
    }
}
```

**Code organization:**
- Declarations in `include/DebugPrint.h` (proper header file)
- Implementations in `src/main.cpp` after callback functions (lines 209-480)
- Clean separation: interface in header, implementation in source
- Follows standard C/C++ convention
- Each UDP send parses the configured IP address using `IPAddress.fromString()`

**Available debug functions:**
- `debugPrint()` - Multiple overloads for String, char*, int, uint, long, ulong, float
- `debugPrintln()` - Multiple overloads with newline
- `debugPrintDec()` - Print integer in decimal format
- `debugPrintHex()` - Print integer in hex format
- `debugPrintArray()` - Print array elements space-separated
- `debugStack()` - Print FreeRTOS stack high water mark

### 4. Network Changes (`src/main.cpp`)

**Replaced:**
- `WiFiClient tcpClient` → `WiFiUDP udpClient`
- Removed `tcpConnect()` function
- Removed TCP connection initialization from `setup()`
- Removed TCP cleanup from `taskSystem()` restart logic

**UDP Implementation:**
- Sends to configured IP address on configured port
- IP address parsed at runtime using `IPAddress.fromString()`
- No connection state to maintain
- Works immediately when Ethernet is connected
- More reliable than TCP for debug output
- Invalid IP addresses are silently ignored (packet not sent)

### 5. Web Interface Updates

**Settings page (`data/settings.html` via `webServerPlaceholderProcessor`):**

Added form fields:
- "Serial debug" - Checkbox to enable/disable serial output
- "UDP debug" - Checkbox to enable/disable UDP output  
- "UDP debug address" - Text input for UDP destination IP address
- "UDP port" - Text input for UDP port number

Removed form fields:
- "TCP url"
- "TCP port"
- "Use TCP"

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
- `debugUdp = true` - UDP debug enabled by default
- `udpDebugAddress = "192.168.1.3"` - Default UDP destination address
- `udpPort = 8086` - Default UDP port

**Loading behavior:**
- Settings version mismatch triggers automatic deletion and recreation with defaults
- Missing debug fields in existing settings file default to `true` (backward compatible)

**Saving behavior:**
- All three debug settings persisted to LittleFS
- Survive reboots
- Configurable via web interface

## Benefits

1. **No recompilation needed** - Debug output can be toggled at runtime via web interface
2. **Persistent configuration** - Settings survive reboots
3. **Selective output** - Can enable only serial, only UDP, both, or neither
4. **Simpler network stack** - UDP broadcast is simpler than TCP connection management
5. **More reliable** - No connection state to maintain or clean up
6. **Better separation** - Debug system is independent of build configuration

## Migration Notes

- **Settings version bump** - Existing settings files will be auto-deleted and recreated with defaults
- **Users must reconfigure** - All settings (heater configs, MQTT settings, etc.) will need to be re-entered via web interface
- **TCP debug removed** - Any systems relying on TCP debug output must switch to UDP
- **UDP address and port** - Default 192.168.1.3:8086, both configurable via web interface

## Testing Recommendations

1. **Serial output**
   - Enable serial debug only, disable UDP debug
   - Verify output appears on Serial Monitor
   - Verify no network traffic

2. **UDP output**
   - Enable UDP debug only, disable serial debug
   - Configure UDP debug address to your PC's IP
   - Use UDP listener (e.g., `nc -u -l 8086` or `socat UDP-RECV:8086 -`)
   - Verify UDP packets received at configured address
   - Verify no serial output
   - Test with invalid IP address - should fail gracefully without errors

3. **Both enabled**
   - Enable both serial and UDP debug
   - Verify output on both channels simultaneously

4. **Both disabled**
   - Disable both debug options
   - Verify no debug output on either channel
   - Verify system continues to function normally

5. **Settings persistence**
   - Configure debug settings via web interface
   - Reboot controller
   - Verify settings retained after reboot

6. **Web interface**
   - Verify checkboxes reflect current state
   - Verify UDP port field shows current value
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

- Debug functions check `ethConnected` flag before attempting UDP broadcast
- UDP broadcast address is 255.255.255.255 (entire subnet)
- Each UDP debug call sends a separate packet (not buffered)
- Debug stack function includes "Free stack: " prefix for consistency
- Settings default to enabled if not present (backward compatible upgrade path)

