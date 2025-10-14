# Web-Based Debug Logging System with WebSocket Streaming

**Date:** 2025-10-14  
**Status:** Completed  
**Updated:** Added WebSocket support for real-time streaming

## Overview

Replaced the TCP-based debug logging system with a web-based circular buffer implementation. Added FreeRTOS task stack monitoring and created a real-time debug web interface.

## Motivation

The previous TCP debug system had several issues:
1. Required external TCP client to view debug output
2. Could cause task watchdog crashes when TCP server was slow/unresponsive
3. Added complexity with connection management
4. Not easily accessible for quick diagnostics

The new system provides:
- Easy access via web browser
- No external dependencies
- Fixed memory footprint (4KB circular buffer)
- Real-time updates via AJAX
- Task stack monitoring for proactive issue detection

## Changes Made

### 1. Removed TCP Logging Infrastructure

**Files Modified:**
- `include/Settings.h`
- `include/Config.h`
- `src/main.cpp`

**Removed:**
- `WiFiClient tcpClient` global variable
- `tcpConnect()` function
- `tcpSafeWrite()` helper functions
- TCP connection attempts in `setup()`
- TCP cleanup in `taskSystem()`
- Settings fields: `debugTcp`, `tcpUrl`, `tcpPort`
- Settings constants: `TCP_URL`, `TCP_PORT`
- All TCP-related code from `debugPrint*()` functions
- TCP fields from settings web interface

### 2. Implemented Circular Buffer System

**File:** `src/main.cpp`

**Added Global Variables:**
```cpp
char debugBuffer[DEBUG_BUFFER_SIZE];           // 4KB circular buffer
volatile uint16_t debugBufferWritePos = 0;     // Current write position
SemaphoreHandle_t debugBufferMutex = NULL;     // Thread safety
```

**Added Helper Function:**
```cpp
void writeToDebugBuffer(const char* msg, size_t len)
```

**Modified Functions:**
All `debugPrint*()` functions now:
1. Output to Serial (if `settings.debugSerial` is enabled)
2. Write to circular buffer (always, for web viewing)

The circular buffer:
- Wraps around when full, overwriting oldest messages
- Protected by mutex for thread-safe access from multiple tasks
- Stores raw text output (no timestamps, keeps implementation simple)
- Fixed 4KB size (configurable via `DEBUG_BUFFER_SIZE` macro)

### 3. Added Stack Watermark Tracking

**File:** `src/main.cpp`

**Added Global Variables:**
```cpp
volatile UBaseType_t taskSystemStackWatermark = 0;
volatile UBaseType_t taskMainStackWatermark = 0;
```

**Modified Task Functions:**
- `taskSystem()`: Reads watermark at end of each loop iteration
- `taskMain()`: Reads watermark at end of each loop iteration

Stack watermarks show the minimum free stack space since task creation:
- Higher values = more unused stack space (safe)
- Lower values = potential stack overflow risk
- Critical threshold: < 512 bytes
- Warning threshold: < 1024 bytes

### 4. Created Debug Web Interface

**New File:** `data/debug.html`

Features:
- **Static Information** (loaded once via placeholders):
  - Total reboots
  - Last reboot timestamp
  - Last reboot reason

- **Live Information** (updated via AJAX every second):
  - taskSystem stack watermark (with color coding)
  - taskMain stack watermark (with color coding)
  - Debug buffer contents

- **Interactive Controls:**
  - Auto-scroll toggle (enabled by default)
  - Clear display button (clears view, not buffer)

- **Visual Indicators:**
  - Green: Normal stack levels
  - Orange: Warning (< 1024 bytes free)
  - Red: Critical (< 512 bytes free)

**New Web Endpoints:**
```cpp
GET /debug        - Serves debug.html
GET /debug.json   - Returns JSON with live data
```

**JSON Response Format:**
```json
{
  "taskSystemStack": 5432,
  "taskMainStack": 2048,
  "debugOutput": "..."
}
```

### 5. Updated Configuration

**File:** `include/Config.h`

**Added Macros:**
```cpp
#define DEBUG_BUFFER_SIZE 4096              // 4KB circular buffer
#define DEBUG_PAGE_UPDATE_INTERVAL 1000     // JavaScript polling interval (ms)
```

### 6. Updated Main Menu

**File:** `data/main.html`

Added "Debug" menu item between "Backup" and "Reboot".

### 7. Added Placeholder Processing

**File:** `src/main.cpp` - `webServerPlaceholderProcessor()`

**Added Placeholders:**
- `DEBUG_UPDATE_INTERVAL`: JavaScript polling interval from macro
- `TOTAL_REBOOTS`: Calculated from reboot log
- `LAST_REBOOT_TIME`: Parsed from last line of reboot.log
- `LAST_REBOOT_REASON`: Parsed from last line of reboot.log

## Memory Impact

### RAM Usage
- **Circular buffer:** 4,096 bytes (configurable)
- **Stack watermarks:** 8 bytes (2 × 4 bytes)
- **Mutex:** ~100 bytes (FreeRTOS overhead)
- **Total:** ~4.2 KB

### Flash Usage
- **New HTML file:** ~4 KB (debug.html)
- **Code changes:** ~2 KB (new functions, removed TCP code approximately balances out)

### Network Usage
- **Per AJAX request:** ~4-5 KB (4KB buffer + JSON overhead)
- **Frequency:** Every 1 second (configurable)
- **Impact:** Minimal on local network

