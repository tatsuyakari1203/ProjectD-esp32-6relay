# Tổng hợp Giải pháp Cải thiện Logic Code cho Dự án ESP32-S3 6-Relay

Dưới đây là tổng hợp các giải pháp chi tiết nhằm tối ưu hóa logic code, sử dụng tài nguyên hiệu quả và giảm độ trễ cho các tính năng hiện có của dự án ESP32-S3 6-Relay.

## Giải pháp 1: Tối ưu hóa `TaskScheduler` và Tương tác với `Core0TaskCode`

**Mục tiêu:** Giảm tần suất gọi `taskScheduler.update()` không cần thiết, giúp `TaskScheduler` hoạt động "event-driven" hơn thay vì polling liên tục.

**Chi tiết Giải pháp:**

1.  **Trong `TaskScheduler.h`:**
    * Thêm một biến thành viên:
        ```cpp
        time_t _earliestNextCheckTime; // Thời điểm sớm nhất cần kiểm tra lại lịch
        ```
    * Thêm một phương thức public mới:
        ```cpp
        public:
            // ...
            time_t getEarliestNextCheckTime() const;
        ```

2.  **Trong `TaskScheduler.cpp`:**
    * **Khởi tạo (`begin()`):**
        ```cpp
        _earliestNextCheckTime = 0;
        ```
    * **Hàm private `recomputeEarliestNextCheckTime()`:**
        ```cpp
        void TaskScheduler::recomputeEarliestNextCheckTime() {
            if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
                _earliestNextCheckTime = 0; // Reset
                time_t now_val;
                time(&now_val);

                for (const auto& task : _tasks) {
                    if (task.active && task.next_run > now_val) {
                        if (_earliestNextCheckTime == 0 || task.next_run < _earliestNextCheckTime) {
                            _earliestNextCheckTime = task.next_run;
                        }
                    }
                }
                if (_earliestNextCheckTime == 0 && !_tasks.empty()) { // Nếu có task nhưng tất cả đã qua hoặc không active
                     _earliestNextCheckTime = now_val + 60; // Kiểm tra lại sau 1 phút như một fallback
                } else if (_tasks.empty()){
                     _earliestNextCheckTime = now_val + 300; // Nếu không có task nào, kiểm tra lại sau 5 phút
                }
                // Nếu _earliestNextCheckTime vẫn là 0 (nghĩa là không có task active nào trong tương lai),
                // nó sẽ được xử lý ở Core0TaskCode để tránh update liên tục.
                xSemaphoreGive(_mutex);
            }
        }
        ```
    * **Gọi `recomputeEarliestNextCheckTime()`:**
        * Cuối hàm `addOrUpdateTask()`.
        * Cuối hàm `deleteTask()`.
        * Sau khi xử lý các lệnh thay đổi task trong `processCommand()` (nếu `anyChanges` là `true`).
        * Cuối hàm `update()` (sau tất cả các vòng lặp xử lý task).
    * **Trong `update()` (đầu hàm):**
        ```cpp
        // ... (lấy mutex và time_t now) ...
        if (_earliestNextCheckTime != 0 && now < _earliestNextCheckTime) {
            // Chưa đến thời điểm sớm nhất cần kiểm tra, hoặc chưa có lịch nào
            // Vẫn cần cập nhật _lastCheckTime nếu logic polling cơ bản còn giữ lại
            // unsigned long currentMillis = millis();
            // if (currentMillis - _lastCheckTime < 1000) { xSemaphoreGive(_mutex); return; }
            // _lastCheckTime = currentMillis;
            xSemaphoreGive(_mutex);
            return; // Thoát sớm
        }
        // ... (logic còn lại của update()) ...
        // Cuối hàm update(), gọi recomputeEarliestNextCheckTime()
        recomputeEarliestNextCheckTime();
        ```
    * **Trong `TaskScheduler::getEarliestNextCheckTime() const`:**
        ```cpp
        time_t TaskScheduler::getEarliestNextCheckTime() const {
            // Để đơn giản, giả sử Core0 là luồng chính gọi và _earliestNextCheckTime được bảo vệ bởi mutex khi ghi.
            // Nếu cần an toàn tuyệt đối khi đọc từ một task khác, cần dùng mutex.
            return _earliestNextCheckTime;
        }
        ```

