#include "../include/RelayManager.h"
#include "../include/Logger.h"
#include <time.h>

// External declaration for the global relay event queue (defined in main.cpp).
// This allows the static timer callback to access the queue.
// A more idiomatic C++ solution might involve member function pointers or lambdas if supported by the RTOS API for timers.
extern QueueHandle_t g_relayEventQueue; 

// Static callback function for FreeRTOS software timers.
// Called when a relay's ON-duration timer expires.
void RelayManager::relayTimerCallback(TimerHandle_t xTimer) {
    // Retrieve the relayIndex from the timer's ID (set during timer creation).
    int relayIndex = (int)pvTimerGetTimerID(xTimer);
    AppLogger.logf(LOG_LEVEL_DEBUG, "RelayTimerCb", "Timer expired for relay index: %d", relayIndex);

    RelayTimerEvent_t event;
    event.relayIndex = relayIndex;

    // Send an event to the global relay event queue (g_relayEventQueue).
    if (g_relayEventQueue != NULL) {
        if (xQueueSend(g_relayEventQueue, &event, (TickType_t)0) != pdPASS) {
            AppLogger.logf(LOG_LEVEL_ERROR, "RelayTimerCb", "Failed to send event to relay queue for relay index %d", relayIndex);
        } else {
            AppLogger.logf(LOG_LEVEL_DEBUG, "RelayTimerCb", "Sent timer expiration event for relay index %d to queue.", relayIndex);
        }
    } else {
        AppLogger.error("RelayTimerCb", "Relay event queue (g_relayEventQueue) is not initialized!");
    }
}

// Khởi tạo RelayManager
RelayManager::RelayManager() {
    _relayPins = nullptr;
    _numRelays = 0;
    _relayStatus = nullptr;
    _mutex = xSemaphoreCreateMutex();
    _statusChanged = false;
    _relayEventQueue = NULL; // Instance queue, g_relayEventQueue is used by static callback
}

// Khởi tạo RelayManager
void RelayManager::begin(const int* relayPins, int numRelays, QueueHandle_t relayEventQueue) {
    _relayPins = relayPins;
    _numRelays = numRelays;
    _relayEventQueue = relayEventQueue; // Store the queue handle, though static callback uses global g_relayEventQueue

    _relayStatus = new RelayStatus[numRelays];
    
    for (int i = 0; i < _numRelays; i++) {
        pinMode(_relayPins[i], OUTPUT);
        digitalWrite(_relayPins[i], LOW); 
        _relayStatus[i].state = false;

        char timerName[20];
        sprintf(timerName, "RelayTimer%d", i);

        // Create a software timer for each relay for timed operations.
        // pdFALSE means it's a one-shot timer (auto-reload is false).
        _relayStatus[i].timerHandle = xTimerCreate(timerName, 
                                                   pdMS_TO_TICKS(1000), // Default period, will be changed
                                                   pdFALSE,             // No auto-reload
                                                   (void*)i,            // Timer ID is the relayIndex
                                                   RelayManager::relayTimerCallback); // Static callback function
        
        if (_relayStatus[i].timerHandle == NULL) {
            AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Failed to create timer for relay %d", i + 1);
        } else {
            AppLogger.logf(LOG_LEVEL_DEBUG, "RelayMgr", "Created timer for relay %d with ID: %d", i + 1, i);
        }
    }
    
    _statusChanged = true; // Initial status should be reported
    AppLogger.logf(LOG_LEVEL_INFO, "RelayMgr", "Initialized %d relays with software timers.", _numRelays);
}

