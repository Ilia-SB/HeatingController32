# Reboot History Logging System

**Date:** 2025-10-10  
**Type:** Feature Addition  
**Files Modified:** `include/Config.h`, `include/Settings.h`, `src/main.cpp`

## Overview

Implemented a comprehensive reboot history logging system that tracks all system reboots with timestamps, boot counter, and reboot reasons. The system maintains up to 50 historical entries in persistent storage.

## Features

1. **NTP Time Synchronization** - Real-time timestamps for all reboot entries
2. **Persistent Boot Counter** - Tracks number of boots across power cycles
3. **Reboot History Log** - Maintains last 50 reboot entries with automatic trimming
4. **Web Interface Access** - View reboot history via HTTP endpoint

## Changes Made

### 1. Config.h - Added Constants

Added NTP and reboot history configuration:

```cpp
//NTP
static const char* NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 0;        // Default: UTC (0 seconds)
static const int DAYLIGHT_OFFSET_SEC = 0;    // Default: no daylight saving

//Reboot History
#define BOOT_COUNTER_FILE "/boot_counter.txt"
#define REBOOT_LOG_FILE "/reboot.log"
#define MAX_REBOOT_HISTORY_ENTRIES 50
```

### 2. Settings.h - Added NTP Configuration Fields

Added NTP settings to Settings class:

```cpp
#define SETTINGS_NTP_SERVER         "ntpServer"
#define SETTINGS_GMT_OFFSET         "gmtOffset"
#define SETTINGS_DAYLIGHT_OFFSET    "daylightOffset"

class Settings {
public:
    String ntpServer;
    long gmtOffset;
    int daylightOffset;
    // ... other fields
};
```

### 3. main.cpp - Core Implementation

#### Added Header

```cpp
#include <time.h>
```

#### Updated Settings Functions

- `setDefaultSettings()` - Initialize NTP settings with defaults
- `saveSettings()` - Persist NTP settings to JSON
- `loadSettings()` - Load NTP settings from JSON with backward compatibility

#### New Functions

**`initNTP()`**
- Initializes NTP client with configured server and timezone offsets
- Called during system startup after network initialization

**`getFormattedTimestamp()`**
- Returns formatted timestamp string: "YYYY-MM-DD HH:MM:SS"
- Returns "No time sync" if NTP hasn't synchronized yet

**`getNextBootNumber()`**
- Reads the last boot number from `/reboot.log`
- Parses the last line to extract boot number
- Returns last boot number + 1
- Returns 1 if file doesn't exist (first boot)
- No separate boot counter file needed

**`appendRebootEntry(timestamp, bootNum, reason)`**
- Appends formatted entry to `/reboot.log`
- Format: "N. [YYYY-MM-DD HH:MM:SS] Reason"
- Example: "42. [2025-10-10 14:23:15] Task watchdog"

**`trimRebootHistory()`**
- Reads all entries from reboot log
- If more than 50 entries exist, keeps only the last 50
- Rewrites file with trimmed content

**`manageRebootHistory(bootNum, reason)`**
- Main orchestration function
- Gets current timestamp
- Appends new reboot entry
- Trims history to maintain max 50 entries

#### Integration in setup()

Added after settings initialization and before sensor initialization:

```cpp
// Get reboot reason
const char* resetReason = getResetReason();
debugPrint("Last reboot reason: "); debugPrintln(resetReason);

// Get next boot number from reboot log
uint32_t bootNumber = getNextBootNumber();
debugPrint("Boot #"); debugPrintln(bootNumber);

// Initialize NTP
initNTP();

// Wait for time sync (3 second timeout)
debugPrint("Waiting for NTP time sync");
unsigned long ntpStart = millis();
bool timeSynced = false;
while (millis() - ntpStart < 3000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        timeSynced = true;
        break;
    }
    debugPrint(".");
    delay(500);
}
debugPrintln();
if (timeSynced) {
    debugPrint("Time synced: "); debugPrintln(getFormattedTimestamp());
} else {
    debugPrintln("NTP sync timeout - logging with 'No time sync'");
}

// Manage reboot history (appends entry and trims to 50)
manageRebootHistory(bootNumber, resetReason);
```

#### Web Server Endpoint

Added HTTP endpoint to view reboot history:

```cpp
server.on("/reboot.log", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (LittleFS.exists(REBOOT_LOG_FILE)) {
        request->send(LittleFS, REBOOT_LOG_FILE, "text/plain");
    } else {
        request->send(200, "text/plain", "No reboot history available");
    }
});
```

## File Structure