3.  **Trong `main.cpp` (`Core0TaskCode`):**
    * Bỏ các biến `lastScheduleCheckTime` và `scheduleCheckInterval`.
    * Thay đổi logic gọi `taskScheduler.update()`:
        ```cpp
        // Trong vòng lặp for(;;) của Core0TaskCode:
        time_t current_time_for_scheduler;
        time(&current_time_for_scheduler);
        time_t next_check = taskScheduler.getEarliestNextCheckTime();

        if (next_check == 0 || current_time_for_scheduler >= next_check) {
            // Nếu next_check là 0 (chưa có lịch, hoặc cần tính toán lại lần đầu)
            // hoặc đã đến/qua thời điểm kiểm tra
            taskScheduler.update();
        }
        ```

**Lợi ích:**
* **Giảm CPU Usage:** `taskScheduler.update()` chỉ thực hiện logic kiểm tra đầy đủ khi có khả năng một lịch sắp đến hạn.
* **Thông minh hơn:** Hệ thống phản ứng dựa trên thời điểm thực sự cần thiết.

---

## Giải pháp 2: Đồng bộ hóa và Tối ưu hóa Dữ liệu Cảm biến

**Mục tiêu:** Đảm bảo dữ liệu cảm biến nhất quán, giảm đọc dư thừa từ cảm biến vật lý.

**Chi tiết Giải pháp:**

1.  **Trong `EnvironmentManager.h`:**
    * Thêm các hàm setter public (nếu chưa có):
        ```cpp
        public:
            // ...
            void setCurrentTemperature(float temp);
            void setCurrentHumidity(float hum);
            void setCurrentHeatIndex(float hi);
            // void setSensorReadStatus(bool status); // Tùy chọn: nếu cần biết trạng thái đọc
        ```

2.  **Trong `EnvironmentManager.cpp`:**
    * Triển khai các hàm setter:
        ```cpp
        void EnvironmentManager::setCurrentTemperature(float temp) { _temperature = temp; }
        void EnvironmentManager::setCurrentHumidity(float hum) { _humidity = hum; }
        void EnvironmentManager::setCurrentHeatIndex(float hi) { _heatIndex = hi; }
        ```
    * Sửa đổi hàm `update()`:
        ```cpp
        void EnvironmentManager::update() {
            unsigned long currentTime = millis();
            // Giới hạn tần suất cập nhật tổng thể của EnvironmentManager (ví dụ: cho các logic khác không phải DHT)
            if (currentTime - _lastUpdateTime < 1000) {
                return;
            }
            _lastUpdateTime = currentTime;

            // KHÔNG gọi _sensorManager.readSensors() ở đây nữa.
            // Dữ liệu nhiệt độ, độ ẩm, chỉ số nhiệt từ DHT sẽ được đẩy từ main.cpp
        }
        ```

3.  **Trong `main.cpp` (`Core0TaskCode`):**
    * Thay đổi logic đọc và cập nhật cảm biến:
        ```cpp
        // Trong vòng lặp for(;;) của Core0TaskCode, phần đọc cảm biến:
        if (currentTime - lastSensorReadTime >= sensorReadInterval) {
            lastSensorReadTime = currentTime;
            // bool readSuccessLocal = false; // Không cần thiết nếu chỉ cập nhật giá trị

            if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
                if (sensorManager.readSensors()) { // Đọc cảm biến DHT
                    // Cập nhật ngay vào EnvironmentManager
                    envManager.setCurrentTemperature(sensorManager.getTemperature());
                    envManager.setCurrentHumidity(sensorManager.getHumidity());
                    envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());

                    // (Phần còn lại: print debug, gửi MQTT từ sensorManager.getJsonPayload())
                    // ...
                    if (networkManager.isConnected()) {
                        String payload = sensorManager.getJsonPayload(API_KEY); // JSON vẫn lấy từ SensorManager
                        networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
                    }
                    // ...
                } else {
                    Serial.println("ERROR: Failed to read from sensors");
                    // Có thể set giá trị mặc định hoặc báo lỗi cho EnvironmentManager nếu cần
                    // envManager.setCurrentTemperature(ERROR_VALUE); // Ví dụ
                }
                xSemaphoreGive(sensorDataMutex);
            }
        }

        // Gọi envManager.update() riêng biệt (nếu nó có logic khác ngoài việc lấy từ DHT)
        if (currentTime - lastEnvUpdateTime >= envUpdateInterval) {
            lastEnvUpdateTime = currentTime;
            envManager.update(); // envManager.update() giờ đây không đọc lại DHT
        }
        ```

