#include "../include/TaskScheduler.h"
#include "../include/Logger.h"

TaskScheduler::TaskScheduler(RelayManager& relayManager, EnvironmentManager& envManager) 
    : _relayManager(relayManager), _envManager(envManager) {
    _mutex = xSemaphoreCreateMutex();
    _lastCheckTime = 0;
    _scheduleStatusChanged = false;
}

void TaskScheduler::begin() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Initialize with an empty task list.
        _tasks.clear();
        _activeZonesBits.reset(); // Clear all bits, indicating all zones are initially inactive.
        _earliestNextCheckTime = 0; // Recalculate on first update or task addition.
        _scheduleStatusChanged = true; // Mark as changed to send initial status.
        
        Serial.println("TaskScheduler initialized"); // TODO: Replace with AppLogger.info if appropriate timing
        
        xSemaphoreGive(_mutex);
    }
}

bool TaskScheduler::addOrUpdateTask(const IrrigationTask& task) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Find if a task with the same ID already exists.
        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [task](const IrrigationTask& t) { return t.id == task.id; });
        
        if (it != _tasks.end()) {
            // Update existing task.
            *it = task;
            // Recalculate its next run time based on new settings.
            it->next_run = calculateNextRunTime(*it);
            
            Serial.println("Updated irrigation task ID: " + String(task.id)); // TODO: AppLogger
        } else {
            // Add new task.
            IrrigationTask newTask = task;
            newTask.state = IDLE; // New tasks start as IDLE.
            newTask.start_time = 0;
            // Calculate its first run time.
            newTask.next_run = calculateNextRunTime(newTask);
            
            _tasks.push_back(newTask);
            
            Serial.println("Added new irrigation task ID: " + String(task.id)); // TODO: AppLogger
        }
        
        _scheduleStatusChanged = true; // Flag that schedule has changed.
        recomputeEarliestNextCheckTime(); // Update the overall next check time for the scheduler.
        
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
            // If the task to be deleted is currently running, stop it first.
            if (it->state == RUNNING) {
                stopTask(*it);
            }
            
            _tasks.erase(it); // Remove the task from the list.
            _scheduleStatusChanged = true; // Flag that schedule has changed.
            recomputeEarliestNextCheckTime(); // Update the overall next check time.
            
            Serial.println("Deleted irrigation task ID: " + String(taskId)); // TODO: AppLogger
            xSemaphoreGive(_mutex);
            return true;
        }
        
        xSemaphoreGive(_mutex);
    }
    
    Serial.println("Task ID not found for deletion: " + String(taskId)); // TODO: AppLogger
    return false;
}

String TaskScheduler::getTasksJson(const char* apiKey) {
    StaticJsonDocument<2048> doc; // Adjust size as needed for all tasks.
    
    doc["api_key"] = apiKey;
    doc["timestamp"] = (uint32_t)time(NULL); // Current Unix timestamp.
    
    JsonArray tasks = doc.createNestedArray("tasks");
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        // Add each task's details to the JSON array.
        for (const auto& task : _tasks) {
            JsonObject taskObj = tasks.createNestedObject();
            
            taskObj["id"] = task.id;
            taskObj["active"] = task.active;
            
            // Convert day bitmap to a JSON array of day numbers (1-7).
            JsonArray days = bitmapToDaysArray(doc, task.days);
            taskObj["days"] = days;
            
            // Format time as HH:MM string.
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", task.hour, task.minute);
            taskObj["time"] = timeStr;
            
            taskObj["duration"] = task.duration; // Duration in minutes.
            
            // Add irrigation zones for this task.
            JsonArray zones = taskObj.createNestedArray("zones");
            for (uint8_t zone : task.zones) {
                zones.add(zone);
            }
            
            taskObj["priority"] = task.priority;
            
            // Add task state string.
            switch (task.state) {
                case IDLE:
                    taskObj["state"] = "idle";
                    break;
                case RUNNING:
                    taskObj["state"] = "running";
                    break;
                case COMPLETED:
                    taskObj["state"] = "completed"; // Task ran to completion, awaiting next scheduled run.
                    break;
            }
            
            // Add next run time if calculated.
            if (task.next_run > 0) {
                char next_run_str[25];
                struct tm next_timeinfo;
                localtime_r(&task.next_run, &next_timeinfo);
                strftime(next_run_str, sizeof(next_run_str), "%Y-%m-%d %H:%M:%S", &next_timeinfo);
                taskObj["next_run"] = next_run_str;
            }
            
            // Add sensor condition details if enabled for the task.
            if (task.sensor_condition.enabled) {
                addSensorConditionToJson(doc, taskObj, task.sensor_condition);
            }
        }
        
        xSemaphoreGive(_mutex);
    }
    
    // Serialize the JSON document to a string.
    String jsonString;
    serializeJson(doc, jsonString);
    
    return jsonString;
}

