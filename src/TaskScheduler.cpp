#include "../include/TaskScheduler.h"

TaskScheduler::TaskScheduler(RelayManager& relayManager, EnvironmentManager& envManager) 
    : _relayManager(relayManager), _envManager(envManager) {
    _mutex = xSemaphoreCreateMutex();
    _lastCheckTime = 0;
}

void TaskScheduler::begin() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Khởi tạo danh sách lịch rỗng
        _tasks.clear();
        _activeZones.clear();
        
        Serial.println("TaskScheduler initialized");
        
        xSemaphoreGive(_mutex);
    }
}

bool TaskScheduler::addOrUpdateTask(const IrrigationTask& task) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Tìm kiếm lịch với ID tương ứng
        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [task](const IrrigationTask& t) { return t.id == task.id; });
        
        if (it != _tasks.end()) {
            // Cập nhật lịch đã tồn tại
            *it = task;
            // Tính thời gian chạy kế tiếp
            it->next_run = calculateNextRunTime(*it);
            
            Serial.println("Updated irrigation task ID: " + String(task.id));
        } else {
            // Thêm lịch mới
            IrrigationTask newTask = task;
            newTask.state = IDLE;
            newTask.start_time = 0;
            // Tính thời gian chạy kế tiếp
            newTask.next_run = calculateNextRunTime(newTask);
            
            _tasks.push_back(newTask);
            
            Serial.println("Added new irrigation task ID: " + String(task.id));
        }
        
        xSemaphoreGive(_mutex);
        return true;
    }
    
    return false;
}

bool TaskScheduler::deleteTask(int taskId) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [taskId](const IrrigationTask& t) { return t.id == taskId; });
        
        if (it != _tasks.end()) {
            // Nếu lịch đang chạy thì dừng lại
            if (it->state == RUNNING) {
                stopTask(*it);
            }
            
            // Xóa lịch
            _tasks.erase(it);
            
            Serial.println("Deleted irrigation task ID: " + String(taskId));
            xSemaphoreGive(_mutex);
            return true;
        }
        
        xSemaphoreGive(_mutex);
    }
    
    Serial.println("Task ID not found: " + String(taskId));
    return false;
}

String TaskScheduler::getTasksJson(const char* apiKey) {
    StaticJsonDocument<2048> doc;
    
    // Thêm API key
    doc["api_key"] = apiKey;
    
    // Thêm timestamp hiện tại
    doc["timestamp"] = (uint32_t)time(NULL);
    
    // Tạo mảng tasks
    JsonArray tasks = doc.createNestedArray("tasks");
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Thêm từng task vào JSON
        for (const auto& task : _tasks) {
            JsonObject taskObj = tasks.createNestedObject();
            
            taskObj["id"] = task.id;
            taskObj["active"] = task.active;
            
            // Chuyển đổi bitmap ngày thành mảng
            JsonArray days = bitmapToDaysArray(doc, task.days);
            taskObj["days"] = days;
            
            // Định dạng giờ:phút
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", task.hour, task.minute);
            taskObj["time"] = timeStr;
            
            taskObj["duration"] = task.duration;
            
            // Thêm các vùng tưới
            JsonArray zones = taskObj.createNestedArray("zones");
            for (uint8_t zone : task.zones) {
                zones.add(zone);
            }
            
            taskObj["priority"] = task.priority;
            
            // Thêm thông tin trạng thái
            switch (task.state) {
                case IDLE:
                    taskObj["state"] = "idle";
                    break;
                case RUNNING:
                    taskObj["state"] = "running";
                    break;
                case COMPLETED:
                    taskObj["state"] = "completed";
                    break;
            }
            
            // Thêm thời gian chạy kế tiếp
            if (task.next_run > 0) {
                char next_run_str[25];
                struct tm next_timeinfo;
                localtime_r(&task.next_run, &next_timeinfo);
                strftime(next_run_str, sizeof(next_run_str), "%Y-%m-%d %H:%M:%S", &next_timeinfo);
                taskObj["next_run"] = next_run_str;
            }
            
            // Thêm thông tin điều kiện cảm biến
            if (task.sensor_condition.enabled) {
                addSensorConditionToJson(doc, taskObj, task.sensor_condition);
            }
        }
        
        xSemaphoreGive(_mutex);
    }
    
    // Chuyển JSON thành chuỗi
    String jsonString;
    serializeJson(doc, jsonString);
    
    return jsonString;
}