### `/reboot.log`
Single file storing reboot history entries (up to 50). Boot number is derived from the last entry:
```
1. [2025-10-10 08:15:23] Power-on reset
2. [2025-10-10 09:30:45] Software reset
3. [2025-10-10 10:45:12] Task watchdog
...
50. [2025-10-10 14:23:15] External pin reset
```

**Note:** No separate boot counter file is needed. The boot number is automatically calculated from the last entry in the log.

## Usage

### Viewing Reboot History

1. **Via Web Interface:**
   - Navigate to: `http://<device-ip>/reboot.log`
   - Displays plain text file with reboot history

2. **Via Debug Output:**
   - Each boot displays in serial/TCP debug output:
   ```
   HeatingController32 [version] starting...
   Debug output enabled
   Last reboot reason: Task watchdog
   Boot #42
   Initializing NTP with server: pool.ntp.org
   Waiting for NTP time sync...
   Time synced: 2025-10-10 14:23:15
   Reboot entry added: 42. [2025-10-10 14:23:15] Task watchdog
   ```

### Configuring NTP Settings

NTP settings are stored in `/settings.cfg` and can be configured:

- **NTP Server:** Default is "pool.ntp.org"
- **GMT Offset:** Timezone offset in seconds (e.g., -18000 for EST)
- **Daylight Offset:** Daylight saving offset in seconds (e.g., 3600 for +1 hour)

Settings are loaded on boot and can be updated via the settings file.

## Reboot Reason Types

The system tracks all ESP32 reset reasons:

| Reason | Description |
|--------|-------------|
| Power-on reset | Normal power cycle |
| External pin reset | Hardware reset button pressed |
| Software reset | ESP.restart() or similar |
| Exception/panic | System crash/exception |
| Interrupt watchdog | Interrupt watchdog timeout |
| Task watchdog | Task watchdog timeout (FreeRTOS) |
| Other watchdog | Other watchdog timer reset |
| Deep sleep wake-up | Wake from deep sleep |
| Brownout reset | Voltage dropped below threshold |
| SDIO reset | Reset via SDIO interface |
| Unknown | Reason not determined |

## Behavior Details

### Boot Counter
- Automatically derived from last entry in `/reboot.log`
- Increments on every boot (including all reset types)
- No separate counter file needed
- Resets to 1 if log file is deleted

### History Log
- Appends entry for every boot
- All reset types are logged (including software resets)
- Automatically trims to last 50 entries
- Persists across power cycles

### NTP Time Sync
- Non-blocking initialization
- 3-second timeout for initial sync
- If sync fails, logs with "No time sync" timestamp
- Background sync continues after timeout

## Testing Recommendations

1. **Normal Power Cycle:**
   - Power off/on device
   - Verify "Power-on reset" entry with timestamp

2. **Software Reset:**
   - Trigger ESP.restart()
   - Verify "Software reset" entry is logged
   - Verify history is NOT deleted (changed from original requirement)

3. **External Reset:**
   - Press hardware reset button
   - Verify "External pin reset" entry

4. **Watchdog Timeout:**
   - Create intentional infinite loop
   - Verify watchdog entry appears

5. **History Trimming:**
   - Generate 55+ reboot entries
   - Verify only last 50 are kept

6. **NTP Sync:**
   - Verify timestamps are correct
   - Test with network disconnected (should log "No time sync")

7. **Web Endpoint:**
   - Access `http://<device-ip>/reboot.log`
   - Verify log displays correctly

## Memory Considerations

- Reboot log: ~60 bytes per entry × 50 = ~3KB maximum
- Stack usage in trimRebootHistory: ~3KB temporary (String array)
- Stack usage in getNextBootNumber: minimal (single String for last line)
- Total persistent storage: ~3KB (single file)

## Benefits

1. **Diagnostics:** Quickly identify causes of unexpected reboots
2. **Reliability Tracking:** Monitor system stability over time
3. **Debugging:** Historical view of all system restarts
4. **Remote Monitoring:** Web access to reboot history
5. **Root Cause Analysis:** Timestamp correlation with other events

## Future Enhancements

Possible improvements:
- Add MQTT reporting of reboot events
- Export reboot history as JSON
- Add reboot statistics (count by type)
- Configurable history size via settings
- Add uptime tracking
- Email notifications on specific reset types

## Notes

- NTP settings are backward compatible with existing installations
- If NTP sync fails, system continues normally with fallback timestamp
- Reboot history is independent of debug output settings
- Web endpoint accessible without authentication (consider security for production)
- Boot number is automatically derived from the log file (no separate counter file)
- If log file is deleted, boot counter resets to 1

