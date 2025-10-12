# External Sensor Implementation

**Date:** October 11, 2025  
**Branch:** tasks-external-sensors

## Overview

Reworked the auxAdjust system into a comprehensive external sensor system that allows each heater item to use MQTT-based external temperature sensors with automatic fallback to internal sensors on timeout.

## Changes Summary

### Phase 1: Removal of auxAdjust System

The old auxAdjust system has been completely removed from the codebase:

#### Files Modified:
- `include/HeaterItem.h` - Removed auxAdjust fields and methods
- `src/HeaterItem.cpp` - Removed auxAdjust implementation
- `include/MqttInterface.h` - Removed AUX_ADJUST constant
- `src/main.cpp` - Removed all auxAdjust handling

#### Removed Components:
- `usesAuxAdjust` boolean field
- `auxAdjust` float field
- `setAuxAdjust()` / `getAuxAdjust()` methods
- `setUsesAuxAdjust()` / `getUsesAuxAdjust()` methods
- AUX_ADJUST MQTT command handling
- auxAdjust JSON persistence

### Phase 2: External Sensor System Implementation

#### New Fields in HeaterItem (include/HeaterItem.h)

```cpp
boolean useExternalSensor = false;
char externalSensorTopic[64] = "";
float externalSensorTemperature = 0;
unsigned long externalSensorLastUpdate = 0;
```

#### New Methods in HeaterItem

**Configuration Methods:**
- `setUseExternalSensor(bool)` - Enable/disable external sensor
- `getUseExternalSensor()` - Get external sensor enabled state
- `setUseExternalSensor(const char*)` - Set from MQTT command (ON/OFF)
- `setExternalSensorTopic(const char*)` - Set MQTT topic for external sensor
- `getExternalSensorTopic()` - Get external sensor topic
- `getExternalSensorTopicCStr(char*)` - Get topic as C-string

**Runtime Methods:**
- `updateExternalSensorTemp(float)` - Update external sensor temperature and timestamp
- `isExternalSensorActive()` - Check if external sensor is active and not timed out

#### Temperature Handling Logic

The `getTemperature()` and `getSensorTemperature()` methods now check if an external sensor is active:
- **External sensor active:** Returns external sensor temperature as-is (no smoothing, no temperature adjust)
- **External sensor inactive:** Returns internal sensor temperature with low-pass filter and temperature adjust (existing behavior)

**Note:** External sensors are assumed to be pre-calibrated at their source. The `temperatureAdjust` field only applies to internal Dallas sensors.

#### Timeout Mechanism

External sensors timeout after 3 minutes (180,000ms) of no updates:
- Configured in `include/Config.h` as `EXTERNAL_SENSOR_TIMEOUT`
- Automatic fallback to internal sensor when timeout occurs
- Automatic recovery when external sensor data resumes
- Handles `millis()` rollover correctly

#### MQTT Integration (src/main.cpp)

**New MQTT Constants (include/MqttInterface.h):**
- `USE_EXTERNAL_SENSOR` - MQTT command to enable/disable external sensor
- `EXTERNAL_SENSOR_TOPIC` - MQTT command to set external sensor topic

**MQTT Callback Enhancement:**
The `mqttCallback()` function now handles external sensor topics:
1. Checks if incoming topic matches any heater's external sensor topic
2. Parses temperature from payload (expects float value)
3. Calls `updateExternalSensorTemp()` for the matching heater
4. Provides debug output for tracking

**Dynamic Subscription:**
- `subscribeToExternalSensors()` - Helper function that subscribes to all configured external sensor topics
- Called when MQTT connects (in `mqttConnect()`)
- Called when external sensor settings change (via MQTT or web)

#### Persistence

**Save (itemToJson):**
- `useExternalSensor` - Boolean field
- `externalSensorTopic` - String field
- `externalSensorActive` - Boolean field (report only, shows current status)

**Load (loadState):**
- Reads `useExternalSensor` if present (backwards compatible)
- Reads `externalSensorTopic` if present (backwards compatible)

#### Web Interface Updates

**Settings page form fields (webServerPlaceholderProcessor):**
- Added "Use external sensor" checkbox field
- Added "External sensor topic" text input field
- Fields appear after "Temperature adjust" in each heater's settings section
- Current values are displayed when form loads

**processItemForm() enhancements:**
- Processes `useExternalSensor` checkbox parameter
- Processes `externalSensorTopic` text input parameter
- Triggers MQTT re-subscription after changes
- Saves configuration to persistent storage

