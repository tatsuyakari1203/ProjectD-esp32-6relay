#include "../include/RelayManager.h"
#include "../include/Logger.h"
#include <time.h>

// Khai báo extern cho queue toàn cục (sẽ được định nghĩa trong main.cpp)
// Đây là một cách đơn giản hóa để hàm static callback có thể truy cập queue.
// Một giải pháp tốt hơn trong C++ có thể là sử dụng con trỏ thành viên hoặc lambda nếu RTOS hỗ trợ.
extern QueueHandle_t g_relayEventQueue; 

// Hàm callback cho software timer (static)
void RelayManager::relayTimerCallback(TimerHandle_t xTimer) {
    // Lấy relayIndex từ ID của timer. ID này được set khi tạo timer.
    int relayIndex = (int)pvTimerGetTimerID(xTimer);
    AppLogger.debug("RelayTimerCb", "Timer expired cho relay index: " + String(relayIndex));

    RelayTimerEvent_t event;
    event.relayIndex = relayIndex;

    // Gửi sự kiện vào queue toàn cục g_relayEventQueue
    if (g_relayEventQueue != NULL) {
        if (xQueueSend(g_relayEventQueue, &event, (TickType_t)0) != pdPASS) {
            AppLogger.error("RelayTimerCb", "Không thể gửi sự kiện vào relay queue cho relay " + String(relayIndex));
        } else {
            AppLogger.debug("RelayTimerCb", "Đã gửi sự kiện hết hạn timer cho relay " + String(relayIndex) + " vào queue.");
        }
    } else {
        AppLogger.error("RelayTimerCb", "Relay event queue (g_relayEventQueue) chưa được khởi tạo!");
    }
}

// Khởi tạo RelayManager
RelayManager::RelayManager() {
    _relayPins = nullptr;
    _numRelays = 0;
    _relayStatus = nullptr;
    _mutex = xSemaphoreCreateMutex();
    _statusChanged = false;
    _relayEventQueue = NULL; 
}

// Khởi tạo RelayManager
void RelayManager::begin(const int* relayPins, int numRelays, QueueHandle_t relayEventQueue) {
    _relayPins = relayPins;
    _numRelays = numRelays;
    // Lưu ý: _relayEventQueue của instance này có thể không cần thiết nếu callback dùng g_relayEventQueue
    // Tuy nhiên, để nhất quán, chúng ta vẫn gán nó.
    _relayEventQueue = relayEventQueue; 

    _relayStatus = new RelayStatus[numRelays];
    
    for (int i = 0; i < _numRelays; i++) {
        pinMode(_relayPins[i], OUTPUT);
        digitalWrite(_relayPins[i], LOW); 
        _relayStatus[i].state = false;

        char timerName[20];
        sprintf(timerName, "RelayTimer%d", i);

        _relayStatus[i].timerHandle = xTimerCreate(timerName, 
                                                   pdMS_TO_TICKS(1000), 
                                                   pdFALSE,             
                                                   (void*)i,            // Timer ID là relayIndex
                                                   RelayManager::relayTimerCallback); 
        
        if (_relayStatus[i].timerHandle == NULL) {
            AppLogger.error("RelayMgr", "Không thể tạo timer cho relay " + String(i + 1));
        } else {
            AppLogger.debug("RelayMgr", "Đã tạo timer cho relay " + String(i+1) + " với ID: " + String(i));
        }
    }
    
    _statusChanged = true; 
    AppLogger.info("RelayMgr", "Đã khởi tạo " + String(_numRelays) + " relays với software timers.");
}

