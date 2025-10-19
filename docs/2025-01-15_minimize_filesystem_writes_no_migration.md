# Minimize Filesystem Writes Implementation (No Migration)

**Date:** 2025-01-15  
**Author:** AI Assistant  
**Type:** Code Refactoring  

## Overview

Implemented a new storage system for HeaterItem properties that minimizes filesystem writes by storing each property in separate files and only writing when values change. This reduces flash wear on the ESP32 and improves system performance. **No migration code was included** - the system starts fresh with the new per-property file format.

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

#### Added Property Enum System
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

#### Added Helper Functions
- `getHeaterPropertyFilename()` - Builds property file paths like `/heaters/0_name.cfg`
- `ensureHeatersDirectory()` - Creates `/heaters/` directory if needed

#### Refactored saveState() Function
- New signature: `saveState(HeaterItem& heaterItem, uint8_t property = PROP_ALL)`
- Uses switch statement for property-specific handling
- Implements change detection for each property type:
  - Reads existing file before writing
  - Compares current value with new value
  - Only writes if values differ
  - Logs when files are updated

#### Refactored loadState() Function
- Loads each property from individual files
- Falls back to defaults if property files don't exist
- No migration logic - starts fresh with new format

#### Updated MQTT Command Handler
- Individual property saves instead of full state saves
- Each MQTT command now saves only the specific property that changed
- Examples:
  - `saveState(*heater, PROP_IS_AUTO)` for auto mode changes
  - `saveState(*heater, PROP_TARGET_TEMPERATURE)` for temperature changes

#### Updated Web Form Handlers
- `processSettingsForm()` and `processControlForm()` now save all properties using `PROP_ALL`
- Maintains existing behavior for web-based configuration

#### Updated deleteSettings() Function
- Removed old format file deletion (no migration)
- Deletes all property files in `/heaters/` directory
- Maintains settings file deletion

### 3. File Structure

**New file organization:**
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
└── ... (13 files × 16 heaters = 208 total files)
```

## Benefits

### Performance Improvements
- **Reduced filesystem writes**: Only changed properties are written
- **Smaller individual writes**: Each property file is tiny (1-64 bytes vs 896 bytes)
- **Better flash wear leveling**: Writes distributed across more files
- **Faster saves**: Only write what actually changed

### Operational Benefits
- **Easier debugging**: Can inspect/modify individual properties
- **More granular control**: Can selectively reset properties
- **Better error isolation**: Property file corruption affects only one property

## Implementation Details

### Change Detection Logic
Each property type has specific comparison logic:
- **Strings**: Direct string comparison
- **Numbers**: Direct value comparison
- **Booleans**: Convert to 0/1 for storage, compare as integers
- **Floats**: Use epsilon comparison (0.01f tolerance)

### Property File Formats
- **Strings**: Stored as-is (e.g., "Living Room")
- **Numbers**: Stored as decimal strings (e.g., "5")
- **Booleans**: Stored as "0" or "1"
- **Floats**: Stored as decimal strings (e.g., "23.5")

### Error Handling
- File operations include proper error checking
- Missing property files fall back to defaults
- Directory creation is automatic and safe

## Testing Recommendations

1. **Verify each property type** saves and loads correctly
2. **Test change detection** - unchanged values don't trigger writes
3. **Test with all 16 heaters** configured
4. **Monitor debug output** to confirm write reduction
5. **Test delete/reset functionality**
6. **Verify MQTT commands** save only changed properties

## Migration Notes

**Important:** This implementation does NOT include migration code. The system will start fresh with the new per-property file format. Any existing `/item0.cfg` style files will be ignored.

If migration is needed in the future, it would require:
1. Detecting old format files
2. Loading from old JSON format
3. Saving to new per-property format
4. Deleting old files

## Files Modified

- `src/main.cpp` - Main implementation changes
- `docs/2025-01-15_minimize_filesystem_writes_no_migration.md` - This documentation

## Code Quality

- All changes compile without errors
- Code check passes with no new warnings
- Maintains existing API compatibility
- Follows project coding standards

## Performance Impact

### Expected Write Reduction
- **Before**: 896 bytes per save operation (regardless of changes)
- **After**: 1-64 bytes per changed property
- **Typical MQTT command**: 1 property change = ~10-50 bytes vs 896 bytes
- **Web form save**: All properties = ~200-400 bytes vs 896 bytes

### Flash Wear Improvement
- **Before**: All writes concentrated on 16 large files
- **After**: Writes distributed across 208 small files
- **Wear leveling**: Better distribution across flash memory blocks