// Đặt trạng thái relay
void RelayManager::setRelay(int relayIndex, bool state, unsigned long durationMs) {
    if (relayIndex < 0 || relayIndex >= _numRelays) {
        AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Error: Invalid relay index: %d", relayIndex);
        return;
    }
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool previousState = _relayStatus[relayIndex].state;
        bool previousTimerActive = (_relayStatus[relayIndex].timerHandle != NULL) ? xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle) : false;
        bool changed = false;

        if (state) { // Turn relay ON
            digitalWrite(_relayPins[relayIndex], HIGH);
            _relayStatus[relayIndex].state = true;
            
            if (durationMs > 0) {
                AppLogger.logf(LOG_LEVEL_INFO, "RelayMgr", "Relay %d ON for %lu ms.", relayIndex + 1, durationMs);
                if (_relayStatus[relayIndex].timerHandle != NULL) {
                    // Change timer period and start it.
                    if (xTimerChangePeriod(_relayStatus[relayIndex].timerHandle, pdMS_TO_TICKS(durationMs), (TickType_t)0) != pdPASS) {
                        AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Failed to change timer period for relay %d", relayIndex + 1);
                    }
                    if (xTimerStart(_relayStatus[relayIndex].timerHandle, (TickType_t)0) != pdPASS) {
                        AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Failed to start timer for relay %d", relayIndex + 1);
                    } else {
                        // Timer started or period changed. Mark as changed if duration is new/different for an already ON relay.
                        if (previousState == true && (!previousTimerActive || xTimerGetPeriod(_relayStatus[relayIndex].timerHandle) != pdMS_TO_TICKS(durationMs))) {
                           changed = true; 
                        }
                    }
                }
            } else { // Turn ON indefinitely
                AppLogger.logf(LOG_LEVEL_INFO, "RelayMgr", "Relay %d ON indefinitely.", relayIndex + 1);
                // If a timer was active, stop it.
                if (_relayStatus[relayIndex].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle)) {
                    xTimerStop(_relayStatus[relayIndex].timerHandle, (TickType_t)0);
                    if (previousTimerActive) changed = true; // Changed from timed to indefinite ON
                }
            }
        } else { // Turn relay OFF
            digitalWrite(_relayPins[relayIndex], LOW);
            _relayStatus[relayIndex].state = false;
            AppLogger.logf(LOG_LEVEL_INFO, "RelayMgr", "Relay %d OFF.", relayIndex + 1);
            // If a timer was active, stop it.
            if (_relayStatus[relayIndex].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle)) {
                if (xTimerStop(_relayStatus[relayIndex].timerHandle, (TickType_t)0) != pdPASS) {
                    AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Failed to stop timer for relay %d", relayIndex + 1);
                }
                 // State changed to OFF, this is covered by the 'previousState != _relayStatus[relayIndex].state' check below.
            }
        }
        
        // General check if state (ON/OFF) has changed.
        if (previousState != _relayStatus[relayIndex].state) {
            changed = true;
        }

        if (changed) {
            _statusChanged = true; // Global flag indicating some relay's status changed.
        }
        
        xSemaphoreGive(_mutex);
    }
}

// Bật relay
void RelayManager::turnOn(int relayIndex, unsigned long duration) {
    setRelay(relayIndex, true, duration);
}

// Tắt relay
void RelayManager::turnOff(int relayIndex) {
    setRelay(relayIndex, false, 0);
}

// Lấy trạng thái relay
bool RelayManager::getState(int relayIndex) {
    if (relayIndex < 0 || relayIndex >= _numRelays) {
        return false;
    }
    bool state = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        state = _relayStatus[relayIndex].state;
        xSemaphoreGive(_mutex);
    }
    return state;
}

// Tạo payload JSON cho trạng thái relay
String RelayManager::getStatusJson(const char* apiKey) {
    StaticJsonDocument<512> doc;
    doc["api_key"] = apiKey;
    doc["timestamp"] = (uint32_t)time(NULL);
    JsonArray relays = doc.createNestedArray("relays");
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        for (int i = 0; i < _numRelays; i++) {
            JsonObject relay = relays.createNestedObject();
            relay["id"] = i + 1;
            relay["state"] = _relayStatus[i].state;
            
            unsigned long remainingTimeMs = 0;
            if (_relayStatus[i].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[i].timerHandle)) {
                TickType_t expiryTimeTicks = xTimerGetExpiryTime(_relayStatus[i].timerHandle);
                TickType_t currentTimeTicks = xTaskGetTickCount(); // Current system tick count
                if (expiryTimeTicks > currentTimeTicks) {
                    remainingTimeMs = (expiryTimeTicks - currentTimeTicks) * portTICK_PERIOD_MS;
                }
            }
            relay["remaining_time"] = remainingTimeMs; // Remaining ON time in milliseconds
        }
        xSemaphoreGive(_mutex);
    }
    
    String payload;
    serializeJson(doc, payload);
    return payload;
}