void TaskScheduler::addSensorConditionToJson(JsonDocument& doc, JsonObject& taskObj, const SensorCondition& condition) {
    JsonObject sensorCondition = taskObj.createNestedObject("sensor_condition");
    sensorCondition["enabled"] = condition.enabled;
    
    // Điều kiện nhiệt độ
    if (condition.temperature_check) {
        JsonObject temp = sensorCondition.createNestedObject("temperature");
        temp["enabled"] = true;
        temp["min"] = condition.min_temperature;
        temp["max"] = condition.max_temperature;
    } else {
        JsonObject temp = sensorCondition.createNestedObject("temperature");
        temp["enabled"] = false;
    }
    
    // Điều kiện độ ẩm
    if (condition.humidity_check) {
        JsonObject humidity = sensorCondition.createNestedObject("humidity");
        humidity["enabled"] = true;
        humidity["min"] = condition.min_humidity;
        humidity["max"] = condition.max_humidity;
    } else {
        JsonObject humidity = sensorCondition.createNestedObject("humidity");
        humidity["enabled"] = false;
    }
    
    // Điều kiện độ ẩm đất
    if (condition.soil_moisture_check) {
        JsonObject moisture = sensorCondition.createNestedObject("soil_moisture");
        moisture["enabled"] = true;
        moisture["min"] = condition.min_soil_moisture;
    } else {
        JsonObject moisture = sensorCondition.createNestedObject("soil_moisture");
        moisture["enabled"] = false;
    }
    
    // Điều kiện mưa
    if (condition.rain_check) {
        JsonObject rain = sensorCondition.createNestedObject("rain");
        rain["enabled"] = true;
        rain["skip_when_raining"] = condition.skip_when_raining;
    } else {
        JsonObject rain = sensorCondition.createNestedObject("rain");
        rain["enabled"] = false;
    }
    
    // Điều kiện ánh sáng
    if (condition.light_check) {
        JsonObject light = sensorCondition.createNestedObject("light");
        light["enabled"] = true;
        light["min"] = condition.min_light;
        light["max"] = condition.max_light;
    } else {
        JsonObject light = sensorCondition.createNestedObject("light");
        light["enabled"] = false;
    }
}

bool TaskScheduler::processCommand(const char* json) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.println("JSON parsing failed: " + String(error.c_str()));
        return false;
    }
    
    // Kiểm tra API key (nếu cần)
    // Ở đây mình có thể thêm logic xác thực API key
    
    // Kiểm tra nếu là lệnh xóa task
    if (doc.containsKey("delete_tasks")) {
        return processDeleteCommand(json);
    }
    
    // Kiểm tra nếu có trường tasks
    if (!doc.containsKey("tasks")) {
        Serial.println("Missing 'tasks' field in command");
        return false;
    }
    
    JsonArray tasksArray = doc["tasks"];
    bool anyChanges = false;
    
    // Xử lý từng task
    for (JsonObject taskJson : tasksArray) {
        // Kiểm tra các trường bắt buộc
        if (!taskJson.containsKey("id") || 
            !taskJson.containsKey("active") ||
            !taskJson.containsKey("days") ||
            !taskJson.containsKey("time") ||
            !taskJson.containsKey("duration") ||
            !taskJson.containsKey("zones")) {
            
            Serial.println("Missing required fields in task");
            continue;
        }
        
        // Tạo đối tượng IrrigationTask từ JSON
        IrrigationTask task;
        task.id = taskJson["id"];
        task.active = taskJson["active"];
        
        // Chuyển đổi mảng ngày thành bitmap
        task.days = daysArrayToBitmap(taskJson["days"]);
        
        // Phân tích thời gian (định dạng "HH:MM")
        String timeStr = taskJson["time"].as<String>();
        int separator = timeStr.indexOf(':');
        if (separator > 0) {
            task.hour = timeStr.substring(0, separator).toInt();
            task.minute = timeStr.substring(separator + 1).toInt();
        } else {
            task.hour = 0;
            task.minute = 0;
        }
        
        task.duration = taskJson["duration"];
        
        // Xử lý vùng tưới
        JsonArray zonesArray = taskJson["zones"];
        task.zones.clear();
        for (JsonVariant zone : zonesArray) {
            task.zones.push_back(zone.as<uint8_t>());
        }
        
        // Độ ưu tiên (mặc định là 5 nếu không có)
        task.priority = taskJson.containsKey("priority") ? taskJson["priority"] : 5;
        
        // Khởi tạo các giá trị mặc định cho điều kiện cảm biến
        task.sensor_condition.enabled = false;
        task.sensor_condition.temperature_check = false;
        task.sensor_condition.humidity_check = false;
        task.sensor_condition.soil_moisture_check = false;
        task.sensor_condition.rain_check = false;
        task.sensor_condition.light_check = false;
        
        // Xử lý điều kiện cảm biến nếu có
        if (taskJson.containsKey("sensor_condition")) {
            JsonObject sensorCondition = taskJson["sensor_condition"];
            parseSensorCondition(sensorCondition, task.sensor_condition);
        }
        
        // Trạng thái mặc định
        task.state = IDLE;
        task.start_time = 0;
        
        // Thêm hoặc cập nhật task
        if (addOrUpdateTask(task)) {
            anyChanges = true;
        }
    }
    
    return anyChanges;
}

