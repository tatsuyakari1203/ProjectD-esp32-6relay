    # Hướng Dẫn Triển Khai Relay Hướng Sự Kiện (Event-Driven) cho ESP32

    Tài liệu này mô tả các bước chi tiết để chuyển đổi `RelayManager` và `Core1TaskCode` trong dự án ESP32 của bạn sang kiến trúc hướng sự kiện, sử dụng FreeRTOS Software Timers và Queues. Mục tiêu là tối ưu hóa hệ thống, giảm độ trễ và sử dụng tài nguyên hiệu quả hơn.

    ## Mục Lục

    1.  [Tổng Quan về Thay Đổi](#tổng-quan-về-thay-đổi)
    2.  [Cập Nhật `RelayManager.h`](#cập-nhật-relaymanagerh)
    3.  [Cập Nhật `RelayManager.cpp`](#cập-nhật-relaymanagercpp)
    4.  [Cập Nhật `main.cpp`](#cập-nhật-maincpp)
    5.  [Giải Thích Luồng Hoạt Động Mới](#giải-thích-luồng-hoạt-động-mới)
    6.  [Các Bước Tiếp Theo và Lưu Ý](#các-bước-tiếp-theo-và-lưu-ý)

    ## 1. Tổng Quan về Thay Đổi

    Thay vì `Core1TaskCode` liên tục kiểm tra (polling) trạng thái của các relay timer, chúng ta sẽ sử dụng các cơ chế sau:

    * **FreeRTOS Software Timers:** Mỗi khi một relay được bật với một khoảng thời gian (`duration`), một software timer sẽ được kích hoạt. Khi timer này hết hạn, nó sẽ gọi một hàm callback.
    * **FreeRTOS Queues:** Hàm callback của software timer sẽ gửi một "sự kiện" (chứa thông tin về relay đã hết hạn timer) vào một hàng đợi (queue).
    * **`Core1TaskCode` (Đã sửa đổi):** Task này sẽ "chờ" (block) trên hàng đợi đó. Khi có sự kiện mới, task sẽ được đánh thức, nhận sự kiện và thực hiện hành động tắt relay tương ứng.

    Điều này giúp `Core1TaskCode` không cần phải chạy liên tục, tiết kiệm CPU và phản ứng nhanh hơn khi timer thực sự hết hạn.

    ## 2. Cập Nhật `RelayManager.h`

    Tệp header cho `RelayManager` cần được sửa đổi để bao gồm các thành phần của FreeRTOS và thay đổi cách lưu trữ trạng thái relay.

    ```cpp
    #ifndef RELAY_MANAGER_H
    #define RELAY_MANAGER_H

    #include <Arduino.h>
    #include <ArduinoJson.h>
    #include "freertos/FreeRTOS.h" // Thêm thư viện FreeRTOS
    #include "freertos/timers.h"   // Thêm thư viện Software Timers
    #include "freertos/queue.h"    // Thêm thư viện Queues

    // Định nghĩa kiểu dữ liệu cho sự kiện relay timer
    typedef struct {
        int relayIndex;
    } RelayTimerEvent_t;


    // Struct để lưu trạng thái relay
    struct RelayStatus {
        bool state;                  // Trạng thái hiện tại (true = bật, false = tắt)
        // unsigned long endTime;    // Sẽ được quản lý bởi software timer, không cần endTime nữa
        TimerHandle_t timerHandle;   // Handle cho software timer của relay này
    };

    class RelayManager {
    public:
        RelayManager();
        
        // Khởi tạo relays và queue
        // Cần truyền vào queue handle từ bên ngoài hoặc tạo trong begin() và có getter
        void begin(const int* relayPins, int numRelays, QueueHandle_t relayEventQueue);
        
        // Điều khiển relay
        void setRelay(int relayIndex, bool state, unsigned long duration = 0);
        
        // Bật relay
        void turnOn(int relayIndex, unsigned long duration = 0);
        
        // Tắt relay
        void turnOff(int relayIndex);
        
        // Lấy trạng thái relay
        bool getState(int relayIndex);
        
        // Lấy thời gian còn lại (sẽ phức tạp hơn với software timer, có thể cần API của FreeRTOS timer)
        // unsigned long getRemainingTime(int relayIndex); // Cân nhắc loại bỏ hoặc triển khai lại
        
        // Xử lý các relay có timer - Sẽ được thay thế bằng logic trong Core1TaskCode dựa trên queue
        // void update(); // Hàm này có thể không cần thiết nữa cho việc quản lý timer
        
        // Tạo payload JSON cho trạng thái relay
        String getStatusJson(const char* apiKey);
        
        // Xử lý JSON lệnh điều khiển
        bool processCommand(const char* json);
        
        // Kiểm tra xem trạng thái có thay đổi không và reset cờ
        bool hasStatusChangedAndReset();

        // Hàm callback cho software timer (phải là static hoặc global nếu không dùng mẹo)
        static void relayTimerCallback(TimerHandle_t xTimer);
        
    private:
        const int* _relayPins;        // Con trỏ đến mảng chân GPIO relay
        int _numRelays;               // Số lượng relay
        RelayStatus* _relayStatus;    // Mảng trạng thái relay (giờ chứa cả timer handle)
        SemaphoreHandle_t _mutex;     // Mutex để bảo vệ truy cập _relayStatus và _statusChanged
        bool _statusChanged;          // Cờ đánh dấu thay đổi trạng thái

        QueueHandle_t _relayEventQueue; // Hàng đợi để nhận sự kiện từ timer callbacks
        // Để relayTimerCallback truy cập _relayEventQueue và _instance của RelayManager
        // chúng ta cần một cách. Một cách đơn giản là làm cho _relayEventQueue là static
        // hoặc truyền con trỏ RelayManager vào Timer ID, nhưng an toàn hơn là queue được truyền từ bên ngoài.
        // Hoặc có thể dùng một con trỏ static tới instance của RelayManager (Singleton pattern - không khuyến khích nếu có thể tránh)
        // Trong ví dụ này, _relayEventQueue sẽ được truyền vào từ bên ngoài.
    };

    #endif // RELAY_MANAGER_H

**Các thay đổi chính trong `RelayManager.h`:**

- Bao gồm các header của FreeRTOS: `FreeRTOS.h`, `timers.h`, `queue.h`.

- Định nghĩa `RelayTimerEvent_t` để chứa thông tin sự kiện (chỉ số relay).

- Trong `RelayStatus`, loại bỏ `endTime` và thêm `TimerHandle_t timerHandle`.

- Hàm `begin` nhận thêm tham số `QueueHandle_t relayEventQueue`.

- Hàm `update()` có thể không cần thiết nữa cho việc quản lý timer.

- Khai báo `static void relayTimerCallback(TimerHandle_t xTimer)` cho software timer.

- Thêm `QueueHandle_t _relayEventQueue` làm thành viên private.


## 3. Cập Nhật `RelayManager.cpp`

Tệp triển khai cho `RelayManager` sẽ chứa logic chính của việc tạo, quản lý software timers và gửi sự kiện vào queue.

    #include "../include/RelayManager.h"
    #include "../include/Logger.h" // Đảm bảo Logger được include
    #include <time.h>

    // Khai báo extern cho queue toàn cục (sẽ được định nghĩa trong main.cpp)
    // Đây là một cách đơn giản hóa để hàm static callback có thể truy cập queue.
    // Một giải pháp tốt hơn trong C++ có thể là sử dụng con trỏ thành viên hoặc lambda nếu RTOS hỗ trợ.
    extern QueueHandle_t g_relayEventQueue; 

    // Khởi tạo RelayManager
    RelayManager::RelayManager() {
        _relayPins = nullptr;
        _numRelays = 0;
        _relayStatus = nullptr;
        _mutex = xSemaphoreCreateMutex(); 
        _statusChanged = false;
        _relayEventQueue = NULL; 
    }

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
                        }
                    }
                } else {
                    AppLogger.info("RelayMgr", "Relay " + String(relayIndex + 1) + " BẬT vô thời hạn.");
                    if (_relayStatus[relayIndex].timerHandle != NULL && xTimerIsTimerActive(_relayStatus[relayIndex].timerHandle)) {
                        xTimerStop(_relayStatus[relayIndex].timerHandle, (TickType_t)0);
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
                }
            }
            
            if (previousState != _relayStatus[relayIndex].state) {
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
        StaticJsonDocument<512> doc; // Kích thước có thể cần điều chỉnh cho 6 relay
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
            // Ghi log hiệu suất ở đây nếu cần
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
        bool anyChangeMadeByThisCommand = false; // Theo dõi thay đổi trong lệnh này
        int relayCommandsInPayload = relaysArray.size();
        int validRelayObjectsFound = 0;

        for (JsonObject relayCmd : relaysArray) {
            if (relayCmd.containsKey("id") && relayCmd.containsKey("state")) {
                validRelayObjectsFound++;
                int id = relayCmd["id"];
                bool state = relayCmd["state"];
                unsigned long duration = 0; 

                if (relayCmd.containsKey("duration") && state) { 
                    duration = relayCmd["duration"]; 
                    // Nếu JSON gửi đơn vị là giây, chuyển đổi sang mili giây
                    // Ví dụ: nếu duration từ JSON là số giây:
                    // duration = relayCmd["duration"].as<unsigned long>() * 1000;
                    // Nếu JSON đã gửi mili giây thì không cần nhân.
                    // Giả sử JSON gửi mili giây cho duration.
                }

                if (id >= 1 && id <= _numRelays) {
                    int relayIndex = id - 1;
                    
                    // Ghi lại trạng thái _statusChanged trước khi gọi setRelay
                    bool statusChangedBeforeSetRelay = false;
                    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                        statusChangedBeforeSetRelay = _statusChanged;
                        xSemaphoreGive(_mutex);
                    }

                    setRelay(relayIndex, state, duration);

                    // Kiểm tra xem setRelay có thực sự thay đổi trạng thái không
                    bool statusChangedAfterSetRelay = false;
                     if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10))) {
                        statusChangedAfterSetRelay = _statusChanged;
                        xSemaphoreGive(_mutex);
                    }
                    // Nếu _statusChanged được set thành true bởi setRelay (và nó không phải là true từ trước đó do lệnh khác trong cùng payload)
                    // thì lệnh này đã gây ra thay đổi.
                    if (statusChangedAfterSetRelay && (statusChangedAfterSetRelay != statusChangedBeforeSetRelay || state != getState(relayIndex) /*cần cẩn thận race condition*/ )) {
                         anyChangeMadeByThisCommand = true;
                    }


                } else {
                    AppLogger.error("RelayMgr", "ID relay không hợp lệ: " + String(id) + " trong lệnh.");
                }
            }
        }
        
        if (validRelayObjectsFound > 0) {
            perfDetails = String(validRelayObjectsFound) + "/" + String(relayCommandsInPayload) + " đối tượng relay hợp lệ. ";
            // anyChangeMadeByThisCommand phản ánh chính xác hơn là _statusChanged (vì _statusChanged là toàn cục cho tất cả thay đổi)
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

**Các thay đổi chính trong `RelayManager.cpp`:**

- **`relayTimerCallback`**:

  - Hàm static này được gọi khi software timer hết hạn.

  - Lấy `relayIndex` từ ID của timer.

  - Tạo `RelayTimerEvent_t` và gửi vào `g_relayEventQueue`.

- **`begin`**:

  - Khởi tạo `_relayStatus` và tạo một software timer (`xTimerCreate`) cho mỗi relay. Timer ID được đặt là `relayIndex`.

- **`setRelay`**:

  - Khi bật relay có `duration`: Thay đổi chu kỳ (`xTimerChangePeriod`) và khởi động (`xTimerStart`) software timer.

  - Khi bật relay vô thời hạn hoặc tắt relay: Dừng (`xTimerStop`) software timer nếu nó đang hoạt động.

- **`getStatusJson`**:

  - Tính `remaining_time` dựa trên `xTimerIsTimerActive()` và `xTimerGetExpiryTime()` để lấy thời gian còn lại của timer.

- **`processCommand`**: Logic phân tích JSON được giữ nguyên, nhưng việc bật/tắt relay giờ đây sẽ kích hoạt/dừng software timer tương ứng. Cần tinh chỉnh cách xác định `anyChangeMadeByThisCommand` để phản ánh chính xác hơn.


## 4. Cập Nhật `main.cpp`

Tệp `main.cpp` cần khởi tạo queue và sửa đổi `Core1TaskCode` để xử lý sự kiện từ queue.

    #include <Arduino.h>
    #include "../include/WS_GPIO.h"
    #include "../include/SensorManager.h"
    #include "../include/NetworkManager.h"
    #include "../include/RelayManager.h" // Đã được cập nhật
    #include "../include/TaskScheduler.h"
    #include "../include/EnvironmentManager.h"
    #include "../include/Logger.h"
    #include <time.h>
    #include <Preferences.h>
    #include "freertos/FreeRTOS.h" 
    #include "freertos/task.h"
    #include "freertos/queue.h"    

    // ... (Các hằng số và khai báo khác giữ nguyên) ...

    // Sensor and manager objects
    SensorManager sensorManager;
    NetworkManager networkManager;
    RelayManager relayManager; 
    EnvironmentManager envManager(sensorManager);
    TaskScheduler taskScheduler(relayManager, envManager);

    SemaphoreHandle_t sensorDataMutex;

    // Khai báo QueueHandle_t toàn cục cho sự kiện Relay Timer
    QueueHandle_t g_relayEventQueue; 

    // ... (mqttCallback và các hàm khác giữ nguyên) ...

    // Core 0 Task - Handles sensors, network, MQTT (preemptive)
    void Core0TaskCode(void * parameter) {
      AppLogger.info("Core0", "Task bắt đầu trên core " + String(xPortGetCoreID()));
      
      for(;;) {
        networkManager.loop();
        unsigned long currentTime = millis();
        
        // Đọc cảm biến và gửi dữ liệu (logic giữ nguyên)
        if (currentTime - lastSensorReadTime >= sensorReadInterval) {
          lastSensorReadTime = currentTime;
          if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
            unsigned long sensorReadStartTime = millis();
            bool readSuccess = false;
            if (sensorManager.readSensors()) {
              readSuccess = true;
              envManager.setCurrentTemperature(sensorManager.getTemperature());
              envManager.setCurrentHumidity(sensorManager.getHumidity());
              envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());
              AppLogger.debug("Core0", "Sensors read: T=" + String(sensorManager.getTemperature()) +
                              "°C, H=" + String(sensorManager.getHumidity()) +
                              "%, HI=" + String(sensorManager.getHeatIndex()) + "°C");
              if (networkManager.isConnected()) {
                String payload = sensorManager.getJsonPayload(API_KEY);
                unsigned long mqttPublishStartTime = millis();
                bool mqttSuccess = networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
                unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
                AppLogger.perf("Core0", "MQTTSensorDataPublish", mqttPublishDuration, mqttSuccess);
                AppLogger.debug("Core0", "Sensor data published to MQTT");
              } else {
                AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT");
              }
            } else {
              AppLogger.error("Core0", "Failed to read from sensors");
              readSuccess = false;
            }
            unsigned long sensorReadDuration = millis() - sensorReadStartTime;
            AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);
            xSemaphoreGive(sensorDataMutex);
          }
        }
        
        // Publish relay và scheduler status khi có thay đổi hoặc cần gửi dự phòng
        if (networkManager.isConnected()) {
          bool forcedReport = (currentTime - lastForcedStatusReportTime >= forcedStatusReportInterval);
          
          if (relayManager.hasStatusChangedAndReset() || forcedReport) { 
            String statusPayload = relayManager.getStatusJson(API_KEY);
            unsigned long mqttPublishStartTime = millis();
            bool mqttSuccess = networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
            unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
            AppLogger.perf("Core0", "MQTTRelayStatusPublish", mqttPublishDuration, mqttSuccess);
            if (forcedReport) {
              AppLogger.debug("Core0", "Trạng thái relay đã được publish lên MQTT (báo cáo bắt buộc)");
            } else {
              AppLogger.debug("Core0", "Trạng thái relay đã được publish lên MQTT");
            }
          }
          
          if (taskScheduler.hasScheduleStatusChangedAndReset() || forcedReport) {
            String schedulePayload = taskScheduler.getTasksJson(API_KEY);
            networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
             if (forcedReport) {
              AppLogger.debug("Core0", "Schedule status published to MQTT (forced report)");
            } else {
              AppLogger.debug("Core0", "Schedule status published to MQTT");
            }
          }
          
          if (forcedReport) {
            lastForcedStatusReportTime = currentTime;
          }
        }
        
        // Check and update irrigation schedules (logic giữ nguyên)
        time_t current_time_for_scheduler;
        time(&current_time_for_scheduler);
        time_t next_check = taskScheduler.getEarliestNextCheckTime();
        if (next_check == 0 || current_time_for_scheduler >= next_check) {
          taskScheduler.update();
        }
        
        // Blink LED (logic giữ nguyên)
         if (currentTime - lastLedBlinkTime >= ledBlinkInterval) {
          lastLedBlinkTime = currentTime;
          ledState = !ledState; 
          if (networkManager.isConnected()) {
            if (ledState) { RGB_Light(0, 20, 0); } else { RGB_Light(0, 0, 0); }
          } else if (networkManager.isAttemptingWifiReconnect() || networkManager.isAttemptingMqttReconnect()) {
            if (ledState) { RGB_Light(0, 0, 20); } else { RGB_Light(0, 0, 0); }
          } else if (!networkManager.isWifiConnected()){
            if (ledState) { RGB_Light(20, 0, 0); } else { RGB_Light(0, 0, 0); }
          } else { 
             if (ledState) { RGB_Light(20, 20, 0); } else { RGB_Light(0, 0, 0); }
          }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
      }
    }

    // Core 1 Task - Xử lý sự kiện từ Relay Timer Queue
    void Core1TaskCode(void * parameter) {
      AppLogger.info("Core1", "Task bắt đầu trên core " + String(xPortGetCoreID()));
      RelayTimerEvent_t receivedEvent;

      for(;;) {
        // Chờ sự kiện từ g_relayEventQueue
        if (xQueueReceive(g_relayEventQueue, &receivedEvent, portMAX_DELAY) == pdPASS) {
          AppLogger.info("Core1", "Đã nhận sự kiện hết hạn timer cho relay index: " + String(receivedEvent.relayIndex));
          // Gọi RelayManager để tắt relay tương ứng
          // Hàm turnOff của RelayManager sẽ set _statusChanged,
          // Core0Task sẽ phát hiện và gửi cập nhật trạng thái MQTT.
          relayManager.turnOff(receivedEvent.relayIndex);
        }
        // Task sẽ bị block bởi xQueueReceive cho đến khi có sự kiện,
        // không cần vTaskDelay cố định ở đây nữa.
      }
    }

    void setup() {
      Serial.begin(115200);
      uint32_t serialStartTime = millis();
      while (!Serial && (millis() - serialStartTime < 2000)) { 
        delay(10);
      }
      Serial.println(F("\n\nMain: Serial port initialized."));

      // Khởi tạo Queue cho sự kiện Relay Timer
      g_relayEventQueue = xQueueCreate(10, sizeof(RelayTimerEvent_t)); // 10 là kích thước queue
      if (g_relayEventQueue == NULL) {
          Serial.println(F("Main: LỖI - Không thể tạo relay event queue!"));
          ESP.restart(); // Hoặc xử lý lỗi nghiêm trọng khác
      } else {
          Serial.println(F("Main: Relay event queue đã được tạo."));
      }

      // Initialize NetworkManager (logic giữ nguyên)
      if (!networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT)) {
        // Serial.println(F("Main: NetworkManager failed to initialize properly. Check logs. System will attempt to reconnect."));
        // AppLogger sẽ được khởi tạo sau, nên dùng Serial ở đây nếu cần log sớm
      } else {
        // Serial.println(F("Main: NetworkManager initialized. Attempting to connect..."));
      }
      
      // Initialize Logger
      AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO); 
      
      networkManager.setCallback(mqttCallback); // Đặt callback MQTT sau khi logger sẵn sàng
      AppLogger.info("Setup", "MQTT Callback function set.");


      AppLogger.info("Setup", "Hệ thống đang khởi tạo...");
      AppLogger.info("Setup", "ESP32-S3 Dual-Core Irrigation System");

      sensorDataMutex = xSemaphoreCreateMutex();
      
      AppLogger.debug("Setup", "Đang khởi tạo GPIO...");
      GPIO_Init();
      AppLogger.info("Setup", "GPIO đã khởi tạo");
      
      AppLogger.debug("Setup", "Đang khởi tạo RelayManager...");
      relayManager.begin(relayPins, numRelays, g_relayEventQueue); // Truyền queue vào
      
      AppLogger.debug("Setup", "Đang khởi tạo TaskScheduler...");
      taskScheduler.begin();
      
      AppLogger.debug("Setup", "Đang khởi tạo SensorManager...");
      sensorManager.begin();
      
      // Đăng ký các topic MQTT (logic giữ nguyên)
      networkManager.subscribe(MQTT_TOPIC_CONTROL);
      networkManager.subscribe(MQTT_TOPIC_SCHEDULE);
      networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL);
      networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG);
        
      AppLogger.info("Setup", "Đang tạo và ghim task vào các core...");
      xTaskCreatePinnedToCore(
        Core0TaskCode, "Core0Task", STACK_SIZE_CORE0, NULL, PRIORITY_MEDIUM, &core0Task, 0);
      xTaskCreatePinnedToCore(
        Core1TaskCode, "Core1Task", STACK_SIZE_CORE1, NULL, PRIORITY_MEDIUM, &core1Task, 1); 
        
      AppLogger.info("Setup", "Hoàn tất khởi tạo hệ thống. Các task đang chạy.");
      AppLogger.info("Setup", "---------------- HỆ THỐNG SẴN SÀNG ----------------");
    }

    void loop() {
      static unsigned long lastStackCheckTime = 0;
      if (millis() - lastStackCheckTime > 60000) { 
        lastStackCheckTime = millis();
        if(core0Task != NULL) { 
            UBaseType_t core0StackHWM = uxTaskGetStackHighWaterMark(core0Task);
            AppLogger.info("StackCheck", "Core0Task HWM: " + String(core0StackHWM) + " words (" + String(core0StackHWM * sizeof(StackType_t)) + " bytes)");
        }
        if(core1Task != NULL) {
            UBaseType_t core1StackHWM = uxTaskGetStackHighWaterMark(core1Task);
            AppLogger.info("StackCheck", "Core1Task HWM: " + String(core1StackHWM) + " words (" + String(core1StackHWM * sizeof(StackType_t)) + " bytes)");
        }
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }

**Các thay đổi chính trong `main.cpp`:**

- **`g_relayEventQueue`**: Khai báo một `QueueHandle_t` toàn cục.

- **`setup()`**:

  - Khởi tạo `g_relayEventQueue` bằng `xQueueCreate()`.

  - Truyền `g_relayEventQueue` vào `relayManager.begin()`.

- **`Core1TaskCode`**:

  - Không còn gọi `relayManager.update()`.

  - Chờ nhận sự kiện `RelayTimerEvent_t` từ `g_relayEventQueue` bằng `xQueueReceive()`.

  - Khi nhận được sự kiện, gọi `relayManager.turnOff()` cho relay tương ứng.

- **`Core0TaskCode`**: Logic gửi trạng thái relay lên MQTT không thay đổi đáng kể, vì `relayManager.hasStatusChangedAndReset()` vẫn sẽ được kích hoạt khi `relayManager.turnOff()` (do `Core1TaskCode` gọi) thay đổi trạng thái relay.


## 5. Giải Thích Luồng Hoạt Động Mới

1. **Bật Relay Có Thời Gian:**

   - Lệnh MQTT đến `Core0Task` -> `relayManager.processCommand()` -> `relayManager.setRelay(X, true, D)`.

   - `setRelay` bật relay vật lý và khởi động một software timer (liên kết với relay X) để hết hạn sau D mili giây.

2. **Timer Hết Hạn:**

   - Sau D mili giây, software timer của relay X kích hoạt hàm `RelayManager::relayTimerCallback`.

   - `relayTimerCallback` gửi một `RelayTimerEvent_t` (chứa `relayIndex = X`) vào `g_relayEventQueue`.

3. **Xử** Lý Sự **Kiện trên Core 1:**

   - `Core1TaskCode` đang chờ trên `g_relayEventQueue`. Khi có sự kiện, nó được đánh thức.

   - Nhận được `relayIndex = X`.

   - Gọi `relayManager.turnOff(X)`. Hàm này tắt relay vật lý và đặt cờ `_statusChanged = true` trong `RelayManager`.

4. **Cập Nhật Trạng Thái MQTT (trên Core 0):**

   - Trong vòng lặp tiếp theo, `Core0TaskCode` gọi `relayManager.hasStatusChangedAndReset()`, phát hiện có sự thay đổi.

   - Lấy trạng thái relay mới (`relayManager.getStatusJson()`) và gửi lên MQTT.


## 6. Các Bước Tiếp Theo và Lưu Ý

- **Biên Dịch và Kiểm Thử Kỹ Lưỡng:** Đây là bước quan trọng nhất. Kiểm tra các kịch bản:

  - Bật relay vô thời hạn.

  - Bật relay có thời gian.

  - Tắt relay đang chạy có thời gian (thủ công).

  - Relay tự tắt khi hết thời gian.

  - Gửi nhiều lệnh điều khiển relay liên tiếp.

- **Tinh Chỉnh `RelayManager::getStatusJson`:** Đảm bảo `remaining_time` được tính toán chính xác từ trạng thái của software timer (sử dụng `xTimerIsTimerActive()` và `xTimerGetExpiryTime()`).

- **Xử Lý Lỗi FreeRTOS:** Thêm kiểm tra giá trị trả về của các hàm FreeRTOS (ví dụ: `xTimerCreate`, `xQueueCreate`, `xQueueSend` có thể thất bại nếu hết bộ nhớ) và ghi log lỗi tương ứng.

- **Truy Cập Queue trong Callback:** Cách sử dụng `g_relayEventQueue` là một giải pháp đơn giản. Đối với các dự án lớn hơn hoặc khi cần nhiều instance `RelayManager`, bạn có thể cần các giải pháp phức tạp hơn để hàm static callback có thể gửi sự kiện đến đúng queue của instance (ví dụ: lưu con trỏ `this` của `RelayManager` vào Timer ID khi tạo timer, sau đó trong callback lấy lại con trỏ này để truy cập `_relayEventQueue` của instance đó). Tuy nhiên, việc này cần cẩn thận vì Timer ID thường được dùng cho dữ liệu đơn giản.

- **Mở Rộng Kiến Trúc Event-Driven:** Áp dụng các nguyên tắc tương tự (software timers, queues, event groups) cho các module khác như `TaskScheduler` (ví dụ: dùng software timer để kích hoạt kiểm tra lịch) và `SensorManager` (ví dụ: dùng