void TaskScheduler::addSensorConditionToJson(JsonDocument& doc, JsonObject& taskObj, const SensorCondition& condition) {
    JsonObject sensorCondition = taskObj.createNestedObject("sensor_condition");
    sensorCondition["enabled"] = condition.enabled;
    
    // Temperature condition.
    if (condition.temperature_check) {
        JsonObject temp = sensorCondition.createNestedObject("temperature");
        temp["enabled"] = true;
        temp["min"] = condition.min_temperature;
        temp["max"] = condition.max_temperature;
    } else {
        JsonObject temp = sensorCondition.createNestedObject("temperature");
        temp["enabled"] = false;
    }
    
    // Humidity condition.
    if (condition.humidity_check) {
        JsonObject humidity = sensorCondition.createNestedObject("humidity");
        humidity["enabled"] = true;
        humidity["min"] = condition.min_humidity;
        humidity["max"] = condition.max_humidity;
    } else {
        JsonObject humidity = sensorCondition.createNestedObject("humidity");
        humidity["enabled"] = false;
    }
    
    // Soil moisture condition.
    if (condition.soil_moisture_check) {
        JsonObject moisture = sensorCondition.createNestedObject("soil_moisture");
        moisture["enabled"] = true;
        moisture["min"] = condition.min_soil_moisture;
    } else {
        JsonObject moisture = sensorCondition.createNestedObject("soil_moisture");
        moisture["enabled"] = false;
    }
    
    // Rain condition.
    if (condition.rain_check) {
        JsonObject rain = sensorCondition.createNestedObject("rain");
        rain["enabled"] = true;
        rain["skip_when_raining"] = condition.skip_when_raining;
    } else {
        JsonObject rain = sensorCondition.createNestedObject("rain");
        rain["enabled"] = false;
    }
    
    // Light condition.
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
    
    // TODO: Implement API key validation if required for commands.
    
    // Check if it's a command to delete tasks.
    if (doc.containsKey("delete_tasks")) {
        return processDeleteCommand(json);
    }
    
    // Check for the main "tasks" array for add/update operations.
    if (!doc.containsKey("tasks")) {
        Serial.println("Missing 'tasks' field in command");
        return false;
    }
    
    JsonArray tasksArray = doc["tasks"];
    bool anyChanges = false; // Tracks if any task was successfully added or updated.
    
    // Process each task object in the JSON array.
    for (JsonObject taskJson : tasksArray) {
        // Validate required fields for a task.
        if (!taskJson.containsKey("id") || 
            !taskJson.containsKey("active") ||
            !taskJson.containsKey("days") ||
            !taskJson.containsKey("time") ||
            !taskJson.containsKey("duration") ||
            !taskJson.containsKey("zones")) {
            
            Serial.println("Missing required fields in task object");
            continue; // Skip this malformed task object.
        }
        
        // Create an IrrigationTask object from JSON data.
        IrrigationTask task;
        task.id = taskJson["id"];
        task.active = taskJson["active"];
        
        // Convert JSON array of day numbers (1-7) to a days bitmap.
        task.days = daysArrayToBitmap(taskJson["days"]);
        
        // Parse time string (expected format "HH:MM").
        String timeStr = taskJson["time"].as<String>();
        int separator = timeStr.indexOf(':');
        if (separator > 0) {
            task.hour = timeStr.substring(0, separator).toInt();
            task.minute = timeStr.substring(separator + 1).toInt();
        } else {
            task.hour = 0;
            task.minute = 0;
        }
        
        task.duration = taskJson["duration"]; // Duration in minutes.
        
        // Process zones array.
        JsonArray zonesArray = taskJson["zones"];
        task.zones.clear();
        for (JsonVariant zone : zonesArray) {
            task.zones.push_back(zone.as<uint8_t>());
        }
        
        // Priority (defaults to 5 if not provided).
        task.priority = taskJson.containsKey("priority") ? taskJson["priority"] : 5;
        
        // Initialize sensor condition defaults.
        task.sensor_condition.enabled = false;
        task.sensor_condition.temperature_check = false;
        task.sensor_condition.humidity_check = false;
        task.sensor_condition.soil_moisture_check = false;
        task.sensor_condition.rain_check = false;
        task.sensor_condition.light_check = false;
        
        // Parse sensor conditions if present in JSON.
        if (taskJson.containsKey("sensor_condition")) {
            JsonObject sensorCondition = taskJson["sensor_condition"];
            parseSensorCondition(sensorCondition, task.sensor_condition);
        }
        
        // Default state for new/updated tasks (will be IDLE).
        task.state = IDLE;
        task.start_time = 0;
        
        // Add or update the task in the scheduler.
        if (addOrUpdateTask(task)) {
            anyChanges = true;
        }
    }
    
    // If any tasks were successfully added or updated.
    if (anyChanges) {
        _scheduleStatusChanged = true; // Flag that schedule has changed.
        recomputeEarliestNextCheckTime(); // Update overall next check time.
    }
    
    return anyChanges;
}

