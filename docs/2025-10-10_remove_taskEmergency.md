# Remove taskEmergency Task - 2025-10-10

## Summary
Removed the `taskEmergency` FreeRTOS task wrapper and replaced it with direct calls to `processHeaters()`.

## Motivation
The `taskEmergency` task was simply a wrapper around `processHeaters()` that:
1. Started suspended
2. Waited to be resumed via `vTaskResume()`
3. Acquired the mutex
4. Called `processHeaters()`
5. Released the mutex
6. Suspended itself again

This adds unnecessary complexity and task overhead when a direct function call accomplishes the same goal.

## Changes Made

### 1. Removed Task Handle Declaration (line 27)
**Before:**
```cpp
TaskHandle_t hndlSystem;
TaskHandle_t hndlMain;
TaskHandle_t hndlEmergency;
```

**After:**
```cpp
TaskHandle_t hndlSystem;
TaskHandle_t hndlMain;
```

### 2. Removed Forward Declaration (line 32)
**Before:**
```cpp
void taskSystem(void* pvParameters);
void taskEmergency(void* pvParameters);
void taskMain(void* pvParameters);
```

**After:**
```cpp
void taskSystem(void* pvParameters);
void taskMain(void* pvParameters);
```

### 3. Replaced Task Resume with Direct Call (line 242)
**Before:**
```cpp
if(emergency) {
    vTaskResume(hndlEmergency);
}
```

**After:**
```cpp
if(emergency) {
    processHeaters();
}
```

**Reasoning:** The call site is within `getConsumptionData()` which is called from `mqttCallback()`, which runs in the context of `taskSystem`. Since `taskSystem` already holds the mutex when calling `mqttClient.loop()` (line 1362-1380), we can safely call `processHeaters()` directly without additional mutex handling.

### 4. Removed Task Function Definition (lines 1386-1395)
**Before:**
```cpp
void taskEmergency(void* pvParameters) {
    vTaskSuspend(NULL);
    while(true) {
        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
            processHeaters();
            xSemaphoreGive(mutex);
            vTaskSuspend(NULL);
        }
    }
}

void taskMain(void* pvParameters) {
    // ...
}
```

**After:**
```cpp
void taskMain(void* pvParameters) {
    // ...
}
```

### 5. Removed Task Creation (line 1607)
**Before:**
```cpp
//stack size calculation based on empirical data
xTaskCreate(taskSystem, "System", 4096, NULL, 1, &hndlSystem);
xTaskCreate(taskMain, "Main", 4096, NULL, 1, &hndlMain);
xTaskCreate(taskEmergency, "Emergency", 4096, NULL, 3, &hndlEmergency);
//TODO: reboot reason and number of reboots
```

**After:**
```cpp
//stack size calculation based on empirical data
xTaskCreate(taskSystem, "System", 4096, NULL, 1, &hndlSystem);
xTaskCreate(taskMain, "Main", 4096, NULL, 1, &hndlMain);
//TODO: reboot reason and number of reboots
```

## Benefits
1. **Simplified code:** Removed ~10 lines of boilerplate task code
2. **Reduced memory usage:** No stack allocation for the emergency task (was 4096 bytes)
3. **Better performance:** Direct function call instead of task context switch
4. **Easier to understand:** No task synchronization complexity to reason about

## Thread Safety
The change maintains thread safety because:
- The original task acquired the mutex before calling `processHeaters()`
- The new direct call happens within `taskSystem` which already holds the mutex
- `processHeaters()` is designed to be called from within a mutex-protected context

## Testing Recommendations
1. Test emergency power limit scenarios to ensure heaters are properly shut down
2. Verify MQTT consumption data handling triggers emergency shutdowns correctly
3. Monitor for any race conditions or deadlocks during operation
4. Check that all three phases handle emergency conditions properly

## Related Changes
- This follows the same pattern as the removal of `taskProcessHeaters` documented in `2025-10-10_remove_taskProcessHeaters.md`
- The system now uses only 2 tasks: `taskSystem` and `taskMain`