**Lợi ích:**
* **Nhất quán Dữ liệu:** `TaskScheduler` (qua `EnvironmentManager`) và MQTT sensors dùng chung dữ liệu cảm biến từ một lần đọc.
* **Giảm Gọi Hàm:** Tránh gọi `sensorManager.readSensors()` dư thừa.

---

## Giải pháp 3: Publish Trạng thái MQTT "On Change"

**Mục tiêu:** Giảm lưu lượng MQTT bằng cách chỉ gửi cập nhật trạng thái khi có thay đổi thực sự.

**Chi tiết Giải pháp:**

1.  **Trong `RelayManager.h` và `TaskScheduler.h`:**
    * Thêm cờ `bool _statusChanged;` (cho `RelayManager`) và `bool _scheduleStatusChanged;` (cho `TaskScheduler`).
    * Thêm các hàm `bool hasStatusChangedAndReset()` và `bool hasScheduleStatusChangedAndReset()` tương ứng.

2.  **Trong `RelayManager.cpp` và `TaskScheduler.cpp`:**
    * Khởi tạo các cờ `_statusChanged = true;` và `_scheduleStatusChanged = true;` trong hàm `begin()` (để gửi trạng thái ban đầu).
    * **Set cờ `true` khi có thay đổi:**
        * `RelayManager::setRelay()`: set `_statusChanged = true;`.
        * `RelayManager::update()` (khi tự động tắt relay): set `_statusChanged = true;`.
        * `RelayManager::processCommand()`: Nếu xử lý thành công và có thay đổi, set `_statusChanged = true;`.
        * `TaskScheduler::addOrUpdateTask()`, `deleteTask()`, `processCommand()` (khi thay đổi lịch): set `_scheduleStatusChanged = true;`.
        * `TaskScheduler::update()` (khi trạng thái `RUNNING`/`COMPLETED` của lịch thay đổi): set `_scheduleStatusChanged = true;`.
    * **Triển khai `hasStatusChangedAndReset()`:**
        ```cpp
        // Ví dụ cho RelayManager
        bool RelayManager::hasStatusChangedAndReset() {
            if (xSemaphoreTake(_mutex, portMAX_DELAY)) { // Sử dụng mutex của class
                bool changed = _statusChanged;
                _statusChanged = false; // Reset cờ
                xSemaphoreGive(_mutex);
                return changed;
            }
            // Fallback: nếu không lấy được mutex, coi như có thay đổi để đảm bảo gửi
            // hoặc log lỗi và trả về false. Để đơn giản, có thể trả về true.
            return true; 
        }
        // Tương tự cho TaskScheduler
        ```

3.  **Trong `main.cpp` (`Core0TaskCode`):**
    * Bỏ các biến `lastStatusReportTime` và `statusReportInterval`.
    * Thêm logic gửi dự phòng (tần suất thấp):
        ```cpp
        unsigned long lastForcedStatusReportTime = 0;
        const unsigned long forcedStatusReportInterval = 5 * 60 * 1000; // Gửi lại sau mỗi 5 phút
        ```
    * Sửa đổi logic gửi trạng thái:
        ```cpp
        // Trong vòng lặp for(;;) của Core0TaskCode:
        // unsigned long currentTime = millis(); // Đã có ở trên
        if (networkManager.isConnected()) {
            bool forcedReport = (currentTime - lastForcedStatusReportTime >= forcedStatusReportInterval);

            if (relayManager.hasStatusChangedAndReset() || forcedReport) {
                String statusPayload = relayManager.getStatusJson(API_KEY);
                networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
                if (forcedReport) { // Chỉ reset lastForcedStatusReportTime nếu forcedReport là lý do gửi
                    lastForcedStatusReportTime = currentTime;
                }
            }

            if (taskScheduler.hasScheduleStatusChangedAndReset() || forcedReport) {
                String schedulePayload = taskScheduler.getTasksJson(API_KEY);
                networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
                if (forcedReport) { // Đảm bảo lastForcedStatusReportTime được cập nhật nếu forcedReport là lý do
                     // Nếu cả hai đều gửi do forcedReport, dòng này chỉ cần một lần
                    lastForcedStatusReportTime = currentTime;
                }
            }
             // Nếu forcedReport nhưng không có gì thay đổi, vẫn cập nhật thời gian
            if (forcedReport && !relayManager.hasStatusChangedAndReset() && !taskScheduler.hasScheduleStatusChangedAndReset()){
                 lastForcedStatusReportTime = currentTime;
            }
        }
        ```
    * **Trong `mqttCallback`:** Sau khi gọi `relayManager.processCommand(message)` hoặc `taskScheduler.processCommand(message)` thành công, **không** cần publish trạng thái ngay lập tức nữa. Các hàm `processCommand` này giờ đây sẽ tự set cờ `_statusChanged` / `_scheduleStatusChanged` bên trong nếu chúng thực sự gây ra thay đổi. `Core0TaskCode` sẽ đảm nhận việc gửi.