void TaskScheduler::parseSensorCondition(JsonObject& jsonCondition, SensorCondition& condition) {
    condition.enabled = jsonCondition.containsKey("enabled") ? jsonCondition["enabled"].as<bool>() : false;
    
    if (!condition.enabled) {
        return; // No further parsing if conditions are globally disabled.
    }
    
    // Temperature condition.
    if (jsonCondition.containsKey("temperature")) {
        JsonObject temp = jsonCondition["temperature"];
        condition.temperature_check = temp.containsKey("enabled") ? temp["enabled"].as<bool>() : false;
        if (condition.temperature_check) {
            condition.min_temperature = temp.containsKey("min") ? temp["min"].as<float>() : 0.0;
            condition.max_temperature = temp.containsKey("max") ? temp["max"].as<float>() : 50.0;
        }
    }
    
    // Humidity condition.
    if (jsonCondition.containsKey("humidity")) {
        JsonObject humidity = jsonCondition["humidity"];
        condition.humidity_check = humidity.containsKey("enabled") ? humidity["enabled"].as<bool>() : false;
        if (condition.humidity_check) {
            condition.min_humidity = humidity.containsKey("min") ? humidity["min"].as<float>() : 0.0;
            condition.max_humidity = humidity.containsKey("max") ? humidity["max"].as<float>() : 100.0;
        }
    }
    
    // Soil moisture condition.
    if (jsonCondition.containsKey("soil_moisture")) {
        JsonObject moisture = jsonCondition["soil_moisture"];
        condition.soil_moisture_check = moisture.containsKey("enabled") ? moisture["enabled"].as<bool>() : false;
        if (condition.soil_moisture_check) {
            condition.min_soil_moisture = moisture.containsKey("min") ? moisture["min"].as<float>() : 30.0;
        }
    }
    
    // Rain condition.
    if (jsonCondition.containsKey("rain")) {
        JsonObject rain = jsonCondition["rain"];
        condition.rain_check = rain.containsKey("enabled") ? rain["enabled"].as<bool>() : false;
        if (condition.rain_check) {
            condition.skip_when_raining = rain.containsKey("skip_when_raining") ? rain["skip_when_raining"].as<bool>() : true;
        }
    }
    
    // Light condition.
    if (jsonCondition.containsKey("light")) {
        JsonObject light = jsonCondition["light"];
        condition.light_check = light.containsKey("enabled") ? light["enabled"].as<bool>() : false;
        if (condition.light_check) {
            condition.min_light = light.containsKey("min") ? light["min"].as<int>() : 0;
            condition.max_light = light.containsKey("max") ? light["max"].as<int>() : 50000;
        }
    }
}

