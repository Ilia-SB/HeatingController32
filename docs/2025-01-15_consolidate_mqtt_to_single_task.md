# Consolidate All MQTT Operations to taskMqttPublish

## Overview
Consolidated all MQTT operations (connect, disconnect, loop, subscribe, publish) into the `taskMqttPublish` task, making it fully autonomous and removing MQTT operations from `taskSystem` and other locations. This improves encapsulation, thread safety, and system reliability.

## Architecture Change Rationale

### Before
- `taskMqttPublish`: Only handled publishing via queue
- `taskSystem`: Handled `mqttClient.loop()`, connection management, and disconnect
- Various locations: Called `subscribeToExternalSensors()` which directly called `mqttClient.subscribe()`
- `setup()`: Initial connection and loop during startup

### After
- `taskMqttPublish`: **Fully autonomous** - handles all MQTT operations
- `taskSystem`: Simplified - no MQTT concerns
- All MQTT operations: Queued to `taskMqttPublish` for processing
- `setup()`: No direct MQTT calls - task handles everything

## Changes Made

### 1. New Command Queue System
**File**: `src/main.cpp` (lines 33-48)

Created unified command structure:
```cpp
enum MqttCommandType {
    MQTT_CMD_PUBLISH,
    MQTT_CMD_DISCONNECT,
    MQTT_CMD_SUBSCRIBE
};

struct MqttCommand {
    MqttCommandType type;
    char topic[128];
    char payload[1024];
    bool retain;
};
```

Replaced `MqttPublishMessage` with `MqttCommand` to handle all MQTT operations.

### 2. Helper Functions
**File**: `src/main.cpp` (lines 231-299)

Updated and added helper functions:
- `queueMqttPublish()` - Updated to use new command structure
- `queueMqttDisconnect()` - New function for graceful shutdown
- `queueMqttSubscribe()` - New function for subscription requests

**Note**: No `queueMqttConnect()` or `queueMqttLoop()` needed - task handles these automatically.

### 3. Autonomous taskMqttPublish
**File**: `src/main.cpp` (lines 1998-2099)

Major refactor to make task fully autonomous:

#### Key Features:
- **Autonomous Connection Management**: Checks connection every loop, reconnects every 1 second if needed
- **Automatic Loop Processing**: Calls `mqttClient.loop()` automatically when connected
- **Command Processing**: Handles PUBLISH, DISCONNECT, and SUBSCRIBE commands from queue
- **Built-in Subscriptions**: Automatically subscribes to core topics and external sensors on connect
- **State Reporting**: Reports all heater states after successful connection

#### Connection Logic:
```cpp
// 1. Check connection and reconnect if needed
if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
        lastReconnectAttempt = now;
        if (ethConnected) {
            // Connect, subscribe to core topics, external sensors, publish LWT
        }
    }
}
```

#### Command Processing:
```cpp
// 3. Process queued commands (with 100ms timeout for responsive loop)
if (xQueueReceive(mqttPublishQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
    switch (cmd.type) {
        case MQTT_CMD_PUBLISH: // Handle publishing
        case MQTT_CMD_DISCONNECT: // Handle graceful disconnect
        case MQTT_CMD_SUBSCRIBE: // Handle subscription requests
    }
}
```

### 4. Updated subscribeToExternalSensors
**File**: `src/main.cpp` (lines 897-906)

Changed from direct `mqttClient.subscribe()` calls to `queueMqttSubscribe()` calls.

### 5. Simplified taskSystem
**File**: `src/main.cpp` (lines 1933-1962)

Removed ALL MQTT operations:
- **Removed**: Direct `mqttClient.loop()` and `mqttConnect()` calls
- **Updated**: Restart logic to use `queueMqttDisconnect()` for graceful shutdown
- **Kept**: ElegantOTA, WebSocket updates, process heaters flag handling

The task is now much simpler with no MQTT concerns.

### 6. Updated Setup Function
**File**: `src/main.cpp` (lines 2548)

Removed ALL direct MQTT calls:
- **Removed**: `mqttConnect()` call
- **Removed**: 5-second wait loop for MQTT data
- **Simplified**: Just start tasks and let `taskMqttPublish` handle everything

### 7. Task Priority and Stack Allocation
**File**: `src/main.cpp` (line 2577)

Updated task creation:
```cpp
xTaskCreate(taskMqttPublish, "MqttPublish", 8192, NULL, 2, &hndlMqttPublish);
```

- **Priority**: 2 (higher than taskSystem and taskMain which are priority 1)
- **Stack**: 8192 bytes (doubled from 4096 to accommodate connection logic and JSON parsing)

### 8. Removed mqttConnect Function
**File**: `src/main.cpp` (lines 908-940)

Completely removed the standalone `mqttConnect()` function since its logic is now embedded in `taskMqttPublish`.

## Key Benefits

1. **Full Autonomy**: `taskMqttPublish` manages its own lifecycle without external coordination
2. **Simpler Code**: Other tasks don't need to worry about MQTT at all
3. **Better Thread Safety**: All MQTT operations happen in one task
4. **Auto-Reconnection**: Built-in retry logic with 1-second interval
5. **Cleaner Separation**: Clear boundaries between responsibilities
6. **Higher Priority**: Ensures timely processing of MQTT messages and commands
7. **Adequate Stack**: 8KB stack prevents overflow during complex operations

## Thread Safety Improvements

- **Single MQTT Thread**: All MQTT operations isolated to one task
- **Queue-Based Communication**: Other tasks communicate via thread-safe queues
- **No Direct Access**: No task directly calls `mqttClient` methods except `taskMqttPublish`
- **Automatic Reconnection**: No race conditions in connection management

## Testing Recommendations

- ✅ Verify MQTT connection establishment on boot
- ✅ Test external sensor subscriptions  
- ✅ Verify heater state reporting
- ✅ **Test reconnection after network loss** (especially important now)
- ✅ Test graceful disconnect on reboot
- ✅ **Monitor task stack usage** (especially with doubled allocation)
- ✅ Check for missed messages during queue overflow
- ✅ Verify task priority works as expected (MQTT processed before other tasks)

## Before/After Call Locations

### Before (Scattered MQTT Operations):
- `taskSystem`: `mqttClient.loop()`, `mqttConnect()`, `mqttClient.disconnect()`
- `setup()`: `mqttConnect()`, `mqttClient.loop()` in wait loop
- `WiFiEvent()`: `mqttConnect()`
- `subscribeToExternalSensors()`: `mqttClient.subscribe()`
- Various locations: Direct calls to `subscribeToExternalSensors()`

### After (Centralized MQTT Operations):
- `taskMqttPublish`: ALL MQTT operations (autonomous)
- All other locations: Queue commands via helper functions
- `taskSystem`: Only `queueMqttDisconnect()` before restart
- `subscribeToExternalSensors()`: Only `queueMqttSubscribe()` calls

## Automatic Loop and Reconnection Processing

- **Loop Processing**: Automatic `mqttClient.loop()` every iteration when connected
- **Reconnection**: Automatic retry every 1 second when disconnected
- **No Manual Coordination**: Task handles all timing internally
- **Responsive Design**: 100ms queue timeout ensures loop remains responsive

This consolidation significantly improves the system's reliability and maintainability by centralizing all MQTT operations in a single, autonomous task.
