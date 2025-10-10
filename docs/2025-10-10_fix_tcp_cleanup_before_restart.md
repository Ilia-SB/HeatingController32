# Fix TCP Client Cleanup Before ESP32 Restart

**Date:** October 10, 2025  
**Issue:** TCP server still reports client as connected after ESP32 restart  
**Root Cause:** Insufficient time for TCP close handshake to complete before restart  

## Problem Description

When the ESP32 restarts, it calls `tcpClient.stop()` followed by a 500ms delay before `ESP.restart()`. However, this doesn't give the TCP stack enough time to properly close the connection with a complete TCP FIN/ACK/FIN/ACK handshake. As a result, the TCP server still sees the client as connected (in a half-closed state).

## Solution

The fix implements proper TCP connection cleanup by:

1. **Flushing buffered data** - Call `tcpClient.flush()` before `stop()` to ensure all pending data is sent
2. **Initiating TCP close** - Call `tcpClient.stop()` to send FIN packet
3. **Waiting for handshake** - Increase delay from 500ms to 2000ms to allow TCP close handshake to complete
4. **Adding braces** - Follow project coding standards

## Changes

### Before (lines 1367-1374)
```cpp
if (flagRestartNow) {
    if (mqttClient.connected())
        mqttClient.disconnect();
    if (tcpClient.connected())
        tcpClient.stop();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ESP.restart();
}
```

### After (lines 1367-1384)
```cpp
if (flagRestartNow) {
    // Gracefully disconnect MQTT
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    
    // Gracefully close TCP connection
    if (tcpClient.connected()) {
        tcpClient.flush();  // Flush any pending data
        tcpClient.stop();   // Initiate TCP close (FIN)
    }
    
    // Wait for TCP close handshake to complete
    // TCP requires FIN/ACK/FIN/ACK sequence
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    ESP.restart();
}
```

## Technical Details

### TCP Close Handshake
A proper TCP connection termination requires a 4-way handshake:
1. Client sends FIN (initiated by `tcpClient.stop()`)
2. Server sends ACK
3. Server sends FIN
4. Client sends ACK

This sequence can take anywhere from a few milliseconds to several seconds depending on network conditions. The 2-second delay provides sufficient time for most network scenarios.

## Testing Recommendations

1. **Test restart behavior** - Trigger a restart and verify the TCP server properly detects the disconnection
2. **Check timing** - Monitor how long the TCP close takes in your network environment
3. **Verify MQTT** - Ensure MQTT also disconnects cleanly
4. **Network conditions** - Test under different network conditions (good signal, poor signal, etc.)

## Files Modified

- `src/main.cpp` - Function `taskSystem()` lines 1367-1384

## Related Issues

- Previous fix: [2025-10-10_fix_mqtt_crash_on_temperature_change.md](2025-10-10_fix_mqtt_crash_on_temperature_change.md)
- Code standards: [2025-10-10_code_review_and_fixes.md](2025-10-10_code_review_and_fixes.md)