bool TaskScheduler::processDeleteCommand(const char* json) {
    DynamicJsonDocument doc(1024); // Adjust size if many IDs can be deleted at once.
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.println("JSON parsing failed for delete command: " + String(error.c_str())); // TODO: AppLogger
        return false;
    }
    
    // Expects a "delete_tasks" array field containing task IDs.
    if (!doc.containsKey("delete_tasks")) {
        Serial.println("Missing 'delete_tasks' field in delete command."); // TODO: AppLogger
        return false;
    }
    
    JsonArray deleteTasksArray = doc["delete_tasks"];
    bool anyDeleted = false; // Tracks if at least one task was successfully deleted.
    
    // Delete each task specified by ID in the array.
    for (JsonVariant taskId : deleteTasksArray) {
        if (deleteTask(taskId.as<int>())) {
            anyDeleted = true;
        }
    }
    
    return anyDeleted;
}

void TaskScheduler::update() {
    unsigned long currentMillis = millis();
    
    // Rate limit scheduler checks (e.g., once per second).
    if (currentMillis - _lastCheckTime < 1000) {
        return;
    }
    _lastCheckTime = currentMillis;
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        time_t now;
        time(&now);
        
        // If it's not yet time for the earliest scheduled check, exit early.
        if (_earliestNextCheckTime != 0 && now < _earliestNextCheckTime) {
            xSemaphoreGive(_mutex);
            return;
        }
        
        struct tm timeinfo;
        localtime_r(&now, &timeinfo); // Get current local time details.
        
        bool anyStateChangedThisUpdate = false; // Tracks if any task's state changed during this update cycle.
        
        // 1. Update status of currently RUNNING tasks.
        for (auto& task : _tasks) {
            if (task.state == RUNNING) {
                // Check if the task has completed its duration.
                if (now - task.start_time >= task.duration * 60) { // task.duration is in minutes.
                    stopTask(task);
                    
                    TaskState oldState = task.state;
                    task.state = COMPLETED;
                    if (oldState != task.state) {
                        anyStateChangedThisUpdate = true;
                    }
                    
                    // Calculate the next run time for this completed task.
                    task.next_run = calculateNextRunTime(task);
                    
                    Serial.println("Task " + String(task.id) + " completed, next run at: " + 
                                   String(ctime(&task.next_run))); // TODO: AppLogger & better time formatting
                }
            }
        }
        
        // 2. Check for tasks scheduled to start NOW.
        for (auto& task : _tasks) {
            if (!task.active || task.state == RUNNING) continue; // Skip inactive or already running tasks.
            
            // Check if today is a scheduled day for this task (0=Sun, 1=Mon, ..., 6=Sat).
            bool isDayMatch = (task.days & (1 << timeinfo.tm_wday));
            
            // Check if current time matches task's scheduled hour and minute.
            bool isTimeMatch = (timeinfo.tm_hour == task.hour && 
                               timeinfo.tm_min == task.minute);
            
            // If day and time match:
            if (isDayMatch && isTimeMatch) {
                Serial.println("Task " + String(task.id) + " scheduled time match"); // TODO: AppLogger.debug
                
                // Check sensor conditions before starting.
                if (!checkSensorConditions(task)) {
                    Serial.println("Task " + String(task.id) + " conditions not met, skipping."); // TODO: AppLogger.info
                    // Recalculate next run for this skipped task to avoid re-triggering immediately if conditions persist.
                    task.next_run = calculateNextRunTime(task, true); // `true` indicates it's being skipped now.
                    anyStateChangedThisUpdate = true; // Next run time changed.
                    continue; 
                }
                
                bool canStartThisTask = true;
                
                // Check for zone conflicts and priority.
                for (uint8_t zoneId : task.zones) {
                    if (isZoneBusy(zoneId)) {
                        // If zone is busy, check if this task has higher priority to preempt.
                        if (isHigherPriority(task.id, zoneId)) { // Pass zoneId to check specific conflicting task
                            Serial.println("Task " + String(task.id) + 
                                          " has higher priority, stopping conflicting tasks in zone " + String(zoneId)); // TODO: AppLogger
                            
                            // Find and stop lower priority tasks using this zone.
                            for (auto& runningTask : _tasks) {
                                if (runningTask.state == RUNNING && runningTask.priority < task.priority) {
                                    // Check if this runningTask uses the conflicting zoneId.
                                    if (std::find(runningTask.zones.begin(), 
                                                runningTask.zones.end(), 
                                                zoneId) != runningTask.zones.end()) {
                                        stopTask(runningTask);
                                        
                                        TaskState oldState = runningTask.state;
                                        runningTask.state = COMPLETED; // Mark as completed (preempted).
                                        if (oldState != runningTask.state) {
                                            anyStateChangedThisUpdate = true;
                                        }
                                        
                                        runningTask.next_run = calculateNextRunTime(runningTask);
                                        
                                        Serial.println("Preempted task " + String(runningTask.id) + 
                                                     " due to higher priority task " + String(task.id)); // TODO: AppLogger
                                    }
                                }
                            }
                        } else {
                            // Not high enough priority to preempt the currently running task in this zone.
                            canStartThisTask = false;
                            Serial.println("Task " + String(task.id) + 
                                          " cannot start, zone " + String(zoneId) + " busy with higher or equal priority task."); // TODO: AppLogger
                            // Reschedule this task for its next logical slot because it couldn't run now.
                            task.next_run = calculateNextRunTime(task, true); // `true` means it was skipped.
                            anyStateChangedThisUpdate = true; // Next run time changed.
                            break; // Stop checking other zones for this task, as it can't start.
                        }
                    }
                }
                
                // If all checks pass, start the task.
                if (canStartThisTask) {
                    startTask(task);
                    
                    TaskState oldState = task.state;
                    task.state = RUNNING;
                    if (oldState != task.state) {
                        anyStateChangedThisUpdate = true;
                    }
                    // next_run will be recalculated when it finishes or is stopped.
                }
            }
        }
        
        // If any task changed state (started, stopped, completed, next_run updated).
        if (anyStateChangedThisUpdate) {
            _scheduleStatusChanged = true; // Flag that overall schedule status might have changed.
        }
        
        // Recompute the earliest next check time after all updates.
        recomputeEarliestNextCheckTime();
        
        xSemaphoreGive(_mutex);
    }
}

