# NTP Timezone Configuration Reference

**Date:** 2025-10-10  
**Type:** Configuration Guide

## Quick Start

To set your timezone, edit `include/Config.h`:

```cpp
static const int GMT_OFFSET_HOURS = 3;       // For GMT+3 (Moscow, Istanbul, Riyadh)
static const int DAYLIGHT_OFFSET_HOURS = 0;  // 1 if using daylight saving, 0 otherwise
```

## How It Works

The system automatically converts hours to seconds when calling NTP:
- Hours are multiplied by 3600 (seconds in an hour)
- Example: GMT+3 → 3 × 3600 = 10800 seconds

## Common Timezone Settings

### Europe

| Location | GMT Offset | Daylight Saving |
|----------|-----------|-----------------|
| London (GMT/BST) | 0 | 1 |
| Paris (CET) | 1 | 1 |
| Athens (EET) | 2 | 1 |
| Moscow (MSK) | 3 | 0 |

### North America

| Location | GMT Offset | Daylight Saving |
|----------|-----------|-----------------|
| New York (EST) | -5 | 1 |
| Chicago (CST) | -6 | 1 |
| Denver (MST) | -7 | 1 |
| Los Angeles (PST) | -8 | 1 |

### Asia

| Location | GMT Offset | Daylight Saving |
|----------|-----------|-----------------|
| Dubai (GST) | 4 | 0 |
| India (IST) | 5 | 0 |
| China (CST) | 8 | 0 |
| Tokyo (JST) | 9 | 0 |

### Other

| Location | GMT Offset | Daylight Saving |
|----------|-----------|-----------------|
| UTC | 0 | 0 |
| Sydney (AEDT) | 11 | 1 |
| Auckland (NZDT) | 13 | 1 |

## Configuration Methods

### Method 1: Config.h (Default)

Edit `include/Config.h`:

```cpp
static const int GMT_OFFSET_HOURS = 3;       // Your timezone
static const int DAYLIGHT_OFFSET_HOURS = 0;  // DST if applicable
```

Then recompile and upload firmware.

### Method 2: Settings File

Edit `/settings.cfg` on the ESP32 filesystem:

```json
{
  "ntpServer": "pool.ntp.org",
  "gmtOffsetHours": 3,
  "daylightOffsetHours": 0
}
```

No recompile needed - takes effect on next reboot.

### Method 3: Web Interface (Future)

If settings web interface is implemented, you can configure via web UI.

## Examples

### Moscow (GMT+3, No DST)
```cpp
static const int GMT_OFFSET_HOURS = 3;
static const int DAYLIGHT_OFFSET_HOURS = 0;
```

Result: `2025-10-10 17:30:00` when UTC is `14:30:00`

### New York (GMT-5, With DST)
```cpp
static const int GMT_OFFSET_HOURS = -5;
static const int DAYLIGHT_OFFSET_HOURS = 1;
```

Result during DST: `2025-10-10 10:30:00` when UTC is `14:30:00`  
Result outside DST: `2025-10-10 09:30:00` when UTC is `14:30:00`

### Dubai (GMT+4, No DST)
```cpp
static const int GMT_OFFSET_HOURS = 4;
static const int DAYLIGHT_OFFSET_HOURS = 0;
```

Result: `2025-10-10 18:30:00` when UTC is `14:30:00`

## Verification

After setting timezone, check the boot log:

```
Initializing NTP with server: pool.ntp.org
Waiting for NTP time sync...
Time synced: 2025-10-10 17:30:15
```

Or access the reboot log at `http://<device-ip>/reboot.log`:

```
1. [2025-10-10 17:30:15] Power-on reset
```

The timestamp should match your local time.

## Troubleshooting

### Time is Wrong

1. **Check your GMT offset:**
   - Verify the sign (+ for east of UTC, - for west)
   - Verify the number of hours

2. **Check daylight saving:**
   - Set to 1 if currently observing DST
   - Set to 0 otherwise

3. **Check NTP sync:**
   - Look for "Time synced" in boot log
   - If "NTP sync timeout", check network connection

### Timestamp Shows "No time sync"

- Network not connected when NTP tried to sync
- NTP server unreachable
- System will retry sync in background
- Next reboot will have correct timestamp if network is available

## Advanced: Half-Hour Offsets

For timezones with 30-minute offsets (like India IST +5:30), use the nearest hour:

```cpp
static const int GMT_OFFSET_HOURS = 5;  // Use 5 instead of 5.5
```

The 30-minute difference can be adjusted manually if precise timestamps are critical.

Alternatively, modify the code to support fractional hours:
```cpp
static const float GMT_OFFSET_HOURS = 5.5;  // Requires code modification
```

## Notes

- Timezone settings are backward compatible with existing installations
- If settings file doesn't have NTP settings, defaults from Config.h are used
- NTP sync happens once at boot (3-second timeout)
- System continues normally even if NTP sync fails

