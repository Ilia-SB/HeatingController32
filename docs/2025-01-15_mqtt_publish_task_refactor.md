# MQTT Publish Task Refactor

## Overview
Refactored MQTT publishing to run in a dedicated FreeRTOS task (`taskMqtt`) that processes publish requests from a queue-based system. This improves system responsiveness and prevents blocking operations during MQTT publishing.

## Changes Made

### 1. Queue Structure and Global Variables
Added in `src/main.cpp`:
- `MqttMessage` struct with fields: topic (128 chars), payload (1024 chars), retain (bool)
- Global `QueueHandle_t MqttQueue` for message queue
- Global `TaskHandle_t hndlMqtt` for task handle
- `MQTT_PUBLISH_QUEUE_SIZE` constant set to 20 messages
- `taskMqttStackWatermark` for stack monitoring

### 2. Helper Function
Added `queueMqtt()` function:
- Takes topic, payload, and retain flag as parameters
- Creates `MqttMessage` struct and adds to queue
- Non-blocking queue send with error logging if queue is full
- Returns success/failure boolean

### 3. New Task Implementation
Created `taskMqtt()` task:
- Waits for messages from queue using `xQueueReceive()`
- Checks MQTT connection before publishing
- Handles both small and large payloads:
  - Small payloads: uses simple `mqttClient.publish()`
  - Large payloads: uses `beginPublish()`/`write()`/`endPublish()` pattern
- Updates stack watermark for monitoring
- Runs continuously with minimal delay

### 4. Replaced All Direct MQTT Publish Calls
Updated these locations to use `queueMqtt()`:
- `processCommand()` - settings updates (lines 644, 660)
- `mqttConnect()` - LWT "Online" message (line 841)
- `readTemperatures()` - error messages (line 1207)
- `reportHeaterState()` - heater state updates (line 1607)

### 5. Task and Queue Creation
Added in `setup()`:
- Queue creation: `xQueueCreate(MQTT_PUBLISH_QUEUE_SIZE, sizeof(MqttMessage))`
- Task creation: `xTaskCreate(taskMqtt, "Mqtt", 4096, NULL, 1, &hndlMqtt)`

### 6. Debug Monitoring Updates
Updated JSON responses to include MQTT publish task stack watermark:
- WebSocket streaming responses
- `/debug.json` endpoint
- Real-time debug display

## Benefits

1. **Non-blocking Publishing**: MQTT publishes no longer block other tasks
2. **Improved Responsiveness**: System remains responsive during heavy MQTT traffic
3. **Queue-based Architecture**: Decouples publish requests from actual publishing
4. **Error Handling**: Graceful handling of queue full conditions
5. **Large Payload Support**: Automatic detection and handling of large messages
6. **Monitoring**: Stack watermark tracking for the new task

## Technical Details

- **Queue Size**: 20 messages (configurable via `MQTT_PUBLISH_QUEUE_SIZE`)
- **Message Size**: Topic max 128 chars, payload max 1024 chars
- **Payload Handling**: Automatic size detection for optimal publish method
- **Thread Safety**: Queue is thread-safe, no additional mutex needed
- **Stack Size**: 4096 bytes for the MQTT publish task
- **Priority**: Same priority as other tasks (priority 1)

## Connection Management
MQTT connection management (`mqttClient.loop()`, `mqttConnect()`) remains in `taskSystem` as planned, ensuring connection handling is separate from publishing.

## Testing Recommendations

1. Monitor stack watermarks via debug interface
2. Test with high-frequency heater state updates
3. Verify queue behavior under load
4. Check large payload handling (heater state JSON)
5. Confirm error logging when queue is full
6. Validate MQTT connection handling during disconnections

## Files Modified
- `src/main.cpp` - Complete MQTT publish refactor

## Date
2025-01-15