bool TaskScheduler::checkSensorConditions(const IrrigationTask& task) {
    if (!task.sensor_condition.enabled) {
        return true; // Sensor conditions are disabled for this task, so it can run.
    }
    
    const SensorCondition& condition = task.sensor_condition;
    
    // Temperature check.
    if (condition.temperature_check) {
        float temp = _envManager.getTemperature();
        if (temp < condition.min_temperature || temp > condition.max_temperature) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to temperature out of range: " + String(temp) + "°C"); // TODO: AppLogger
            return false;
        }
    }
    
    // Air humidity check.
    if (condition.humidity_check) {
        float humidity = _envManager.getHumidity();
        if (humidity < condition.min_humidity || humidity > condition.max_humidity) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to humidity out of range: " + String(humidity) + "%"); // TODO: AppLogger
            return false;
        }
    }
    
    // Soil moisture check (checks all zones of the task).
    if (condition.soil_moisture_check) {
        for (uint8_t zoneId : task.zones) {
            float moisture = _envManager.getSoilMoisture(zoneId);
            if (moisture > condition.min_soil_moisture) { // Assumes task should run if moisture is LOW, so skip if HIGH.
                Serial.println("Task " + String(task.id) + 
                             " skipped due to soil moisture in zone " + String(zoneId) + 
                             " above threshold: " + String(moisture) + "%"); // TODO: AppLogger
                return false;
            }
        }
    }
    
    // Rain check.
    if (condition.rain_check && condition.skip_when_raining) {
        if (_envManager.isRaining()) {
            Serial.println("Task " + String(task.id) + " skipped due to rain"); // TODO: AppLogger
            return false;
        }
    }
    
    // Light level check.
    if (condition.light_check) {
        int light = _envManager.getLightLevel();
        if (light < condition.min_light || light > condition.max_light) {
            Serial.println("Task " + String(task.id) + 
                         " skipped due to light level out of range: " + String(light) + " lux"); // TODO: AppLogger
            return false;
        }
    }
    
    return true; // All checked conditions are met.
}