// Đặt trạng thái relay
void RelayManager::setRelay(int relayIndex, bool state, unsigned long duration) {
    if (relayIndex < 0 || relayIndex >= _numRelays) {
        AppLogger.error("RelayMgr", "Lỗi: Chỉ số relay không hợp lệ: " + String(relayIndex));
        return;
    }
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool previousState = _relayStatus[relayIndex].state;
        bool previousTimerActive = (_relayStatus[relayIndex].timerHandle != NULL) ? xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle) : false;
        bool changed = false;

        if (state) { // Bật relay
            digitalWrite(_relayPins[relayIndex], HIGH);
            _relayStatus[relayIndex].state = true;
            
            if (duration > 0) {
                AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " BẬT trong " + String(duration / 1000) + " giây.");
                if (_relayStatus[relayIndex].timerHandle != NULL) {
                    if (xTimerChangePeriod(_relayStatus[relayIndex].timerHandle, pdMS_TO_TICKS(duration), (TickType_t)0) != pdPASS) {
                        AppLogger.error("RelayMgr", "Không thể thay đổi chu kỳ timer cho relay " + String(relayIndex + 1));
                    }
                    if (xTimerStart(_relayStatus[relayIndex].timerHandle, (TickType_t)0) != pdPASS) {
                        AppLogger.error("RelayMgr", "Không thể khởi động timer cho relay " + String(relayIndex + 1));
                    } else {
                        // Timer started or period changed for an already ON relay
                        if (previousState == true && (!previousTimerActive || xTimerGetPeriod(_relayStatus[relayIndex].timerHandle) != pdMS_TO_TICKS(duration))) {
                           changed = true; // Explicitly mark change if duration is new/different for an ON relay
                        }
                    }
                }
            } else {
                AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " BẬT vô thời hạn.");
                if (_relayStatus[relayIndex].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle)) {
                    xTimerStop(_relayStatus[relayIndex].timerHandle, (TickType_t)0);
                    if (previousTimerActive) changed = true; // Was timed, now indefinite
                }
            }
        } else { // Tắt relay
            digitalWrite(_relayPins[relayIndex], LOW);
            _relayStatus[relayIndex].state = false;
            AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " TẮT.");
            if (_relayStatus[relayIndex].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle)) {
                if (xTimerStop(_relayStatus[relayIndex].timerHandle, (TickType_t)0) != pdPASS) {
                    AppLogger.error("RelayMgr", "Không thể dừng timer cho relay " + String(relayIndex + 1));
                }
                 // if (previousTimerActive) changed = true; // State changed to OFF, this is covered by previousState check
            }
        }
        
        if (previousState != _relayStatus[relayIndex].state) {
            changed = true;
        }

        if (changed) {
            _statusChanged = true;
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
                TickType_t currentTimeTicks = xTaskGetTickCount(); // Lấy thời gian tick hiện tại
                if (expiryTimeTicks > currentTimeTicks) {
                    remainingTimeMs = (expiryTimeTicks - currentTimeTicks) * portTICK_PERIOD_MS;
                }
            }
            relay["remaining_time"] = remainingTimeMs; 
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

    AppLogger.debug("RelayMgrCmd", "Đã nhận JSON: " + String(json));
    DynamicJsonDocument doc(512); 
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        AppLogger.error("RelayMgr", "Phân tích JSON thất bại: " + String(error.c_str()));
        perfDetails = "Phân tích JSON thất bại";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, false, perfDetails);
        return false;
    }

    if (!doc.containsKey("relays")) {
        AppLogger.error("RelayMgr", "Lệnh thiếu trường 'relays'");
        perfDetails = "Thiếu trường 'relays'";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, false, perfDetails);
        return false;
    }

    JsonArray relaysArray = doc["relays"];
    if (relaysArray.isNull()) {
        AppLogger.warning("RelayMgr", "Mảng 'relays' là null.");
        perfDetails = "Mảng 'relays' là null";
        unsigned long cmdDuration = millis() - cmdStartTime;
        AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, true, perfDetails); // Vẫn parse thành công JSON
        return false; 
    }

    commandParseSuccess = true;
    bool anyChangeMadeByThisCommand = false; 
    int relayCommandsInPayload = relaysArray.size();
    int validRelayObjectsFound = 0;

    for (JsonObject relayCmd : relaysArray) {
        if (relayCmd.containsKey("id") && relayCmd.containsKey("state")) {
            validRelayObjectsFound++;
            int id = relayCmd["id"];
            bool stateCmd = relayCmd["state"];
            unsigned long durationCmd = 0; 

            if (relayCmd.containsKey("duration") && stateCmd) { 
                durationCmd = relayCmd["duration"]; 
                // Assuming duration from JSON is in milliseconds as per improve.md
            }

            if (id >= 1 && id <= _numRelays) {
                int relayIndex = id - 1;
                
                bool statusChangedBeforeSetRelay = false;
                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                    statusChangedBeforeSetRelay = _statusChanged; // Read global _statusChanged
                     // Reset it locally for this specific command part if needed for precise tracking, or rely on its global nature.
                     // For simplicity, we check if it *becomes* true *after* setRelay, 
                     // or if it was already true and remains true (meaning this command might not have changed it, but a previous one in the loop did)
                     // A more precise way is to check the *specific* relay's state before/after.
                    xSemaphoreGive(_mutex);
                }

                setRelay(relayIndex, stateCmd, durationCmd);

                // Check if setRelay marked a status change
                bool statusChangedAfterSetRelay = false;
                 if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                    statusChangedAfterSetRelay = _statusChanged;
                    // If _statusChanged became true due to this setRelay call
                    if (statusChangedAfterSetRelay && !statusChangedBeforeSetRelay) {
                        anyChangeMadeByThisCommand = true;
                    } else if (statusChangedAfterSetRelay && statusChangedBeforeSetRelay) {
                        // If _statusChanged was already true, it means a previous command in this JSON array already caused a change.
                        // We still want to reflect that *this* command might have also caused a change IF its specific parameters
                        // would have triggered _statusChanged in setRelay. This is harder to check without direct return from setRelay.
                        // For now, if setRelay was called, and _statusChanged is true, we assume this operation was part of the changes.
                        // This might not be perfectly granular for `anyChangeMadeByThisCommand` if multiple sub-commands are in one JSON.
                        // The `improve.md` suggests `anyChangeMadeByThisCommand` should be more precise.
                        // The simplest is to check if the current relay state/timer reflects the command.
                        bool currentState = _relayStatus[relayIndex].state;
                        bool currentTimerActive = xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle);
                        if (currentState == stateCmd) {
                            if (stateCmd == true && durationCmd > 0) { // Commanded ON with duration
                                if (currentTimerActive && xTimerGetPeriod(_relayStatus[relayIndex].timerHandle) == pdMS_TO_TICKS(durationCmd)) {
                                    anyChangeMadeByThisCommand = true; // State and duration match command
                                }
                            } else if (stateCmd == true && durationCmd == 0) { // Commanded ON indefinitely
                                if (!currentTimerActive) {
                                    anyChangeMadeByThisCommand = true; // State matches, timer is off as commanded
                                }
                            } else { // Commanded OFF
                                 anyChangeMadeByThisCommand = true; // State matches OFF
                            }
                        }
                    }
                    // If setRelay has made a change, _statusChanged will be true. We should make it true for this command
                    if (statusChangedAfterSetRelay) anyChangeMadeByThisCommand = true;
                    xSemaphoreGive(_mutex);
                } else {
                     AppLogger.warning("RelayMgrCmd", "Could not take mutex for post-check relay " + String(id));
                }

            } else {
                AppLogger.error("RelayMgr", "ID relay không hợp lệ: " + String(id) + " trong lệnh.");
            }
        }
    }
    
    if (validRelayObjectsFound > 0) {
        perfDetails = String(validRelayObjectsFound) + "/" + String(relayCommandsInPayload) + " đối tượng relay hợp lệ. ";
        if(anyChangeMadeByThisCommand) perfDetails += "Trạng thái đã thay đổi bởi lệnh này."; else perfDetails += "Không có thay đổi trạng thái bởi lệnh này.";
    } else if (relayCommandsInPayload > 0) {
        perfDetails = "Không có đối tượng relay hợp lệ trong mảng 'relays' kích thước " + String(relayCommandsInPayload);
        commandParseSuccess = false;
    } else {
        perfDetails = "Mảng 'relays' trống.";
    }

    unsigned long cmdDuration = millis() - cmdStartTime;
    AppLogger.perf("RelayMgr", "RelayControlProcessing", cmdDuration, commandParseSuccess, perfDetails);

    return anyChangeMadeByThisCommand; 
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