void TaskScheduler::parseSensorCondition(JsonObject& jsonCondition, SensorCondition& condition) {
    // Điều kiện chính
    condition.enabled = jsonCondition.containsKey("enabled") ? jsonCondition["enabled"] : false;
    
    if (!condition.enabled) {
        return;
    }
    
    // Điều kiện nhiệt độ
    if (jsonCondition.containsKey("temperature")) {
        JsonObject temp = jsonCondition["temperature"];
        condition.temperature_check = temp.containsKey("enabled") ? temp["enabled"] : false;
        if (condition.temperature_check) {
            condition.min_temperature = temp.containsKey("min") ? temp["min"].as<float>() : 0.0;
            condition.max_temperature = temp.containsKey("max") ? temp["max"].as<float>() : 50.0;
        }
    }
    
    // Điều kiện độ ẩm
    if (jsonCondition.containsKey("humidity")) {
        JsonObject humidity = jsonCondition["humidity"];
        condition.humidity_check = humidity.containsKey("enabled") ? humidity["enabled"] : false;
        if (condition.humidity_check) {
            condition.min_humidity = humidity.containsKey("min") ? humidity["min"].as<float>() : 0.0;
            condition.max_humidity = humidity.containsKey("max") ? humidity["max"].as<float>() : 100.0;
        }
    }
    
    // Điều kiện độ ẩm đất
    if (jsonCondition.containsKey("soil_moisture")) {
        JsonObject moisture = jsonCondition["soil_moisture"];
        condition.soil_moisture_check = moisture.containsKey("enabled") ? moisture["enabled"] : false;
        if (condition.soil_moisture_check) {
            condition.min_soil_moisture = moisture.containsKey("min") ? moisture["min"].as<float>() : 30.0;
        }
    }
    
    // Điều kiện mưa
    if (jsonCondition.containsKey("rain")) {
        JsonObject rain = jsonCondition["rain"];
        condition.rain_check = rain.containsKey("enabled") ? rain["enabled"] : false;
        if (condition.rain_check) {
            condition.skip_when_raining = rain.containsKey("skip_when_raining") ? rain["skip_when_raining"] : true;
        }
    }
    
    // Điều kiện ánh sáng
    if (jsonCondition.containsKey("light")) {
        JsonObject light = jsonCondition["light"];
        condition.light_check = light.containsKey("enabled") ? light["enabled"] : false;
        if (condition.light_check) {
            condition.min_light = light.containsKey("min") ? light["min"].as<int>() : 0;
            condition.max_light = light.containsKey("max") ? light["max"].as<int>() : 50000;
        }
    }
}

bool TaskScheduler::processDeleteCommand(const char* json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.println("JSON parsing failed: " + String(error.c_str()));
        return false;
    }
    
    // Kiểm tra nếu có trường delete_tasks
    if (!doc.containsKey("delete_tasks")) {
        return false;
    }
    
    JsonArray deleteTasksArray = doc["delete_tasks"];
    bool anyDeleted = false;
    
    // Xóa từng task theo ID
    for (JsonVariant taskId : deleteTasksArray) {
        if (deleteTask(taskId.as<int>())) {
            anyDeleted = true;
        }
    }
    
    return anyDeleted;
}