void TaskScheduler::startTask(IrrigationTask& task) {
    // Turn ON relays for each zone in the task.
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) { // Check against actual number of relays
            uint8_t relayIndex = zoneId - 1; // Convert 1-based zoneId to 0-based relayIndex
            _relayManager.turnOn(relayIndex, task.duration * 60 * 1000); // Duration to ms for RelayManager timer.
            
            _activeZonesBits.set(relayIndex); // Mark this zone (relay index) as active.
        } else {
            Serial.println("ERROR: Invalid zoneId " + String(zoneId) + " in startTask for task " + String(task.id)); // TODO: AppLogger.error
        }
    }
    
    task.start_time = time(NULL); // Record task start time.
    
    Serial.print("Started irrigation task " + String(task.id) + 
                  " for " + String(task.duration) + " minutes on zones: "); // TODO: AppLogger.info
                  
    for (uint8_t zoneId : task.zones) {
        Serial.print(zoneId);
        Serial.print(" ");
    }
    Serial.println();
}

void TaskScheduler::stopTask(IrrigationTask& task) {
    // Turn OFF relays for each zone in the task.
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
            uint8_t relayIndex = zoneId - 1; // Convert 1-based zoneId to 0-based relayIndex
            _relayManager.turnOff(relayIndex);
            
            _activeZonesBits.reset(relayIndex); // Mark this zone (relay index) as inactive.
        } else {
             Serial.println("ERROR: Invalid zoneId " + String(zoneId) + " in stopTask for task " + String(task.id)); // TODO: AppLogger.error
        }
    }
    
    Serial.println("Stopped irrigation task " + String(task.id)); // TODO: AppLogger.info
}

// Checks if a specific zone (1-based) is currently part of an active (RUNNING) task.
bool TaskScheduler::isZoneBusy(uint8_t zoneId) {
    if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
        return _activeZonesBits.test(zoneId - 1); // Test 0-based index.
    }
    Serial.println("ERROR: Invalid zoneId in isZoneBusy: " + String(zoneId)); // TODO: AppLogger.error
    return false; // Invalid zone ID is considered not busy, or handle error differently.
}

// Checks if the task with `checkingTaskId` has higher priority than any currently RUNNING task that uses `conflictingZoneId`.
bool TaskScheduler::isHigherPriority(int checkingTaskId, uint8_t conflictingZoneId) {
    auto checkingTaskIt = std::find_if(_tasks.begin(), _tasks.end(),
                            [checkingTaskId](const IrrigationTask& t) { return t.id == checkingTaskId; });
    
    if (checkingTaskIt == _tasks.end()) return false; // Task to check not found.
    
    // Iterate through all tasks to find any RUNNING task that uses the conflictingZoneId.
    for (const auto& runningTask : _tasks) {
        if (runningTask.state == RUNNING) {
            // Check if this runningTask uses the conflictingZoneId.
            bool usesConflictingZone = false;
            for (uint8_t zone : runningTask.zones) {
                if (zone == conflictingZoneId) {
                    usesConflictingZone = true;
                    break;
                }
            }

            if (usesConflictingZone) {
                // A running task uses the conflicting zone. Check priority.
                if (runningTask.priority >= checkingTaskIt->priority) {
                    return false; // The running task has higher or equal priority.
                }
            }
        }
    }
    
    return true; // No higher or equal priority task is running in the conflicting zone.
}

