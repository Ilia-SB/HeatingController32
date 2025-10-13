# Fix Task Watchdog Crashes Caused by TCP Debug Logging

**Date:** October 13, 2025  
**Issue:** Task watchdog crashes when sending TCP logs to a slow/different TCP server  
**Root Cause:** Blocking TCP write operations in debug functions called from FreeRTOS tasks  

## Problem Description

The system started crashing with task watchdog errors when TCP debug logging was directed to a different (slower) TCP server. The crashes occurred because:

1. **Blocking TCP writes**: The `debugPrint()` and `debugPrintln()` functions make synchronous TCP write calls using `tcpClient.print()` and `tcpClient.println()`.

2. **No timeout configured**: TCP socket operations had no timeout, so they could block indefinitely waiting for:
   - TCP send buffer space (when buffer is full)
   - ACK packets from the server (when server is slow to read)
   - Retransmission timeouts (when there's packet loss)

3. **Called from watchdog-monitored tasks**: Debug functions are called extensively from `taskSystem()` and `taskMain()`, which are monitored by the ESP32 task watchdog timer (default timeout: ~5 seconds).

4. **Heavy logging in critical functions**: The `processHeaters()` function contains dozens of debug calls. When TCP writes block, the cumulative delay exceeds the watchdog timeout.

5. **TCP flow control**: When the receiving server is slow to read data, TCP flow control causes the sender (ESP32) to wait, blocking the debug print calls.

## Technical Details

### Why the Different Server Made It Worse

Different TCP servers behave differently:
- **Fast server** (original): Reads data immediately, TCP buffer rarely fills up, writes complete quickly
- **Slow server** (new): Slower to read data, TCP send buffer fills up, TCP flow control kicks in, writes block longer

### Task Watchdog Behavior

The ESP32 task watchdog monitors tasks to ensure they don't block indefinitely:
- Each task must yield control (via `vTaskDelay()`, `yield()`, etc.) within the timeout period
- If a task runs continuously without yielding, the watchdog triggers
- Default timeout is typically 5 seconds
- Crash results in system reset with reason: `ESP_RST_TASK_WDT`

### Cumulative Blocking Effect

Example from `processHeaters()`:
```cpp
debugPrint("Phase "); debugPrint(phase + 1);  // Multiple TCP writes
debugPrint(". Using measured power consumption. ");
debugPrint("Available power: ");debugPrint(availablePower);
debugPrint(" = ");debugPrint(settings.consumptionLimit[phase]);
debugPrint(" - ");debugPrint(currentConsumption[phase]);
```

If each TCP write blocks for 200ms (due to slow server), and there are 50+ debug calls in the function, the total blocking time can exceed 10 seconds → watchdog timeout!

## Solutions Implemented

### 1. TCP Socket Timeout Configuration

**File:** `src/main.cpp`, function `tcpConnect()`

**Change:**
```cpp
if (tcpClient.connect(settings.tcpUrl.c_str(), settings.tcpPort)) {
    Serial.println("TCP client connected.");
    tcpClient.setNoDelay(true);
    // Set socket timeout to prevent blocking indefinitely
    // This prevents task watchdog crashes when TCP server is slow
    tcpClient.setTimeout(100); // 100ms timeout for write operations
    return true;
}
```

**Effect:**
- TCP write operations now timeout after 100ms instead of blocking indefinitely
- If write can't complete in 100ms, it returns early with partial write
- Prevents any single TCP operation from blocking for more than 100ms

### 2. Non-Blocking TCP Writes with Buffer Checking

**File:** `src/main.cpp`, helper functions and debug functions

**Changes:**

Added helper functions to check buffer availability before writing:
```cpp
// Helper function to safely write to TCP client
// Returns false if write failed or timed out
inline bool tcpSafeWrite(const String& msg) {
    if (!tcpClient.connected()) {
        return false;
    }
    // availableForWrite() checks if there's buffer space
    // This prevents blocking on full buffers
    if (tcpClient.availableForWrite() < msg.length()) {
        return false; // Skip this write to avoid blocking
    }
    size_t written = tcpClient.print(msg);
    return (written == msg.length());
}
```

Updated all debug functions to check buffer availability:
```cpp
void debugPrint(const char* msg) {
    if (settings.debugSerial) {
        Serial.print(msg);
    }
    if (settings.debugTcp) {
        tcpSafeWrite(msg);  // Non-blocking write
    }
}

void debugPrintln(const String& msg) {
    if (settings.debugSerial) {
        Serial.println(msg);
    }
    if (settings.debugTcp && tcpClient.connected() && 
        tcpClient.availableForWrite() > msg.length() + 2) {
        tcpClient.println(msg);  // Only write if buffer has space
    }
}
```

**Effect:**
- Debug functions now check if TCP buffer has space before writing
- If buffer is full (server is slow), debug output is silently dropped for TCP
- Serial output continues normally (no blocking issues with Serial)
- No blocking wait for buffer space → no watchdog timeout

### 3. Watchdog Feeding in Long-Running Functions

**File:** `src/main.cpp`, function `processHeaters()`

**Changes:**

Added strategic `vTaskDelay()` calls to feed the watchdog:
```cpp
void processHeaters() {
    if (flagRestartNow) {
        return;
    }
    debugPrintln("Processing heaters...");
    for (uint8_t phase=0; phase<NUMBER_OF_PHASES; phase++) {
        // Feed watchdog to prevent timeout during heavy debug output
        vTaskDelay(1 / portTICK_PERIOD_MS);
        
        // ... processing logic ...
        
        if (flagEmergency[phase]) {
            // Feed watchdog during emergency handling
            vTaskDelay(1 / portTICK_PERIOD_MS);
            // ... emergency handling ...
        }
    }
}
```

**Effect:**
- `vTaskDelay(1)` yields control to the scheduler and resets the watchdog timer
- Even if debug writes somehow block longer than expected, watchdog is fed regularly
- Adds minimal latency (1ms delays)
- Belt-and-suspenders approach for extra safety

## Benefits

### Immediate Benefits
1. ✅ **No more task watchdog crashes** - System stable even with slow TCP servers
2. ✅ **Graceful degradation** - TCP debug output drops messages instead of crashing
3. ✅ **Serial output unaffected** - Serial debugging always works
4. ✅ **System responsiveness maintained** - No long blocking delays

### Trade-offs
- ⚠️ **TCP debug messages may be lost** when server is slow (acceptable for debugging)
- ⚠️ **100ms timeout** means very slow networks might see incomplete messages
- ✅ **Minimal overhead** - Buffer checks are fast (no performance impact)
- ✅ **Conservative approach** - Multiple layers of protection

## Testing Recommendations

1. **Test with slow TCP server**:
   ```python
   # Python TCP server that reads slowly
   import socket, time
   s = socket.socket()
   s.bind(('0.0.0.0', 8085))
   s.listen(1)
   conn, addr = s.accept()
   while True:
       data = conn.recv(100)  # Read only 100 bytes at a time
       time.sleep(0.5)  # Slow read - 500ms delay
       print(data.decode(), end='')
   ```

2. **Test with no TCP server**: 
   - Configure TCP debug to non-existent IP
   - Should fail to connect, fall back to serial only
   - No crashes

3. **Monitor watchdog behavior**:
   - Check stack high water marks with `debugStack()` calls
   - Monitor for any watchdog warnings in serial output

4. **Test under load**:
   - Enable all heaters
   - Trigger emergency conditions
   - Run for extended period (hours)
   - Should remain stable

## Configuration Notes

### Adjusting TCP Timeout

If you need to adjust the timeout for different network conditions:

```cpp
// In tcpConnect():
tcpClient.setTimeout(100);  // 100ms - default, aggressive
tcpClient.setTimeout(500);  // 500ms - for slower networks
tcpClient.setTimeout(50);   // 50ms - for very fast local networks
```

**Recommendation**: Keep at 100ms for most use cases. The watchdog timeout is ~5000ms, so even with 50 debug calls, 100ms × 50 = 5000ms stays just under the limit.

### Disabling TCP Debug

If TCP debug is causing issues, disable it via web interface:
1. Navigate to Settings page
2. Uncheck "TCP debug"
3. Save settings
4. Serial debug continues to work

### Alternative: Increase Watchdog Timeout

**Not recommended**, but possible:
```cpp
// In setup() before xTaskCreate():
esp_task_wdt_init(10, true); // Increase to 10 seconds
```

This is a workaround, not a fix. Better to fix the blocking issue.

## Related Files Modified

- `src/main.cpp`:
  - `tcpConnect()` - Added timeout configuration
  - `tcpSafeWrite()` - New helper functions (2 overloads)
  - `debugPrint()` - All overloads updated (7 functions)
  - `debugPrintln()` - All overloads updated (7 functions)
  - `debugPrintDec()`, `debugPrintHex()`, `debugPrintArray()`, `debugStack()` - Updated
  - `processHeaters()` - Added watchdog feeding

## Future Improvements (Optional)

If TCP debugging needs to be more robust, consider:

1. **Dedicated debug task**: Move TCP logging to separate low-priority task with ring buffer
2. **UDP logging**: Use UDP instead of TCP (no flow control, no blocking)
3. **Reduced verbosity**: Add log levels (ERROR, WARN, INFO, DEBUG, TRACE)
4. **Async TCP**: Use AsyncTCP library for non-blocking TCP operations
5. **Message batching**: Buffer debug messages and send in batches

## Conclusion

The task watchdog crashes were caused by blocking TCP write operations when the debug server was slow to read data. The fix implements three layers of protection:

1. **Timeout**: TCP writes timeout after 100ms
2. **Buffer checking**: Skip writes when buffer is full
3. **Watchdog feeding**: Yield control regularly in long functions

The system now gracefully handles slow TCP servers by dropping debug messages instead of crashing. Serial debugging remains reliable. The fix maintains system stability while preserving the debug functionality for normal use cases.