**Control page display:**
- Shows "Ext: Active" or "Ext: Timeout" status when external sensor is enabled
- Replaces old auxAdjust display

#### Sanity Checking

Updated `sanityCheckHeater()` to allow auto mode if either:
- Internal sensor is connected, OR
- External sensor is active

This allows heaters to operate in auto mode using external sensors even when internal sensors are not connected.

## Configuration

### MQTT Topics

External sensor topics should be absolute paths, for example:
- `tele/aux_sensor/kitchen`
- `tele/aux_sensor/living_room`
- `sensors/temperature/bedroom`

### MQTT Commands

Configure external sensors via MQTT:

```bash
# Enable external sensor for heater item_000001
mosquitto_pub -t "cmnd/heating/item_000001/useExternalSensor" -m "ON"

# Set external sensor topic for heater item_000001
mosquitto_pub -t "cmnd/heating/item_000001/externalSensorTopic" -m "tele/aux_sensor/kitchen"

# Disable external sensor
mosquitto_pub -t "cmnd/heating/item_000001/useExternalSensor" -m "OFF"
```

### Sending Temperature Updates

External sensors should publish temperature values as floats:

```bash
# Publish temperature update
mosquitto_pub -t "tele/aux_sensor/kitchen" -m "21.5"
```

### Web Interface

Configure external sensors through the web interface:
1. Navigate to Settings page (http://controller-ip/settings)
2. Click on the heater name to expand its settings
3. Check "Use external sensor" checkbox
4. Enter the MQTT topic in "External sensor topic" field (e.g., `tele/aux_sensor/kitchen`)
5. Click "Save" button
6. Configuration is saved and MQTT subscriptions are updated automatically

## Behavior

### Normal Operation
1. When external sensor is enabled and receiving updates:
   - Uses external sensor temperature + temperature adjust
   - No smoothing applied to external sensor readings
   - Internal sensor continues to be read but not used

### Timeout Fallback
1. If no external sensor update received for 3 minutes:
   - Automatically falls back to internal sensor
   - Smoothing/filtering applied to internal sensor
   - Status shows "Ext: Timeout" on control page

### Recovery
1. When external sensor updates resume:
   - Automatically switches back to external sensor
   - No manual intervention required
   - Status shows "Ext: Active" on control page

### Disabled State
1. When external sensor is disabled:
   - Uses internal sensor with smoothing (normal operation)
   - External sensor topic is ignored
   - No external sensor status displayed

## Testing Recommendations

1. **Basic Configuration:**
   - Enable external sensor via MQTT/web
   - Set valid topic
   - Verify subscription in debug output

2. **Temperature Updates:**
   - Send temperature via MQTT to external sensor topic
   - Verify heater receives and uses the temperature
   - Check that reported temperature matches external sensor

3. **Timeout Behavior:**
   - Stop sending external sensor updates
   - Wait 3+ minutes
   - Verify fallback to internal sensor
   - Check status shows "Timeout"

4. **Recovery:**
   - Resume external sensor updates after timeout
   - Verify automatic recovery to external sensor
   - Check status shows "Active"

5. **Persistence:**
   - Configure external sensor
   - Reboot controller
   - Verify configuration persists
   - Verify automatic re-subscription on reconnect

6. **Multiple Heaters:**
   - Configure different external sensors for multiple heaters
   - Verify each heater receives correct temperature
   - Check for topic routing issues

## Files Modified

- `include/Config.h` - Added EXTERNAL_SENSOR_TIMEOUT constant
- `include/HeaterItem.h` - Removed auxAdjust, added external sensor fields and methods
- `src/HeaterItem.cpp` - Removed auxAdjust, implemented external sensor logic
- `include/MqttInterface.h` - Removed AUX_ADJUST, added external sensor constants
- `src/main.cpp` - Removed auxAdjust, added external sensor MQTT handling and persistence

## Backwards Compatibility

- Configuration files without external sensor fields will load correctly (fields are optional)
- Heaters without external sensors configured operate normally with internal sensors
- No breaking changes to existing functionality

## Known Limitations

- External sensor topic limited to 63 characters (C-string buffer size)
- Only one external sensor per heater
- External sensor must publish simple float values (no JSON parsing)
- Re-subscription occurs for all external sensors when any sensor configuration changes

## Future Enhancements

Potential improvements for future versions:
1. Per-heater timeout configuration
2. JSON payload support for external sensors
3. External sensor quality/availability metrics
4. Notification when external sensor times out
5. Web UI to show external sensor last update time

