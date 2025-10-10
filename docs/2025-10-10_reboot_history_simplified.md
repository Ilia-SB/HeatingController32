# Reboot History - Simplified Boot Counter

**Date:** 2025-10-10  
**Type:** Simplification  
**Build:** 582

## Change Made

Simplified the boot counter implementation to use only `/reboot.log` file instead of maintaining a separate `/boot_counter.txt` file.

## Before

- Boot counter stored in separate file: `/boot_counter.txt`
- System maintained two files: `/boot_counter.txt` and `/reboot.log`

## After

- Boot counter derived from last entry in `/reboot.log`
- System maintains only one file: `/reboot.log`
- Boot number parsed from last line in format: `N. [timestamp] reason`

## Implementation Details

### Function Changed: `getNextBootNumber()`

Replaces `loadBootCounter()` and `saveBootCounter()`:

```cpp
uint32_t getNextBootNumber() {
    if (!LittleFS.exists(REBOOT_LOG_FILE)) {
        return 1;
    }
    
    File file = LittleFS.open(REBOOT_LOG_FILE, FILE_READ);
    if (!file) {
        return 1;
    }
    
    // Read the last line to get the last boot number
    String lastLine = "";
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) {
            lastLine = line;
        }
    }
    file.close();
    
    if (lastLine.length() == 0) {
        return 1;
    }
    
    // Parse boot number from format "N. [timestamp] reason"
    int dotIndex = lastLine.indexOf('.');
    if (dotIndex > 0) {
        uint32_t lastBootNum = lastLine.substring(0, dotIndex).toInt();
        return lastBootNum + 1;
    }
    
    return 1;
}
```

### Config.h Change

**Before:**
```cpp
#define BOOT_COUNTER_FILE "/boot_counter.txt"
#define REBOOT_LOG_FILE "/reboot.log"
```

**After:**
```cpp
#define REBOOT_LOG_FILE "/reboot.log"
```

### setup() Integration

**Before:**
```cpp
uint32_t bootCounter = loadBootCounter();
manageRebootHistory(bootCounter, resetReason);
saveBootCounter(bootCounter + 1);
```

**After:**
```cpp
uint32_t bootNumber = getNextBootNumber();
manageRebootHistory(bootNumber, resetReason);
```

## Benefits

1. **Simpler:** Only one file to manage
2. **Less I/O:** No separate counter file operations
3. **Atomic:** Boot number and log entry are inherently synchronized
4. **Self-correcting:** If counter gets out of sync, it auto-corrects from log

## Edge Cases Handled

| Scenario | Behavior |
|----------|----------|
| First boot (no log file) | Returns 1 |
| File exists but empty | Returns 1 |
| Log file corrupted/unreadable | Returns 1 (safe fallback) |
| Last line has no dot | Returns 1 (safe fallback) |
| Normal operation | Returns last boot # + 1 |

## Memory Impact

**Saved:**
- No separate boot counter file (~10 bytes persistent)
- No `saveBootCounter()` function
- Slightly reduced code size

**Added:**
- Minimal - single String variable for last line during parsing

## Testing

✅ **Build Status:** Successful (Build 582)
- RAM: 15.1% (49,628 bytes)
- Flash: 77.0% (1,008,753 bytes)
- 52 bytes saved vs previous build

## Compatibility

- Existing `/boot_counter.txt` files (if any) will be ignored
- Boot counter will restart from 1 or continue from last log entry
- No data migration needed

## Example Log

```
1. [2025-10-10 08:15:23] Power-on reset
2. [2025-10-10 09:30:45] Software reset
3. [2025-10-10 10:45:12] Task watchdog
```

On next boot:
- System reads last line: "3. [2025-10-10 10:45:12] Task watchdog"
- Parses boot number: 3
- Next boot will be: 4

## Recommendation

This simplification improves the implementation by reducing file operations and maintaining a single source of truth for boot history.