void TaskScheduler::update() {
    unsigned long currentMillis = millis();
    
    // Giới hạn tần suất kiểm tra (mỗi giây)
    if (currentMillis - _lastCheckTime < 1000) {
        return;
    }
    _lastCheckTime = currentMillis;
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Kiểm tra thời gian hiện tại
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        // 1. Cập nhật trạng thái lịch đang chạy
        for (auto& task : _tasks) {
            if (task.state == RUNNING) {
                // Kiểm tra nếu đã hoàn thành
                if (now - task.start_time >= task.duration * 60) {
                    stopTask(task);
                    task.state = COMPLETED;
                    
                    // Tính thời gian chạy kế tiếp
                    task.next_run = calculateNextRunTime(task);
                    
                    Serial.println("Task " + String(task.id) + " completed, next run at: " + 
                                   String(ctime(&task.next_run)));
                }
            }
        }
        
        // 2. Kiểm tra lịch đến giờ chạy
        for (auto& task : _tasks) {
            if (!task.active || task.state == RUNNING) continue;
            
            // Kiểm tra ngày trong tuần (0 = CN, 1-6 = T2-T7)
            bool isDayMatch = (task.days & (1 << timeinfo.tm_wday));
            
            // Kiểm tra giờ, phút
            bool isTimeMatch = (timeinfo.tm_hour == task.hour && 
                               timeinfo.tm_min == task.minute);
            
            // Nếu đến giờ chạy
            if (isDayMatch && isTimeMatch) {
                Serial.println("Task " + String(task.id) + " scheduled time match");
                
                // Kiểm tra điều kiện cảm biến
                if (!checkSensorConditions(task)) {
                    continue; // Bỏ qua lịch này nếu không thỏa mãn điều kiện cảm biến
                }
                
                // Kiểm tra xem có thể chạy ngay không
                bool canStart = true;
                
                // Kiểm tra từng vùng của lịch
                for (uint8_t zoneId : task.zones) {
                    // Nếu vùng đang bận
                    if (isZoneBusy(zoneId)) {
                        // Kiểm tra độ ưu tiên với task đang chạy
                        if (isHigherPriority(task.id)) {
                            Serial.println("Task " + String(task.id) + 
                                          " has higher priority, stopping conflicts");
                            
                            // Tìm và dừng các lịch ưu tiên thấp hơn
                            for (auto& runningTask : _tasks) {
                                if (runningTask.state == RUNNING) {
                                    // Kiểm tra xem runningTask có sử dụng vùng này không
                                    if (std::find(runningTask.zones.begin(), 
                                                runningTask.zones.end(), 
                                                zoneId) != runningTask.zones.end()) {
                                        stopTask(runningTask);
                                        runningTask.state = COMPLETED;
                                        runningTask.next_run = calculateNextRunTime(runningTask);
                                        
                                        Serial.println("Preempted task " + String(runningTask.id) + 
                                                     " due to higher priority task");
                                    }
                                }
                            }
                        } else {
                            // Không đủ ưu tiên để chạy
                            canStart = false;
                            Serial.println("Task " + String(task.id) + 
                                          " cannot start, lower priority than running tasks");
                            break;
                        }
                    }
                }
                
                // Nếu có thể chạy
                if (canStart) {
                    startTask(task);
                    task.state = RUNNING;
                }
            }
        }
        
        xSemaphoreGive(_mutex);
    }
}

bool TaskScheduler::checkSensorConditions(const IrrigationTask& task) {
    if (!task.sensor_condition.enabled) {
        return true; // Không kích hoạt điều kiện cảm biến, luôn cho phép chạy
    }
    
    const SensorCondition& condition = task.sensor_condition;
    
    // Kiểm tra nhiệt độ
    if (condition.temperature_check) {
        float temp = _envManager.getTemperature();
        if (temp < condition.min_temperature || temp > condition.max_temperature) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to temperature out of range: " + String(temp) + "°C");
            return false;
        }
    }
    
    // Kiểm tra độ ẩm không khí
    if (condition.humidity_check) {
        float humidity = _envManager.getHumidity();
        if (humidity < condition.min_humidity || humidity > condition.max_humidity) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to humidity out of range: " + String(humidity) + "%");
            return false;
        }
    }
    
    // Kiểm tra độ ẩm đất
    if (condition.soil_moisture_check) {
        // Kiểm tra tất cả các vùng tưới
        for (uint8_t zoneId : task.zones) {
            float moisture = _envManager.getSoilMoisture(zoneId);
            if (moisture > condition.min_soil_moisture) {
                Serial.println("Task " + String(task.id) + 
                             " skipped due to soil moisture above threshold: " + 
                             String(moisture) + "% in zone " + String(zoneId));
                return false;
            }
        }
    }
    
    // Kiểm tra mưa
    if (condition.rain_check && condition.skip_when_raining) {
        if (_envManager.isRaining()) {
            Serial.println("Task " + String(task.id) + " skipped due to rain");
            return false;
        }
    }
    
    // Kiểm tra ánh sáng
    if (condition.light_check) {
        int light = _envManager.getLightLevel();
        if (light < condition.min_light || light > condition.max_light) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to light level out of range: " + String(light) + " lux");
            return false;
        }
    }
    
    return true; // Tất cả điều kiện đều thỏa mãn
}

