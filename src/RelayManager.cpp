#include "../include/RelayManager.h"
#include "../include/Logger.h"
#include <time.h>

RelayManager::RelayManager() {
    _relayPins = nullptr;
    _numRelays = 0;
    _relayStatus = nullptr;
    _mutex = xSemaphoreCreateMutex();
    _statusChanged = false;
}

void RelayManager::begin(const int* relayPins, int numRelays) {
    // Lưu tham chiếu đến các chân GPIO
    _relayPins = relayPins;
    _numRelays = numRelays;
    
    // Khởi tạo mảng trạng thái
    _relayStatus = new RelayStatus[numRelays];
    
    // Khởi tạo tất cả các relay ở trạng thái tắt
    for (int i = 0; i < _numRelays; i++) {
        pinMode(_relayPins[i], OUTPUT);
        digitalWrite(_relayPins[i], LOW);
        _relayStatus[i].state = false;
        _relayStatus[i].endTime = 0;
    }
    
    // Đánh dấu có thay đổi để gửi trạng thái ban đầu
    _statusChanged = true;
    
    AppLogger.info("RelayMgr", "Initialized with " + String(_numRelays) + " relays");
}

void RelayManager::setRelay(int relayIndex, bool state, unsigned long duration) {
    // Kiểm tra chỉ số relay hợp lệ
    if (relayIndex < 0 || relayIndex >= _numRelays) {
        AppLogger.error("RelayMgr", "ERROR: Invalid relay index: " + String(relayIndex));
        return;
    }
    
    // Lấy mutex trước khi truy cập dữ liệu dùng chung
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool stateChanged = (_relayStatus[relayIndex].state != state);
        bool durationChanged = (state && _relayStatus[relayIndex].endTime != millis() + duration && duration > 0);
        
        if (state) {
            // Bật relay
            digitalWrite(_relayPins[relayIndex], HIGH);
            _relayStatus[relayIndex].state = true;
            
            // Nếu có thời gian, thiết lập thời điểm kết thúc
            if (duration > 0) {
                _relayStatus[relayIndex].endTime = millis() + duration;
                AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " turned ON for " + String(duration / 1000) + " seconds");
            } else {
                _relayStatus[relayIndex].endTime = 0;
                AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " turned ON indefinitely");
            }
        } else {
            // Tắt relay
            digitalWrite(_relayPins[relayIndex], LOW);
            _relayStatus[relayIndex].state = false;
            _relayStatus[relayIndex].endTime = 0;
            AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " turned OFF");
        }
        
        // Đánh dấu có sự thay đổi nếu trạng thái hoặc thời gian thay đổi
        if (stateChanged || durationChanged) {
            _statusChanged = true;
        }
        
        xSemaphoreGive(_mutex);
    }
}

void RelayManager::turnOn(int relayIndex, unsigned long duration) {
    setRelay(relayIndex, true, duration);
}

void RelayManager::turnOff(int relayIndex) {
    setRelay(relayIndex, false, 0);
}

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

unsigned long RelayManager::getRemainingTime(int relayIndex) {
    if (relayIndex < 0 || relayIndex >= _numRelays) {
        return 0;
    }
    
    unsigned long remaining = 0;
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        if (_relayStatus[relayIndex].endTime > 0) {
            unsigned long currentTime = millis();
            
            if (_relayStatus[relayIndex].endTime > currentTime) {
                remaining = _relayStatus[relayIndex].endTime - currentTime;
            }
        }
        
        xSemaphoreGive(_mutex);
    }
    
    return remaining;
}

void RelayManager::update() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        unsigned long currentTime = millis();
        bool anyRelayChanged = false;
        
        for (int i = 0; i < _numRelays; i++) {
            // Kiểm tra nếu relay đang bật và có thời gian
            if (_relayStatus[i].state && _relayStatus[i].endTime > 0) {
                // Kiểm tra nếu đã hết thời gian
                if (currentTime >= _relayStatus[i].endTime) {
                    // Tắt relay
                    digitalWrite(_relayPins[i], LOW);
                    _relayStatus[i].state = false;
                    _relayStatus[i].endTime = 0;
                    anyRelayChanged = true;
                    
                    AppLogger.info("RelayMgr", "Auto turned OFF relay " + String(i + 1) + " (timer expired)");
                }
            }
        }
        
        // Đánh dấu có sự thay đổi nếu có relay nào đó tự động tắt
        if (anyRelayChanged) {
            _statusChanged = true;
        }
        
        xSemaphoreGive(_mutex);
    }
}