// Xử lý JSON lệnh điều khiển
bool RelayManager::processCommand(const char* json) {
    unsigned long cmdStartTime = millis();
    bool commandParseSuccess = false;
    String perfDetails = "";

    AppLogger.logf(LOG_LEVEL_DEBUG, "RelayMgrCmd", "Received JSON: %s", json);
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "JSON parsing failed: %s", error.c_str());
        perfDetails = "JSON parsing failed";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, false, perfDetails);
        return false;
    }

    if (!doc.containsKey("relays")) {
        AppLogger.error("RelayMgr", "Command missing 'relays' field");
        perfDetails = "Missing 'relays' field";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, false, perfDetails);
        return false;
    }

    JsonArray relaysArray = doc["relays"];
    if (relaysArray.isNull()) {
        AppLogger.warning("RelayMgr", "'relays' array is null.");
        perfDetails = "'relays' array is null";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, true, perfDetails); // JSON structure was valid
        return false; 
    }

    commandParseSuccess = true; // At this point, basic JSON structure is valid.
    bool anyChangeMadeByThisCommand = false; // Tracks if any relay's state/timer actually changed due to this command payload.
    int relayCommandsInPayload = relaysArray.size();
    int validRelayObjectsFound = 0;

    for (JsonObject relayCmd : relaysArray) {
        if (relayCmd.containsKey("id") && relayCmd.containsKey("state")) {
            validRelayObjectsFound++;
            int id = relayCmd["id"];
            bool stateCmd = relayCmd["state"];
            unsigned long durationCmd = 0; // Duration in milliseconds

            if (relayCmd.containsKey("duration") && stateCmd) { // Duration is only relevant if turning ON
                durationCmd = relayCmd["duration"]; 
            }

            if (id >= 1 && id <= _numRelays) {
                int relayIndex = id - 1;
                
                // Check if this specific command part causes a change.
                // Need to capture state *before* calling setRelay for this specific relay.
                bool oldRelayState = false;
                bool oldTimerActive = false;
                TickType_t oldTimerPeriodTicks = 0;

                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                    oldRelayState = _relayStatus[relayIndex].state;
                    if (_relayStatus[relayIndex].timerHandle) {
                        oldTimerActive = xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle);
                        if(oldTimerActive) {
                            oldTimerPeriodTicks = xTimerGetPeriod(_relayStatus[relayIndex].timerHandle);
                        }
                    }
                    xSemaphoreGive(_mutex);
                }

                setRelay(relayIndex, stateCmd, durationCmd);

                // Check if setRelay actually changed the state or timer for this specific relay.
                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                    bool newRelayState = _relayStatus[relayIndex].state;
                    bool newTimerActive = false;
                    TickType_t newTimerPeriodTicks = 0;
                    if(_relayStatus[relayIndex].timerHandle) {
                        newTimerActive = xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle);
                         if(newTimerActive) {
                            newTimerPeriodTicks = xTimerGetPeriod(_relayStatus[relayIndex].timerHandle);
                        }
                    }

                    if (oldRelayState != newRelayState) {
                        anyChangeMadeByThisCommand = true;
                    } else if (newRelayState == true) { // State is ON, check if timer changed
                        if (oldTimerActive != newTimerActive) {
                            anyChangeMadeByThisCommand = true;
                        } else if (newTimerActive && (oldTimerPeriodTicks != newTimerPeriodTicks)) {
                            anyChangeMadeByThisCommand = true;
                        }
                    }
                    // Note: _statusChanged (global flag) is set by setRelay if any change happened.
                    // anyChangeMadeByThisCommand is more granular for *this specific command in the array*.
                    xSemaphoreGive(_mutex);
                } else {
                     AppLogger.logf(LOG_LEVEL_WARNING, "RelayMgrCmd", "Could not take mutex for post-setRelay check for relay %d", id);
                }

            } else {
                AppLogger.logf(LOG_LEVEL_ERROR, "RelayMgr", "Invalid relay ID: %d in command.", id);
            }
        }
    }
    
    if (validRelayObjectsFound > 0) {
        perfDetails = String(validRelayObjectsFound) + "/" + String(relayCommandsInPayload) + " valid relay objects. ";
        if(anyChangeMadeByThisCommand) perfDetails += "State changed by this command."; else perfDetails += "No state change by this command.";
    } else if (relayCommandsInPayload > 0) {
        perfDetails = "No valid relay objects in 'relays' array of size " + String(relayCommandsInPayload);
        commandParseSuccess = false; // If array had items but none were valid, it's a partial parse success / data error.
    } else {
        perfDetails = "'relays' array is empty.";
    }

    unsigned long cmdDuration = millis() - cmdStartTime;
    // commandParseSuccess here indicates if the overall JSON structure was okay and contained expected fields.
    // anyChangeMadeByThisCommand is more about the *effect* of the command.
    AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);

    return commandParseSuccess && anyChangeMadeByThisCommand; 
}

// Kiểm tra xem trạng thái có thay đổi không và reset cờ
bool RelayManager::hasStatusChangedAndReset() {
    bool changed = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        changed = _statusChanged;
        _statusChanged = false; 
        xSemaphoreGive(_mutex);
    }
    return changed;
} 

int RelayManager::getNumRelays() const {
    return _numRelays;
}
