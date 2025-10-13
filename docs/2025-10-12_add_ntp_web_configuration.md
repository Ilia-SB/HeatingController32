# Add NTP Configuration to Web Interface

**Date:** 2025-10-12  
**Type:** Feature Enhancement  
**Files Modified:** `src/main.cpp`

## Overview

Added web interface controls for NTP server, GMT offset, and daylight saving settings. These settings were already present in the Settings class and properly saved/loaded, but were not exposed to users through the web interface.

## Problem Statement

The HeatingController32 system uses NTP for time synchronization and maintains timestamped reboot logs. However, users needed to modify `include/Config.h` and recompile the firmware to change:
- NTP server address
- GMT timezone offset
- Daylight saving time offset

This was inconvenient for users in different timezones or those wanting to use a local NTP server.

## Solution

Added three new input fields to the Global Settings section of the web interface:
1. **NTP Server** - text input for NTP server address (e.g., "pool.ntp.org")
2. **GMT Offset (hours)** - number input with range -12 to +14 for timezone
3. **Daylight Saving (hours)** - number input with range 0 to 1 for DST

## Changes Made

### 1. Web Form HTML Generation

Added three new table rows in `webServerPlaceholderProcessor()` function after the debug settings (lines 875-883):

```cpp
retValue += "<tr><td class=\"name\">NTP Server</td><td class=\"value\"><input type=\"text\" name=\"ntpServer\" value=\"";
retValue += settings.ntpServer;
retValue += "\"></td></tr>";
retValue += "<tr><td class=\"name\">GMT Offset (hours)</td><td class=\"value\"><input type=\"number\" name=\"gmtOffsetHours\" min=\"-12\" max=\"14\" value=\"";
retValue += String(settings.gmtOffsetHours);
retValue += "\"></td></tr>";
retValue += "<tr><td class=\"name\">Daylight Saving (hours)</td><td class=\"value\"><input type=\"number\" name=\"daylightOffsetHours\" min=\"0\" max=\"1\" value=\"";
retValue += String(settings.daylightOffsetHours);
retValue += "\"></td></tr>";
```

### 2. Form Parameter Processing

Added parameter handling in `processSettingsForm()` function (lines 1228-1236):

```cpp
if (request->hasParam(SETTINGS_NTP_SERVER, true)) {
    settings.ntpServer = request->getParam(SETTINGS_NTP_SERVER, true)->value();
}
if (request->hasParam(SETTINGS_GMT_OFFSET, true)) {
    settings.gmtOffsetHours = request->getParam(SETTINGS_GMT_OFFSET, true)->value().toInt();
}
if (request->hasParam(SETTINGS_DAYLIGHT_OFFSET, true)) {
    settings.daylightOffsetHours = request->getParam(SETTINGS_DAYLIGHT_OFFSET, true)->value().toInt();
}
```

## Usage

1. Navigate to the **Settings** page in the web interface
2. Click on **Global Settings** to expand the section
3. Locate the new NTP configuration fields:
   - **NTP Server**: Enter your preferred NTP server (default: "pool.ntp.org")
   - **GMT Offset (hours)**: Enter your timezone offset from UTC (e.g., 3 for GMT+3, -5 for GMT-5)
   - **Daylight Saving (hours)**: Enter 1 if currently observing DST, 0 otherwise
4. Click **Save**
5. System will reboot and apply the new NTP settings

## Examples

### Moscow (GMT+3, No DST)
- NTP Server: `pool.ntp.org`
- GMT Offset: `3`
- Daylight Saving: `0`

### New York (GMT-5, With DST)
- NTP Server: `pool.ntp.org`
- GMT Offset: `-5`
- Daylight Saving: `1` (during DST period)

### Dubai (GMT+4, No DST)
- NTP Server: `pool.ntp.org`
- GMT Offset: `4`
- Daylight Saving: `0`

### Using Local NTP Server
- NTP Server: `192.168.1.100` (your local NTP server IP)
- GMT Offset: (your timezone)
- Daylight Saving: (0 or 1 as applicable)

## Technical Details

### Existing Infrastructure

No changes were needed to the underlying Settings infrastructure:
- Settings class already had `ntpServer`, `gmtOffsetHours`, `daylightOffsetHours` fields (include/Settings.h)
- `setDefaultSettings()` already initialized these fields from Config.h defaults
- `saveSettings()` already persisted these fields to `/settings.cfg`
- `loadSettings()` already loaded these fields with backward compatibility
- `initNTP()` already used these settings to configure NTP client

### Settings Persistence

Settings are stored in `/settings.cfg` as JSON:
```json
{
  "settingsVersion": 3,
  "hysteresis": 1,
  "mqttUrl": "192.168.1.3",
  "mqttPort": 1883,
  "ntpServer": "pool.ntp.org",
  "gmtOffsetHours": 3,
  "daylightOffsetHours": 0,
  "consumptionLimit": [5500, 5500, 5500]
}
```

### Application of Settings

When settings are saved:
1. Form data is captured and written to `settings` object
2. `saveSettings()` persists to `/settings.cfg`
3. System reboots automatically
4. During startup, `loadSettings()` reads the configuration
5. `initNTP()` applies the new NTP settings using `configTime()`

### Validation

- **GMT Offset**: HTML5 number input enforces range -12 to +14
- **Daylight Saving**: HTML5 number input enforces range 0 to 1
- **NTP Server**: Text input, no client-side validation (standard NTP server names or IP addresses accepted)

## Testing Recommendations

1. **Default Values**: Verify that default settings from Config.h are displayed correctly
2. **Save and Load**: Change settings, save, verify they persist after reboot
3. **NTP Sync**: Verify timestamps in reboot log reflect correct timezone
4. **Edge Cases**: Test negative GMT offsets, maximum/minimum values
5. **Invalid Input**: Test with invalid NTP server to ensure graceful handling

## Related Documentation

- `docs/2025-10-10_ntp_timezone_configuration.md` - Comprehensive timezone configuration guide
- `docs/2025-10-10_add_reboot_history_logging.md` - Original NTP implementation for reboot logs
- `include/Config.h` - Default NTP configuration constants

## Benefits

- **User-Friendly**: No firmware recompilation needed to change timezone
- **Flexibility**: Users can specify custom NTP servers (e.g., local NTP server for faster sync)
- **Proper Timestamps**: Reboot logs and other timestamped data will show correct local time
- **Multi-Region Deployment**: Same firmware can be deployed in different timezones

## Notes

- The system reboots after saving settings to ensure NTP is re-initialized with new values
- Settings are backward compatible - existing installations will continue to work with defaults
- The constants `SETTINGS_NTP_SERVER`, `SETTINGS_GMT_OFFSET`, and `SETTINGS_DAYLIGHT_OFFSET` were already defined in Settings.h
- No changes to JSON_DOCUMENT_SIZE_SETTINGS were needed as the fields were already accounted for