String RelayManager::getStatusJson(const char* apiKey) {
    // Tạo JSON document
    StaticJsonDocument<512> doc;
    
    // Thêm API key và timestamp
    doc["api_key"] = apiKey;
    doc["timestamp"] = (uint32_t)time(NULL);
    
    // Tạo mảng relays
    JsonArray relays = doc.createNestedArray("relays");
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        unsigned long currentTime = millis();
        
        // Thêm thông tin cho mỗi relay
        for (int i = 0; i < _numRelays; i++) {
            JsonObject relay = relays.createNestedObject();
            relay["id"] = i + 1;
            relay["state"] = _relayStatus[i].state;
            
            // Tính thời gian còn lại
            unsigned long remaining = 0;
            if (_relayStatus[i].endTime > currentTime) {
                remaining = _relayStatus[i].endTime - currentTime;
            }
            
            relay["remaining"] = remaining;
        }
        
        xSemaphoreGive(_mutex);
    }
    
    // Chuyển JSON document thành chuỗi
    String payload;
    serializeJson(doc, payload);
    
    return payload;
}

bool RelayManager::processCommand(const char* json) {
    unsigned long cmdStartTime = millis();
    bool commandParseSuccess = false; // Indicates if overall JSON and basic structure are OK
    String perfDetails = "";

    AppLogger.debug("RelayMgrCmd", "Received JSON: " + String(json));
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        AppLogger.error("RelayMgr", "JSON parsing failed: " + String(error.c_str()));
        perfDetails = "JSON parsing failed";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);
        return false;
    }

    if (!doc.containsKey("relays")) {
        AppLogger.error("RelayMgr", "Command missing 'relays' field");
        perfDetails = "Missing 'relays' field";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);
        return false;
    }

    JsonArray relays = doc["relays"];
    if (relays.isNull()) {
        AppLogger.warning("RelayMgr", "'relays' array is null.");
        perfDetails = "'relays' array is null";
        commandParseSuccess = true; // Parsed successfully, but relays array is effectively empty.
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);
        return false; // No changes made
    }

    commandParseSuccess = true; // Basic structure is fine up to here
    bool actualStateChangesOccurred = false;
    int relayCommandsInPayload = relays.size();
    int validRelayObjectsFound = 0; // Counts how many objects in the array looked like relay commands

    for (JsonObject relayCmd : relays) {
        if (relayCmd.containsKey("id") && relayCmd.containsKey("state")) {
            validRelayObjectsFound++;

            int id = relayCmd["id"];
            bool state = relayCmd["state"];
            unsigned long duration = 0;

            if (relayCmd.containsKey("duration") && state) {
                duration = relayCmd["duration"];
                if (duration < 10000 && duration != 0) {
                    duration *= 1000;
                }
            }

            if (id >= 1 && id <= _numRelays) {
                int relayIndex = id - 1;
                
                // Original logic to determine if a state change is needed and setRelay should be called
                // This attempts to mimic the condition under which setRelay would lead to a change.
                // A brief mutex lock is added for safety when reading current relay status.
                bool currentState;
                unsigned long currentEndTime;
                bool performSetRelay = false;

                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) { // Brief attempt to get mutex
                    currentState = _relayStatus[relayIndex].state;
                    currentEndTime = _relayStatus[relayIndex].endTime;
                    xSemaphoreGive(_mutex);

                    // Determine if setRelay should be called based on current state and command
                    if (currentState != state) {
                        performSetRelay = true;
                    } else if (state) { // If command is to turn ON and it's already ON
                        if (duration > 0) { // If a duration is specified
                            // Check if current operation is timed and if the new duration is different
                            // Note: millis() comparison for endTime can be tricky due to passage of time
                            if (currentEndTime == 0 || currentEndTime != (millis() + duration)) { // Simplified check
                                performSetRelay = true;
                            }
                        } else { // Turning ON indefinitely
                            if (currentEndTime != 0) { // If it was previously timed
                                performSetRelay = true;
                            }
                        }
                    }
                    // If command is to turn OFF (state=false) and it's already OFF, performSetRelay remains false.
                } else {
                    AppLogger.warning("RelayMgr", "Could not take mutex for relay " + String(id) + " state check, skipping update for this relay.");
                    performSetRelay = false; 
                }

                if (performSetRelay) {
                    setRelay(relayIndex, state, duration); // setRelay handles its own mutex for writing
                    actualStateChangesOccurred = true;
                }
            } else {
                AppLogger.error("RelayMgr", "Invalid relay ID: " + String(id) + " in command's relay object.");
            }
        }
    }

    if (validRelayObjectsFound > 0) {
        perfDetails = String(validRelayObjectsFound) + "/" + String(relayCommandsInPayload) + " relay obj(s) valid. ";
        if(actualStateChangesOccurred) perfDetails += "State changed."; else perfDetails += "No state change.";
    } else if (relayCommandsInPayload > 0) { // Array had items, but none were valid relay command structures
        perfDetails = "No valid relay objects in 'relays' array of size " + String(relayCommandsInPayload);
        commandParseSuccess = false; // Structure was bad if items existed but weren't processable
    } else { // relays.size() == 0
        perfDetails = "'relays' array was empty.";
    }

    unsigned long cmdDuration = millis() - cmdStartTime;
    AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);

    return actualStateChangesOccurred;
}

bool RelayManager::hasStatusChangedAndReset() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool changed = _statusChanged;
        _statusChanged = false; // Reset cờ
        xSemaphoreGive(_mutex);
        return changed;
    }
    // Fallback nếu không lấy được mutex
    return true;
} 