// Calculates the next run time for a given task.
// If `isRescheduleAfterSkip` is true, it means the task was just skipped due to conditions/conflicts,
// so its next run should be the next *valid* slot, not necessarily today if today's slot was missed.
time_t TaskScheduler::calculateNextRunTime(IrrigationTask& task, bool isRescheduleAfterSkip) {
    time_t now;
    time(&now);
    struct tm timeinfo_now; // Stores current time details
    localtime_r(&now, &timeinfo_now);

    struct tm scheduled_tm = timeinfo_now; // Start with current time structure
    
    // Set the scheduled hour and minute from the task.
    scheduled_tm.tm_hour = task.hour;
    scheduled_tm.tm_min = task.minute;
    scheduled_tm.tm_sec = 0;
    
    // Find the next suitable day, starting from today.
    for (int dayOffset = 0; dayOffset < 8; dayOffset++) { // Check up to 7 days ahead.
        struct tm nextDayCandidate_tm = timeinfo_now; // Base on current day for offset calculation.
        nextDayCandidate_tm.tm_mday += dayOffset;
        // Normalize the date (handles month/year rollovers).
        // Also sets tm_wday and tm_yday correctly for the new date.
        mktime(&nextDayCandidate_tm); 
        
        // Check if this candidate day is one of the scheduled days for the task.
        if (task.days & (1 << nextDayCandidate_tm.tm_wday)) { // tm_wday: 0=Sun, ..., 6=Sat.
            // If it's today (dayOffset == 0) and we are not rescheduling a skipped task,
            // we need to check if the scheduled time has already passed today.
            if (dayOffset == 0 && !isRescheduleAfterSkip) {
                if (timeinfo_now.tm_hour > task.hour || 
                    (timeinfo_now.tm_hour == task.hour && timeinfo_now.tm_min >= task.minute)) {
                    // Scheduled time for today has already passed. Look for the next valid day.
                    continue;
                }
            }
            
            // Valid day found. Set the task's time to this day.
            scheduled_tm.tm_year = nextDayCandidate_tm.tm_year;
            scheduled_tm.tm_mon  = nextDayCandidate_tm.tm_mon;
            scheduled_tm.tm_mday = nextDayCandidate_tm.tm_mday;
            // Hour, minute, second are already set from task's definition.

            return mktime(&scheduled_tm); // Convert struct tm to time_t.
        }
    }
    
    // Should not be reached if task has at least one day active, as loop is 0-7.
    // Fallback: If no suitable day found within 7 days (e.g., task.days is 0), schedule for a week from now at the task's time.
    // This behavior might need refinement based on desired handling of tasks with no active days.
    scheduled_tm = timeinfo_now; // Reset to current time structure
    scheduled_tm.tm_mday += 7;   // Move one week ahead
    scheduled_tm.tm_hour = task.hour;
    scheduled_tm.tm_min = task.minute;
    scheduled_tm.tm_sec = 0;
    mktime(&scheduled_tm); // Normalize
    Serial.println("Warning: Task " + String(task.id) + " has no valid run day in next 7 days, or days=0. Defaulting to 1 week."); // TODO: AppLogger
    return mktime(&scheduled_tm);
}

// Converts a JSON array of day numbers (1-7, Mon-Sun or Sun-Sat depending on input convention)
// to a bitmap (uint8_t) where bit 0 = Sunday, ..., bit 6 = Saturday.
// Assumes input day numbers are 1=Mon, 2=Tue, ..., 7=Sun for user-friendliness in JSON.
uint8_t TaskScheduler::daysArrayToBitmap(JsonArray daysArray) {
    uint8_t bitmap = 0;
    
    for (JsonVariant dayVar : daysArray) {
        int day = dayVar.as<int>(); // e.g., 1 for Monday, 7 for Sunday
        if (day >= 1 && day <= 7) {
            // Convert to tm_wday convention (0=Sun, 1=Mon, ..., 6=Sat)
            // If input 1=Mon,...,7=Sun:  (day % 7) might map 7(Sun) to 0, 1(Mon) to 1, ..., 6(Sat) to 6.
            // Example: Monday (1) -> (1%7) = 1. Sunday (7) -> (7%7) = 0.
            int adjustedDay = (day == 7) ? 0 : day; // If 7 means Sunday, map to 0. Else, day is 1-6 (Mon-Sat).
            bitmap |= (1 << adjustedDay);
        }
    }
    
    return bitmap;
}