void TaskScheduler::startTask(IrrigationTask& task) {
    // Bật relay cho mỗi vùng
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= 6) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOn(relayIndex, task.duration * 60 * 1000);
            
            // Thêm vào danh sách vùng đang hoạt động
            _activeZones.push_back(zoneId);
        }
    }
    
    // Cập nhật thông tin
    task.start_time = time(NULL);
    
    Serial.println("Started irrigation task " + String(task.id) + 
                  " for " + String(task.duration) + " minutes on zones: ");
                  
    for (uint8_t zoneId : task.zones) {
        Serial.print(zoneId);
        Serial.print(" ");
    }
    Serial.println();
}

void TaskScheduler::stopTask(IrrigationTask& task) {
    // Tắt relay cho mỗi vùng
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= 6) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOff(relayIndex);
            
            // Xóa khỏi danh sách vùng đang hoạt động
            _activeZones.erase(std::remove(_activeZones.begin(), 
                                         _activeZones.end(), 
                                         zoneId), 
                             _activeZones.end());
        }
    }
    
    Serial.println("Stopped irrigation task " + String(task.id));
}

bool TaskScheduler::isZoneBusy(uint8_t zoneId) {
    return std::find(_activeZones.begin(), _activeZones.end(), zoneId) != _activeZones.end();
}

bool TaskScheduler::isHigherPriority(int taskId) {
    auto taskIt = std::find_if(_tasks.begin(), _tasks.end(),
                            [taskId](const IrrigationTask& t) { return t.id == taskId; });
    
    if (taskIt == _tasks.end()) return false;
    
    // Kiểm tra có ưu tiên cao hơn các lịch đang chạy không
    for (const auto& task : _tasks) {
        if (task.state == RUNNING && task.priority >= taskIt->priority) {
            return false;
        }
    }
    
    return true;
}

time_t TaskScheduler::calculateNextRunTime(IrrigationTask& task) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Thiết lập giờ và phút
    timeinfo.tm_hour = task.hour;
    timeinfo.tm_min = task.minute;
    timeinfo.tm_sec = 0;
    
    // Tìm ngày kế tiếp phù hợp
    for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
        struct tm nextDay = timeinfo;
        nextDay.tm_mday += dayOffset;
        mktime(&nextDay);  // Chuẩn hóa
        
        // Kiểm tra ngày trong tuần
        if (task.days & (1 << nextDay.tm_wday)) {
            // Nếu là cùng ngày, kiểm tra xem đã qua giờ chạy chưa
            if (dayOffset == 0) {
                struct tm currentTime;
                localtime_r(&now, &currentTime);
                
                if (currentTime.tm_hour > task.hour || 
                    (currentTime.tm_hour == task.hour && currentTime.tm_min >= task.minute)) {
                    // Đã qua giờ chạy, tìm ngày tiếp theo
                    continue;
                }
            }
            
            // Trả về thời gian tìm được
            return mktime(&nextDay);
        }
    }
    
    // Nếu không tìm thấy ngày phù hợp, quay lại tuần sau
    timeinfo.tm_mday += 7;
    return mktime(&timeinfo);
}

uint8_t TaskScheduler::daysArrayToBitmap(JsonArray daysArray) {
    uint8_t bitmap = 0;
    
    for (JsonVariant dayVar : daysArray) {
        int day = dayVar.as<int>();
        if (day >= 1 && day <= 7) {
            // Chuyển đổi từ định dạng 1-7 (T2-CN) sang 0-6 (CN-T7)
            int adjustedDay = (day % 7);  // CN = 0, T2 = 1, ..., T7 = 6
            bitmap |= (1 << adjustedDay);
        }
    }
    
    return bitmap;
}

JsonArray TaskScheduler::bitmapToDaysArray(JsonDocument& doc, uint8_t daysBitmap) {
    JsonArray array = doc.createNestedArray();
    
    for (int day = 1; day <= 7; day++) {
        // Chuyển đổi từ 1-7 (T2-CN) sang 0-6 (CN-T7)
        int adjustedDay = (day % 7);  // CN = 0, T2 = 1, ..., T7 = 6
        
        if (daysBitmap & (1 << adjustedDay)) {
            array.add(day);
        }
    }
    
    return array;
} 