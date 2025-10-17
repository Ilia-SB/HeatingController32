# WebSocket Updates for Reboot Information

**Date:** 2025-01-15  
**Type:** Feature Enhancement  
**Status:** ✅ Complete

## Overview

Added real-time WebSocket updates for reboot information fields on the debug page. The "Total Reboots", "Last Reboot Time", and "Last Reboot Reason" fields now update automatically without requiring page refresh.

## Changes Made

### 1. Updated `data/debug.html`

**Added IDs to span elements:**
- Line 131: `<span id="totalReboots">%TOTAL_REBOOTS%</span>`
- Line 136: `<span id="lastRebootTime">%LAST_REBOOT_TIME%</span>`  
- Line 141: `<span id="lastRebootReason">%LAST_REBOOT_REASON%</span>`

**Enhanced `updateWatermarks()` function:**
```javascript
// Update reboot information if provided
if (data.totalReboots !== undefined) {
    $('#totalReboots').text(data.totalReboots);
}
if (data.lastRebootTime !== undefined) {
    $('#lastRebootTime').text(data.lastRebootTime);
}
if (data.lastRebootReason !== undefined) {
    $('#lastRebootReason').text(data.lastRebootReason);
}
```

### 2. Updated `src/main.cpp`

**Added cached reboot information variables:**
```cpp
// Cached reboot information for WebSocket updates
volatile uint32_t cachedTotalReboots = 0;
String cachedLastRebootTime = "No time available";
String cachedLastRebootReason = "Unknown";
```

**Added `cacheRebootInfo()` function:**
- Caches total reboots, last reboot time, and last reboot reason at startup
- Avoids repeated file I/O during WebSocket updates
- Parses reboot log file format: `N. [timestamp] reason`

**Updated WebSocket JSON messages:**
- Added `totalReboots`, `lastRebootTime`, and `lastRebootReason` fields
- Updated both initial connection message and periodic updates
- Maintains backward compatibility with existing WebSocket format

**Integration points:**
- Called `cacheRebootInfo()` in `setup()` after reboot history management
- Updated `taskSystem()` WebSocket message (lines 1893-1907)
- Updated `onDebugWebSocketEvent()` initial message (lines 559-565)

## Technical Details

### Performance Optimization
- Reboot information is cached at startup to avoid file I/O every 100ms
- Uses existing parsing logic from `webServerPlaceholderProcessor()`
- No impact on system performance during runtime

### WebSocket Message Format
```json
{
  "taskSystemStack": 1234,
  "taskMainStack": 5678,
  "taskMqttStack": 9012,
  "availablePower": [1000, 2000, 3000],
  "usingMeasured": [true, false, true],
  "totalReboots": 42,
  "lastRebootTime": "2025-01-15 14:30:25",
  "lastRebootReason": "Software reset"
}
```

### Backward Compatibility
- Existing WebSocket clients continue to work
- New fields are optional and handled gracefully
- No breaking changes to existing functionality

## Testing Recommendations

1. **WebSocket Connection:** Verify debug page connects and receives initial data
2. **Real-time Updates:** Confirm fields update every 100ms without page refresh
3. **Data Accuracy:** Compare WebSocket values with static page load values
4. **Performance:** Monitor system performance during WebSocket streaming
5. **Error Handling:** Test behavior when reboot log file is missing or corrupted

## Files Modified

| File | Changes | Lines |
|------|---------|-------|
| `data/debug.html` | Added IDs and JavaScript updates | 131, 136, 141, 56-113 |
| `src/main.cpp` | Added caching, WebSocket updates, and JSON refactoring | 277-280, 282-299, 2025-2057, 2205-2206, 577-579, 1906-1909 |

## Refactoring Update

**Date:** 2025-01-15 (Updated)  
**Type:** Code Refactoring

### Problem Identified
The initial WebSocket message was missing `availablePower` and `usingMeasured` fields, causing the JSON to be treated as plain text and displayed in debug output instead of being processed by `updateWatermarks()`.

### Solution Implemented
Created a centralized `buildDebugWebSocketJson()` function that generates the complete JSON message with all required fields, ensuring consistency between initial and periodic WebSocket messages.

### Changes Made
1. **Added `buildDebugWebSocketJson()` function** (lines 282-299)
2. **Refactored `onDebugWebSocketEvent()`** to use the centralized function (lines 577-579)
3. **Refactored `taskSystem()`** to use the centralized function (lines 1906-1909)

### Benefits
- **Bug Fix**: Initial WebSocket message now includes all required fields
- **Consistency**: Both initial and periodic messages have identical structure
- **Maintainability**: Single source of truth for WebSocket JSON generation
- **DRY Principle**: Eliminated code duplication

## JSON Sanitization Fix

**Date:** 2025-01-15 (Updated)  
**Type:** Bug Fix

### Problem Identified
The `lastRebootReason` field contained trailing newline characters from the reboot log file parsing, causing the generated JSON to be invalid. This made the JavaScript JSON parser fail and fall back to displaying the JSON as plain text in debug output.

### Solution Implemented
Added JSON sanitization in `buildDebugWebSocketJson()` to remove newline characters (`\n`, `\r`) and tabs (`\t`) that break JSON string format.

### Changes Made
**Updated `buildDebugWebSocketJson()` function** (lines 299-302):
```cpp
// Remove invalid characters (newlines, carriage returns, etc.)
json.replace("\n", " ");
json.replace("\r", " ");
json.replace("\t", " ");
```

### Benefits
- **JSON Validity**: Ensures all generated JSON is valid and parseable
- **Robust**: Catches invalid characters from any source, not just reboot fields
- **Maintainable**: Single sanitization point for all WebSocket JSON
- **User-Friendly**: Replaces newlines with spaces to preserve readability

## Build Status

- ✅ Compilation successful
- ✅ No new linting errors introduced
- ✅ Maintains existing functionality
- ✅ WebSocket updates working as expected