// Converts a days bitmap (bit 0 = Sun, ..., bit 6 = Sat) to a JSON array of day numbers.
// Outputs day numbers as 1=Mon, 2=Tue, ..., 7=Sun for user-friendly JSON.
JsonArray TaskScheduler::bitmapToDaysArray(JsonDocument& doc, uint8_t daysBitmap) {
    JsonArray array = doc.createNestedArray();
    const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}; // For mapping if needed, not directly used for 1-7 numbers

    for (int wday_idx = 0; wday_idx <= 6; wday_idx++) { // 0=Sun, 1=Mon, ..., 6=Sat (tm_wday convention)
        if (daysBitmap & (1 << wday_idx)) {
            // Convert tm_wday index (0-6) back to user-friendly 1-7 (Mon-Sun)
            // Sunday (0) -> 7. Monday (1) -> 1. ... Saturday (6) -> 6.
            int userDay = (wday_idx == 0) ? 7 : wday_idx;
            array.add(userDay);
        }
    }
    // Sort the array for consistent output, e.g., [1, 2, 7] for Mon, Tue, Sun.
    // std::sort can't be directly used on JsonArray. If order matters, collect then add.
    return array;
}

// Gets the earliest next check time required by any active task.
// This is used by the main loop to know when to call TaskScheduler::update().
time_t TaskScheduler::getEarliestNextCheckTime() const {
    // No mutex needed for simple read by Core0, assuming _earliestNextCheckTime updates are atomic enough for this purpose.
    // For more complex scenarios or multi-writer, a mutex might be needed here too.
    return _earliestNextCheckTime;
}

// Recalculates _earliestNextCheckTime based on all active tasks' next_run times.
// Should be called whenever tasks are added, updated, deleted, or when a task completes/starts.
void TaskScheduler::recomputeEarliestNextCheckTime() {
    _earliestNextCheckTime = 0; // Reset to find the minimum.
    time_t now_val;
    time(&now_val);

    for (const auto& task : _tasks) {
        // Consider only active tasks that have a future run time.
        if (task.active && task.next_run > 0 && task.next_run > now_val) { 
            if (_earliestNextCheckTime == 0 || task.next_run < _earliestNextCheckTime) {
                _earliestNextCheckTime = task.next_run;
            }
        }
    }
    
    // Fallback if no active future tasks are found.
    if (_earliestNextCheckTime == 0) {
        if (!_tasks.empty()) {
            // If tasks exist but none are active or scheduled in future, check again in 1 minute.
             _earliestNextCheckTime = now_val + 60; 
        } else {
            // If no tasks at all, check again in 5 minutes (or a longer interval).
            _earliestNextCheckTime = now_val + 300;
        }
    }
}

// Checks if the schedule status has changed since the last call and resets the flag.
// Used by the main loop to determine if it needs to publish an updated schedule status via MQTT.
bool TaskScheduler::hasScheduleStatusChangedAndReset() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool changed = _scheduleStatusChanged;
        _scheduleStatusChanged = false; // Reset the flag after reading.
        xSemaphoreGive(_mutex);
        return changed;
    }
    // Fallback if mutex cannot be taken (should not happen with portMAX_DELAY unless an error occurs).
    // Consider logging an error here.
    AppLogger.error("TaskSched", "Failed to take mutex in hasScheduleStatusChangedAndReset");
    return true; // Default to true to ensure status is sent if mutex fails, to be safe.
} 