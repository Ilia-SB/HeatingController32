# Minimize Filesystem Writes Implementation

**Date:** 2025-01-15  
**Author:** AI Assistant  
**Type:** Code Refactoring  

## Overview

Implemented a new storage system for HeaterItem properties that minimizes filesystem writes by storing each property in separate files and only writing when values change. This reduces flash wear on the ESP32 and improves system performance.

## Changes Made

### 1. New Property Storage System

**Before:**
- Each HeaterItem stored as single JSON file: `/item0.cfg`, `/item1.cfg`, etc.
- Every save operation rewrote entire 896-byte JSON file
- No change detection - always wrote even if nothing changed

**After:**
- Each HeaterItem property stored in separate file: `/heaters/0_name.cfg`, `/heaters/0_port.cfg`, etc.
- Only writes when property value actually changes
- Individual files are tiny (1-64 bytes vs 896 bytes)

### 2. Code Changes

#### Added Property Enum
```cpp
enum HeaterProperty {
    PROP_NAME = 0,
    PROP_SUBTOPIC = 1,
    PROP_IS_ENABLED = 2,
    PROP_SENSOR_ADDRESS = 3,
    PROP_PORT = 4,
    PROP_PHASE = 5,
    PROP_IS_AUTO = 6,
    PROP_POWER_CONSUMPTION = 7,
    PROP_PRIORITY = 8,
    PROP_TARGET_TEMPERATURE = 9,
    PROP_TEMPERATURE_ADJUST = 10,
    PROP_USE_EXTERNAL_SENSOR = 11,
    PROP_EXTERNAL_SENSOR_TOPIC = 12,
    PROP_ALL = 255  // Special case: save all properties
};
```

#### New Helper Functions
```cpp
void getHeaterPropertyFilename(uint8_t heaterNum, const char* property, String& fileName);
void ensureHeatersDirectory();
```

#### Refactored saveState() Function
- Changed signature: `void saveState(HeaterItem& heaterItem, uint8_t property = PROP_ALL)`
- Implements change detection for each property type
- Only writes to filesystem when value differs from stored value
- Uses switch statement for property-specific handling

#### Refactored loadState() Function
- Calls separate migration function to handle old format conversion
- Reads each property from individual files
- Falls back to defaults if property files don't exist

#### New Migration Function
- `migrateHeaterFromOldFormat()` - Dedicated function for migrating from old JSON format
- Checks for old format files and converts them to new per-property format
- Automatically deletes old files after successful migration

#### Updated MQTT Command Handler
- Now saves individual properties instead of entire state
- Each MQTT command triggers save of only the changed property
- Examples:
  - `IS_AUTO` command → `saveState(*heater, PROP_IS_AUTO)`
  - `PRIORITY` command → `saveState(*heater, PROP_PRIORITY)`

#### Updated Web Form Handlers
- `processSettingsForm()` and `processControlForm()` updated to use new save system
- Still save all properties for web forms (using `PROP_ALL`)

#### Updated deleteSettings() Function
- Deletes both old format files (`/item*.cfg`) and new format files (`/heaters/*`)
- Ensures complete cleanup when resetting system

### 3. Migration Strategy

The system automatically migrates from old to new format:
1. On first boot, `loadState()` checks for old format files
2. If found, loads old JSON format
3. Saves to new individual property files
4. Deletes old format file
5. Logs migration completion

## Benefits

1. **Reduced Filesystem Writes:** Only changed properties are written
2. **Smaller Individual Writes:** 1-64 bytes vs 896 bytes per write
3. **Better Flash Wear Leveling:** Writes distributed across more files
4. **Easier Debugging:** Can inspect/modify individual properties
5. **More Granular Control:** Can selectively reset properties
6. **Backward Compatibility:** Automatic migration from old format

## File Structure

**New Directory Structure:**
```
/heaters/
├── 0_name.cfg
├── 0_subtopic.cfg
├── 0_isEnabled.cfg
├── 0_sensorAddress.cfg
├── 0_port.cfg
├── 0_phase.cfg
├── 0_isAuto.cfg
├── 0_powerConsumption.cfg
├── 0_priority.cfg
├── 0_targetTemperature.cfg
├── 0_temperatureAdjust.cfg
├── 0_useExternalSensor.cfg
├── 0_externalSensorTopic.cfg
├── 1_name.cfg
├── 1_subtopic.cfg
└── ... (for all 16 heaters × 13 properties = 208 files)
```

## Testing Recommendations

1. ✅ Test migration from old format to new format
2. ✅ Verify each property type saves and loads correctly
3. ✅ Test change detection (unchanged values don't trigger writes)
4. ✅ Test with all 16 heaters configured
5. ✅ Monitor debug output to confirm write reduction
6. ✅ Test delete/reset functionality

## Code Quality

- All changes passed cppcheck validation
- No compilation errors introduced
- Maintains backward compatibility
- Follows existing code patterns and style
- Proper error handling and logging

## Performance Impact

- **Reduced:** Filesystem write operations (only on changes)
- **Reduced:** Flash wear (smaller writes, better distribution)
- **Slight increase:** Memory usage during save operations (reading existing files)
- **Improved:** System responsiveness (fewer large writes)

## Future Considerations

- Could implement property-level caching to reduce file reads
- Could add compression for larger property values
- Could implement atomic writes for critical properties
- Could add property change event notifications