**Lợi ích:**
* **Giảm Lưu lượng MQTT:** Chỉ gửi dữ liệu khi cần thiết.
* **Tập trung Logic Publish:** Logic gửi trạng thái tập trung ở `Core0TaskCode`.

---

## Giải pháp 4: Tối ưu hóa `_activeZones` trong `TaskScheduler`

**Mục tiêu:** Sử dụng cấu trúc dữ liệu hiệu quả hơn cho việc theo dõi các zone đang hoạt động.

**Chi tiết Giải pháp:**

1.  **Trong `TaskScheduler.h`:**
    * Thêm `#include <bitset>`.
    * Thay thế `std::vector<uint8_t> _activeZones;` bằng:
        ```cpp
        // Giả sử có tối đa 6 relay/zone, đánh số từ 1-6
        // Sẽ map zone 1 vào bit 0, zone 2 vào bit 1, ..., zone 6 vào bit 5
        std::bitset<6> _activeZonesBits;
        ```

2.  **Trong `TaskScheduler.cpp`:**
    * **Trong `begin()`:**
        ```cpp
        _activeZonesBits.reset(); // Xóa tất cả các bit
        ```
    * **Sửa đổi `startTask(IrrigationTask& task)`:**
        ```cpp
        // ... (bên trong vòng lặp for (uint8_t zoneId : task.zones))
        if (zoneId >= 1 && zoneId <= 6) {
            uint8_t relayIndex = zoneId - 1; // Chuyển từ 1-based sang 0-based
            _relayManager.turnOn(relayIndex, task.duration * 60 * 1000);
            _activeZonesBits.set(relayIndex); // Set bit tương ứng (true)
        }
        // ...
        ```
    * **Sửa đổi `stopTask(IrrigationTask& task)`:**
        ```cpp
        // ... (bên trong vòng lặp for (uint8_t zoneId : task.zones))
        if (zoneId >= 1 && zoneId <= 6) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOff(relayIndex);
            _activeZonesBits.reset(relayIndex); // Clear bit tương ứng (false)
        }
        // ...
        ```
    * **Sửa đổi `isZoneBusy(uint8_t zoneId)`:** (zoneId là 1-6)
        ```cpp
        bool TaskScheduler::isZoneBusy(uint8_t zoneId) {
            if (zoneId >= 1 && zoneId <= 6) {
                return _activeZonesBits.test(zoneId - 1); // Kiểm tra bit (0-based)
            }
            Serial.println("ERROR: Invalid zoneId in isZoneBusy: " + String(zoneId));
            return false; // ID zone không hợp lệ
        }
        ```

**Lợi ích:**
* **Hiệu quả Bộ nhớ và Tốc độ:** `std::bitset` rất hiệu quả và nhanh cho các thao tác bit.
* **Đơn giản hóa Logic:** Không cần các thao tác phức tạp của `std::vector::erase/remove`.

---

**Lưu ý Chung Khi Triển Khai:**

* **Kiểm thử Kỹ lưỡng:** Sau mỗi thay đổi, đặc biệt là các thay đổi về logic và quản lý trạng thái, cần kiểm thử toàn diện các trường hợp sử dụng.
* **Mutex:** Đảm bảo rằng tất cả các truy cập vào tài nguyên dùng chung (ví dụ: `_tasks`, `_activeZonesBits`, `_statusChanged`, `_earliestNextCheckTime`) từ nhiều task đều được bảo vệ đúng cách bằng mutex.
* **Đọc hiểu Code:** Các thay đổi này có thể làm tăng một chút độ phức tạp ban đầu khi đọc hiểu code, nhưng sẽ mang lại lợi ích về hiệu suất và tài nguyên về lâu dài. Comment rõ ràng các thay đổi logic.