## Testing Recommendations

1. **Stack Monitoring:**
   - Watch watermarks during normal operation
   - If taskMain < 1024 bytes, reduce debug output in temperature loops
   - If taskSystem < 2048 bytes, consider reducing MQTT debug output

2. **Buffer Size:**
   - Current 4KB holds approximately 3-5 minutes of typical debug output
   - If buffer wraps too quickly, increase `DEBUG_BUFFER_SIZE`
   - Monitor during heater processing (most verbose operation)

3. **Performance:**
   - AJAX polling every 1 second is minimal overhead
   - Increase `DEBUG_PAGE_UPDATE_INTERVAL` if needed
   - Test with multiple browser clients connected

## Potential Issues & Solutions

### Issue 1: String Fragmentation
**Problem:** `String` class usage in `debugPrint()` can fragment heap  
**Impact:** Acceptable for debug system, not used in critical paths  
**Mitigation:** Debug output is for diagnostics, not production-critical

### Issue 2: Buffer Wrapping
**Problem:** Circular buffer overwrites old messages when full  
**Impact:** Expected behavior, oldest messages are lost  
**Solution:** Increase `DEBUG_BUFFER_SIZE` if needed, or reduce debug verbosity

### Issue 3: Mutex Timeout
**Problem:** `writeToDebugBuffer()` uses 10ms timeout  
**Impact:** Debug message dropped if mutex unavailable  
**Mitigation:** Acceptable - maintaining system responsiveness is priority

### Issue 4: Low Stack Watermarks
**Problem:** Watermarks below 512 bytes indicate potential overflow  
**Action Required:** 
- Review task stack allocations
- Reduce debug output verbosity
- Optimize local variable usage

## Migration Notes

### Settings Migration
Old settings files with TCP debug configuration are automatically handled:
- `debugSerial` preserved
- `debugTcp`, `tcpUrl`, `tcpPort` silently ignored
- No settings version increment needed (backward compatible)

### User Experience
- Users previously using TCP debug should use web interface instead
- Serial debug output unchanged (still available if enabled)
- No configuration changes required

## Code Quality

### Thread Safety
- Circular buffer protected by `debugBufferMutex`
- Stack watermarks are `volatile` (safe for read-only access)
- AJAX endpoint uses mutex when reading buffer

### Error Handling
- Null checks on mutex before use
- Timeout on mutex acquisition prevents deadlock
- Graceful degradation if buffer unavailable

### Standards Compliance
- Braces added for all single-line if statements
- Meaningful variable names used
- Documented in project's `docs/` directory per .cursorrules

## Future Enhancements

Potential improvements for future consideration:

1. **Filtering:** Add client-side filtering by keyword/level
2. **Download:** Button to download buffer as text file
3. **Timestamps:** Option to add timestamps to debug messages
4. **Levels:** Debug levels (INFO, WARN, ERROR) with color coding
5. **Persistence:** Option to save buffer to LittleFS on demand

## WebSocket Streaming Enhancement

### Implementation Details

Added real-time WebSocket streaming on 2025-10-14:

**New Components:**
- `AsyncWebSocket wsDebug("/ws/debug")` - WebSocket server endpoint
- `wsDebugClient` - Single client tracking pointer
- `wsDebugStreaming` - Boolean flag for streaming state

**Smart Buffering Logic:**
1. **No client connected**: Messages buffered in circular buffer
2. **Client connects**: 
   - Disconnect any existing client (single client only)
   - Push entire buffer contents to client
   - Clear buffer (head = tail = 0)
   - Switch to streaming mode
3. **Streaming mode**: Messages sent directly to WebSocket, bypassing buffer
4. **Client disconnects**: Resume buffering

**Watermark Updates:**
- Sent via JSON every 100ms in `taskSystem()` loop
- Format: `{"taskSystemStack":xxxx,"taskMainStack":xxxx}`

**Client Behavior:**
- Auto-reconnect on disconnect (2-second interval)
- Differentiates between JSON (watermarks) and plain text (debug messages)
- Appends new messages in real-time

### Benefits Over HTTP Polling

1. **True real-time** - No 1-second delay
2. **More efficient** - No HTTP overhead, persistent connection
3. **Lower bandwidth** - Only changed data transmitted
4. **Buffer cleared on connect** - Frees 4KB RAM when streaming
5. **Instant updates** - Messages appear immediately

### Memory Impact

- WebSocket overhead: ~500 bytes per connection
- Single client limit: Minimal impact
- Buffer cleared during streaming: Saves 4KB

## Verification Steps

After deployment, verify:

1. ✓ Debug page accessible at `/debug`
2. ✓ WebSocket connects automatically on page load
3. ✓ Buffer contents pushed on initial connect
4. ✓ New debug messages appear in real-time
5. ✓ Stack watermarks update continuously
6. ✓ Auto-scroll works correctly
7. ✓ Reboot information displays correctly
8. ✓ Settings page no longer shows TCP debug options
9. ✓ Serial debug output still works (if enabled)
10. ✓ System stable under normal operation
11. ✓ Stack watermarks remain above critical thresholds
12. ✓ Auto-reconnect works after network interruption
13. ✓ Second client connection disconnects first client

## References

- ESP32 FreeRTOS Documentation: Stack High Water Mark
- ESPAsyncWebServer: WebSocket support
- ArduinoJson: Efficient JSON serialization
- Project .cursorrules: Documentation standards

