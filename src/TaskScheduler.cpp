#include "../include/TaskScheduler.h"
#include "../include/Logger.h"

// File-scope constant for day names, used in logging
static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

TaskScheduler::TaskScheduler(RelayManager& relayManager, EnvironmentManager& envManager) 
    : _relayManager(relayManager), _envManager(envManager) {
    _mutex = xSemaphoreCreateMutex();
    _lastCheckTime = 0;
    _scheduleStatusChanged = false;
}

void TaskScheduler::begin() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        _tasks.clear();
        _activeZonesBits.reset(); 
        _earliestNextCheckTime = 0; 
        _scheduleStatusChanged = true;
        AppLogger.info("TaskSched", "TaskScheduler initialized");
        xSemaphoreGive(_mutex);
    }
}

bool TaskScheduler::addOrUpdateTask(const IrrigationTask& task_param) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        IrrigationTask task = task_param; // Work with a copy to set new defaults if needed

        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [&task](const IrrigationTask& t) { return t.id == task.id; });
        
        if (it != _tasks.end()) {
            AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Updating task ID: %d. Previous state: %d, New active: %s", task.id, it->state, task.active ? "true" : "false");
            
            // Preserve runtime state if task is being updated but not fundamentally changed in a way that requires reset
            task.state = it->state;
            task.start_time = it->start_time;
            task.remaining_duration_on_pause_ms = it->remaining_duration_on_pause_ms;
            task.is_resuming_from_pause = it->is_resuming_from_pause;

            if (it->state == RUNNING && !task.active) {
                AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task ID: %d was RUNNING and is being deactivated. Stopping relays.", task.id);
                stopTask(*it); // Stop based on the *old* task's zone configuration
                it->state = IDLE; // Set to IDLE as it's deactivated
            }
            
            *it = task; // Apply the update
            
            if (it->active) {
                 it->next_run = calculateNextRunTime(*it);
            } else {
                it->next_run = 0;
                if (it->state != IDLE && it->state != COMPLETED) { // If it was RUNNING (handled above), PAUSED, or WAITING
                    if (it->state == RUNNING || it->state == PAUSED) { // If it was running or paused, ensure relays are off
                        stopTask(*it); // This also clears zone bits
                    }
                    it->state = IDLE; 
                }
            }
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Updated irrigation task ID: %d. New next_run: %lu, Active: %s, State: %d", it->id, (unsigned long)it->next_run, it->active ? "true" : "false", it->state);
        } else {
            // Add new task - task_param already has defaults from constructor or JSON parsing
            _tasks.push_back(task);
            IrrigationTask& newTaskRef = _tasks.back(); // Get a reference to the task in the vector
            newTaskRef.state = IDLE; // New tasks start as IDLE.
            newTaskRef.start_time = 0;
            newTaskRef.next_run = calculateNextRunTime(newTaskRef);
            
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Added new irrigation task ID: %d, preemptable: %s", task.id, task.preemptable ? "true" : "false");
        }
        
        _scheduleStatusChanged = true;
        recomputeEarliestNextCheckTime();
        
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
            if (it->state == RUNNING || it->state == PAUSED) { // If running or paused, stop it and free resources
                stopTask(*it);
            }
            _tasks.erase(it);
            _scheduleStatusChanged = true;
            recomputeEarliestNextCheckTime();
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Deleted irrigation task ID: %d", taskId);
            xSemaphoreGive(_mutex);
            return true;
        }
        xSemaphoreGive(_mutex);
    }
    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Task ID not found for deletion: %d", taskId);
    return false;
}

String TaskScheduler::getTasksJson(const char* apiKey) {
    StaticJsonDocument<2048> doc;
    doc["api_key"] = apiKey;
    doc["timestamp"] = (uint32_t)time(NULL);
    JsonArray tasks = doc.createNestedArray("tasks");
    
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        for (const auto& task : _tasks) {
            JsonObject taskObj = tasks.createNestedObject();
            taskObj["id"] = task.id;
            taskObj["active"] = task.active;
            
            JsonArray days = bitmapToDaysArray(doc, task.days);
            taskObj["days"] = days;
            
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", task.hour, task.minute);
            taskObj["time"] = timeStr;
            taskObj["duration"] = task.duration;
            
            JsonArray zones = taskObj.createNestedArray("zones");
            for (uint8_t zone : task.zones) {
                zones.add(zone);
            }
            taskObj["priority"] = task.priority;
            taskObj["preemptable"] = task.preemptable; // Serialize new field
            
            switch (task.state) {
                case IDLE: taskObj["state"] = "idle"; break;
                case RUNNING: taskObj["state"] = "running"; break;
                case COMPLETED: taskObj["state"] = "completed"; break;
                case PAUSED: taskObj["state"] = "paused"; break;
                case WAITING: taskObj["state"] = "waiting"; break;
            }

            if (task.state == PAUSED) {
                taskObj["remaining_duration_ms"] = task.remaining_duration_on_pause_ms;
            }
            
            if (task.next_run > 0) {
                char next_run_str[25];
                struct tm next_timeinfo;
                localtime_r(&task.next_run, &next_timeinfo); // Use localtime_r
                strftime(next_run_str, sizeof(next_run_str), "%Y-%m-%d %H:%M:%S", &next_timeinfo);
                taskObj["next_run"] = next_run_str;
            } else {
                taskObj["next_run"] = nullptr;
            }
            
            if (task.sensor_condition.enabled) {
                addSensorConditionToJson(doc, taskObj, task.sensor_condition);
            }
        }
        xSemaphoreGive(_mutex);
    }
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
    DynamicJsonDocument doc(2048); // Increased size slightly for new fields
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "JSON parsing failed: %s", error.c_str());
        return false;
    }
    
    if (doc.containsKey("delete_tasks")) {
        return processDeleteCommand(json);
    }
    
    if (!doc.containsKey("tasks")) {
        AppLogger.error("TaskSched", "Missing 'tasks' field in command");
        return false;
    }
    
    JsonArray tasksArray = doc["tasks"];
    bool anyChanges = false;
    
    for (JsonObject taskJson : tasksArray) {
        if (!taskJson.containsKey("id") || !taskJson.containsKey("active") ||
            !taskJson.containsKey("days") || !taskJson.containsKey("time") ||
            !taskJson.containsKey("duration") || !taskJson.containsKey("zones")) {
            AppLogger.error("TaskSched", "Missing required fields in task object");
            continue;
        }
        
        IrrigationTask task; // Uses constructor for defaults
        task.id = taskJson["id"];
        task.active = taskJson["active"];
        task.days = daysArrayToBitmap(taskJson["days"]);
        
        String timeStr = taskJson["time"].as<String>();
        int separator = timeStr.indexOf(':');
        if (separator > 0) {
            task.hour = timeStr.substring(0, separator).toInt();
            task.minute = timeStr.substring(separator + 1).toInt();
        } else {
            task.hour = 0; task.minute = 0;
        }
        
        task.duration = taskJson["duration"];
        
        JsonArray zonesArray = taskJson["zones"];
        task.zones.clear();
        for (JsonVariant zone : zonesArray) {
            task.zones.push_back(zone.as<uint8_t>());
        }
        
        task.priority = taskJson.containsKey("priority") ? taskJson["priority"] : 5;
        task.preemptable = taskJson.containsKey("preemptable") ? taskJson["preemptable"].as<bool>() : true; // Default to true
        
        // Sensor conditions
        task.sensor_condition.enabled = false; // Default
        if (taskJson.containsKey("sensor_condition")) {
            JsonObject sensorCondition = taskJson["sensor_condition"];
            parseSensorCondition(sensorCondition, task.sensor_condition);
        }
        
        // state, start_time, next_run, remaining_duration_on_pause_ms, is_resuming_from_pause
        // are handled by addOrUpdateTask logic or default constructor.
        
        if (addOrUpdateTask(task)) { // Pass the fully constructed task
            anyChanges = true;
        }
    }
    
    if (anyChanges) {
        _scheduleStatusChanged = true;
        recomputeEarliestNextCheckTime();
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
        AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "JSON parsing failed for delete command: %s", error.c_str());
        return false;
    }
    
    // Expects a "delete_tasks" array field containing task IDs.
    if (!doc.containsKey("delete_tasks")) {
        AppLogger.error("TaskSched", "Missing 'delete_tasks' field in delete command.");
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
    if (currentMillis - _lastCheckTime < 1000) { // Rate limit
        return;
    }
    _lastCheckTime = currentMillis;

    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo); // Use localtime_r for thread safety

        bool anyStateChangedThisUpdate = false;

        // Section 1: Process RUNNING tasks for completion
        for (auto& task : _tasks) {
            if (task.state == RUNNING) {
                time_t current_target_duration_seconds; // Use time_t for consistency
                if (task.is_resuming_from_pause) {
                    current_target_duration_seconds = task.remaining_duration_on_pause_ms / 1000;
                } else {
                    current_target_duration_seconds = task.duration * 60;
                }

                time_t elapsed_seconds = 0;
                if (now >= task.start_time) { // Should normally always be true for a running task
                    elapsed_seconds = now - task.start_time;
                } else {
                    // This case should ideally not happen if start_time is set correctly.
                    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSchedChk", "Task %d: now (%lu) < task.start_time (%lu)!", task.id, (unsigned long)now, (unsigned long)task.start_time);
                }

                // Enhanced Log for debugging completion check
                
                if (task.is_resuming_from_pause) { // Only log for task đang resume để giảm nhiễu
                    char relay_state_str[128] = "Zones: "; // Increased buffer size
                    if (!task.zones.empty()) {
                        bool first_zone = true;
                        for(uint8_t zone_id : task.zones) {
                            if (zone_id >= 1 && zone_id <= _relayManager.getNumRelays()) { // Assumes getNumRelays exists
                                if (!first_zone) strcat(relay_state_str, ", ");
                                strcat(relay_state_str, "Z");
                                char zone_num_str[4];
                                sprintf(zone_num_str, "%d", zone_id);
                                strcat(relay_state_str, zone_num_str);
                                strcat(relay_state_str, ":");
                                // strcat(relay_state_str, _relayManager.isOn(zone_id - 1) ? "ON" : "OFF"); // Problematic line
                                first_zone = false;
                            }
                        }
                    } else {
                        strcat(relay_state_str, "None");
                    }

                    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSchedChk", "Task %d: RUNNING. Resuming: Y. Elapsed: %llds. Target: %llds. RemMS: %lu. Start: %lu. Now: %lu. %s",
                                   task.id,
                                   (long long)(now - task.start_time), // Calculate elapsed here for current state
                                   (long long)(task.remaining_duration_on_pause_ms / 1000),
                                   task.remaining_duration_on_pause_ms,
                                   (unsigned long)task.start_time,
                                   (unsigned long)now,
                                   relay_state_str
                                   );
                }
                
                if (task.id == 1) { // Or some other way to identify the specific task if IDs can change
                    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSchedDbg",
                                   "Task %d CompletionCheck: now=%lu, start_time=%lu, elapsed=%llds, is_resuming=%s, rem_ms=%lu, target_s=%llds",
                                   task.id,
                                   (unsigned long)now,
                                   (unsigned long)task.start_time,
                                   (long long)(now >= task.start_time ? now - task.start_time : -1), // Calculate elapsed here
                                   task.is_resuming_from_pause ? "TRUE" : "FALSE",
                                   task.remaining_duration_on_pause_ms,
                                   (long long)(task.is_resuming_from_pause ? (task.remaining_duration_on_pause_ms / 1000) : (task.duration * 60))
                                  );
                }

                if (elapsed_seconds >= current_target_duration_seconds) {
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (resuming: %s) COMPLETED. Elapsed: %llds, Target: %llds. Start: %lu, Now: %lu, RemMS: %lu",
                                   task.id, 
                                   task.is_resuming_from_pause ? "Y":"N", 
                                   (long long)elapsed_seconds, 
                                   (long long)current_target_duration_seconds,
                                   (unsigned long)task.start_time,
                                   (unsigned long)now,
                                   task.remaining_duration_on_pause_ms);
                    stopTask(task); // Clears bits, resets resume flags
                    task.state = COMPLETED;
                    task.next_run = calculateNextRunTime(task);
                    anyStateChangedThisUpdate = true;
                }
            }
        }

        // Section 2: Try to start IDLE, WAITING tasks or resume PAUSED tasks
        for (auto& task_to_evaluate : _tasks) {
            if (!task_to_evaluate.active) continue;

            bool canProceedWithEvaluation = false;
            if (task_to_evaluate.state == IDLE) {
                bool isDayMatch = (task_to_evaluate.days & (1 << timeinfo.tm_wday));
                bool isTimeMatch = (timeinfo.tm_hour == task_to_evaluate.hour && timeinfo.tm_min == task_to_evaluate.minute);
                if (now >= task_to_evaluate.next_run && task_to_evaluate.next_run != 0) { // Check against next_run time
                     if (isDayMatch && isTimeMatch) canProceedWithEvaluation = true; // Original time match
                } else if (isDayMatch && isTimeMatch && (task_to_evaluate.next_run == 0 || now >= task_to_evaluate.next_run ) ) { // Fallback for initial runs or if next_run was 0
                    canProceedWithEvaluation = true;
                }

            } else if (task_to_evaluate.state == WAITING || task_to_evaluate.state == PAUSED) {
                canProceedWithEvaluation = true;
            }

            if (!canProceedWithEvaluation) continue;

            // Sensor conditions check (only for IDLE or WAITING trying to start fresh)
            if (task_to_evaluate.state == IDLE || task_to_evaluate.state == WAITING) {
                if (!checkSensorConditions(task_to_evaluate)) {
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d conditions not met, skipping/remaining WAITING.", task_to_evaluate.id);
                    if (task_to_evaluate.state == IDLE) {
                        task_to_evaluate.state = WAITING; // Move to waiting if conditions fail at scheduled time
                        task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true); // Mark as skipped
                    }
                    anyStateChangedThisUpdate = true;
                    continue;
                }
            }

            // Zone conflict resolution and starting/resuming
            bool canRunThisTask = true;
            std::vector<IrrigationTask*> tasksToPauseForThisEvaluation; 

            for (uint8_t zoneId : task_to_evaluate.zones) {
                IrrigationTask* conflictingTask = getTaskUsingZone(zoneId, task_to_evaluate.id);

                if (conflictingTask) { // Zone is busy (RUNNING or PAUSED)
                    if (task_to_evaluate.state == PAUSED && conflictingTask->id == task_to_evaluate.id) {
                        // This is the PAUSED task itself "occupying" its zone, which is fine for resumption check.
                        // But if another task is *also* in this zone, that's a different conflict.
                        // getTaskUsingZone should exclude the task_to_evaluate if it's PAUSED.
                        // Let's refine getTaskUsingZone or ensure PAUSED task isn't seen as conflicting with itself.
                        // For now, this means a PAUSED task checks if its zone is *still* held by its *own* paused reservation.
                        // If another task is using it, that's a conflict.
                        // If zone is busy by *another* task:
                        // This case is complex: task_to_evaluate is PAUSED, wants to resume, but zone is taken by *another* task.
                        // For now, PAUSED task will only resume if its zones are free or taken by itself.
                        continue; // PAUSED task's own reservation is not a conflict for itself
                    }


                    if (task_to_evaluate.priority > conflictingTask->priority) {
                        if (conflictingTask->preemptable) {
                            bool alreadyMarked = false;
                            for(const auto* tp : tasksToPauseForThisEvaluation) if(tp->id == conflictingTask->id) alreadyMarked = true;
                            if(!alreadyMarked) tasksToPauseForThisEvaluation.push_back(conflictingTask);
                        } else {
                            canRunThisTask = false;
                            if (task_to_evaluate.state != WAITING) { // If IDLE or PAUSED
                                task_to_evaluate.state = WAITING;
                                task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true); 
                            }
                            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (state %d) cannot start/resume, zone %u busy with non-preemptable task %d. Setting/keeping WAITING.", task_to_evaluate.id, task_to_evaluate.state, zoneId, conflictingTask->id);
                            anyStateChangedThisUpdate = true;
                            break; 
                        }
                    } else { 
                        canRunThisTask = false;
                        if (task_to_evaluate.state != WAITING) {
                            task_to_evaluate.state = WAITING;
                            task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true);
                        }
                        AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (state %d) cannot start/resume, zone %u busy with higher/equal priority task %d. Setting/keeping WAITING.", task_to_evaluate.id, task_to_evaluate.state, zoneId, conflictingTask->id);
                        anyStateChangedThisUpdate = true;
                        break;
                    }
                }
            } // End zone check loop

            if (canRunThisTask) {
                for (IrrigationTask* taskToActuallyPause : tasksToPauseForThisEvaluation) {
                    if (taskToActuallyPause->state == RUNNING) { // Only pause if it's actually running
                       pauseTask(*taskToActuallyPause);
                       AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d PAUSED by higher priority task %d.", taskToActuallyPause->id, task_to_evaluate.id);
                        anyStateChangedThisUpdate = true;
                    }
                }

                TaskState state_before_action = task_to_evaluate.state; // Capture state before action
                if (task_to_evaluate.state == PAUSED) {
                    resumeTask(task_to_evaluate); 
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d RESUMED.", task_to_evaluate.id);
                } else { // IDLE or WAITING
                    startTask(task_to_evaluate); 
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d STARTED (from %s).", task_to_evaluate.id, (state_before_action == IDLE ? "IDLE" : "WAITING"));
                }
                
                // If the state is now RUNNING (it should be after start/resume) and it changed, or was PAUSED/IDLE/WAITING
                // then we consider the status changed for reporting purposes.
                if (task_to_evaluate.state == RUNNING) {
                    if(state_before_action != RUNNING) { // If it wasn't already running (e.g. IDLE, PAUSED, WAITING)
                        anyStateChangedThisUpdate = true;
                    }
                    // If it was already RUNNING and somehow canRunThisTask was true (e.g. error in logic),
                    // explicitly setting anyStateChangedThisUpdate might not be needed unless an action (like pausing others) occurred.
                    // The pausing of other tasks already sets anyStateChangedThisUpdate.
                } else if (task_to_evaluate.state != state_before_action) {
                    // If state changed to something other than RUNNING (e.g. resumeTask decided to mark COMPLETED directly)
                    anyStateChangedThisUpdate = true;
                }
            }
        } // End main task evaluation loop

        if (anyStateChangedThisUpdate) {
            _scheduleStatusChanged = true;
        }
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
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d skipped due to temperature out of range: %.2f C", task.id, temp);
            return false;
        }
    }
    
    // Air humidity check.
    if (condition.humidity_check) {
        float humidity = _envManager.getHumidity();
        if (humidity < condition.min_humidity || humidity > condition.max_humidity) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d skipped due to humidity out of range: %.2f %%", task.id, humidity);
            return false;
        }
    }
    
    // Soil moisture check (checks all zones of the task).
    if (condition.soil_moisture_check) {
        for (uint8_t zoneId : task.zones) {
            float moisture = _envManager.getSoilMoisture(zoneId);
            if (moisture > condition.min_soil_moisture) { // Assumes task should run if moisture is LOW, so skip if HIGH.
                AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d skipped due to soil moisture in zone %u above threshold: %.2f %%", task.id, zoneId, moisture);
                return false;
            }
        }
    }
    
    // Rain check.
    if (condition.rain_check && condition.skip_when_raining) {
        if (_envManager.isRaining()) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d skipped due to rain", task.id);
            return false;
        }
    }
    
    // Light level check.
    if (condition.light_check) {
        int light = _envManager.getLightLevel();
        if (light < condition.min_light || light > condition.max_light) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d skipped due to light level out of range: %d lux", task.id, light);
            return false;
        }
    }
    
    return true; // All checked conditions are met.
}

void TaskScheduler::startTask(IrrigationTask& task) {
    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Attempting to start task %d. Duration: %u mins. Zones: ...", task.id, task.duration);
    task.start_time = time(NULL);
    task.is_resuming_from_pause = false; 
    task.remaining_duration_on_pause_ms = 0; // Ensure this is reset for a fresh start
    uint32_t duration_ms = task.duration * 60 * 1000;

    String zonesStr = "";
    for (size_t i = 0; i < task.zones.size(); ++i) {
        zonesStr += String(task.zones[i]);
        if (i < task.zones.size() - 1) zonesStr += ", ";
    }
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d starting with zones: [%s]", task.id, zonesStr.c_str());


    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOn(relayIndex, duration_ms);
            _activeZonesBits.set(relayIndex);
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "ERROR: Invalid zoneId %u in startTask for task %d", zoneId, task.id);
        }
    }
    task.state = RUNNING; // Ensure state is set
}

void TaskScheduler::stopTask(IrrigationTask& task) { // Called when task COMPLETED or is forcefully stopped/cancelled
    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Stopping task %d (current state %d). Clearing active zones.", task.id, task.state);
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOff(relayIndex);
            _activeZonesBits.reset(relayIndex); 
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "ERROR: Invalid zoneId %u in stopTask for task %d", zoneId, task.id);
        }
    }
    task.is_resuming_from_pause = false;
    task.remaining_duration_on_pause_ms = 0;
    // task.state is typically set to COMPLETED or IDLE by the caller after this.
}

void TaskScheduler::pauseTask(IrrigationTask& task) {
    if (task.state != RUNNING) {
        AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Attempted to pause task %d but it was not RUNNING (state: %d).", task.id, task.state);
        return;
    }

    time_t now = time(NULL);
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSchedDbg", "PauseTask ID %d: now=%lu, task.start_time=%lu, task.is_resuming_from_pause_before_calc=%s",
                   task.id, (unsigned long)now, (unsigned long)task.start_time, task.is_resuming_from_pause ? "Y":"N");
    time_t elapsed_seconds_before_pause = now - task.start_time;
    
    uint32_t original_duration_seconds_this_run;
    if (task.is_resuming_from_pause) {
        original_duration_seconds_this_run = task.remaining_duration_on_pause_ms / 1000;
    } else {
        original_duration_seconds_this_run = task.duration * 60;
    }
    
    int32_t remaining_sec = original_duration_seconds_this_run - elapsed_seconds_before_pause;
    task.remaining_duration_on_pause_ms = (remaining_sec > 0) ? (unsigned long)remaining_sec * 1000 : 0;

    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Pausing task %d. Original duration for this run: %u s. Elapsed (calc): %ld s. Stored Remaining MS: %lu ms.",
                   task.id, original_duration_seconds_this_run, (long)elapsed_seconds_before_pause, task.remaining_duration_on_pause_ms);

    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
            _relayManager.turnOff(zoneId - 1); 
            // _activeZonesBits.set(zoneId - 1); // Zone bit remains set to reserve it
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "ERROR: Invalid zoneId %u in pauseTask for task %d", zoneId, task.id);
        }
    }
    task.state = PAUSED;
    task.is_resuming_from_pause = false; // It's now paused, not resuming. This flag is for when it *becomes* RUNNING again.
}

void TaskScheduler::resumeTask(IrrigationTask& task) {
    if (task.state != PAUSED) {
         AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Attempted to resume task %d but it was not PAUSED (state: %d).", task.id, task.state);
        return;
    }
    if (task.remaining_duration_on_pause_ms == 0) {
        AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d was PAUSED but has no remaining duration. Marking COMPLETED.", task.id);
        stopTask(task); // Clear zone bits etc.
        task.state = COMPLETED;
        task.next_run = calculateNextRunTime(task);
        _scheduleStatusChanged = true;
        return;
    }

    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Resuming task %d with %lu ms remaining.", task.id, task.remaining_duration_on_pause_ms);
    task.start_time = time(NULL); 
    task.is_resuming_from_pause = true; 

    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
            // _activeZonesBits should already be set for these zones
            _relayManager.turnOn(zoneId - 1, task.remaining_duration_on_pause_ms);
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "ERROR: Invalid zoneId %u in resumeTask for task %d", zoneId, task.id);
        }
    }
    task.state = RUNNING;
}

bool TaskScheduler::isZoneBusy(uint8_t zoneId) {
    if (zoneId >= 1 && zoneId <= _relayManager.getNumRelays()) {
        // Check _activeZonesBits directly, as this should reflect committed zones
        return _activeZonesBits.test(zoneId - 1);
    }
    AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "ERROR: Invalid zoneId in isZoneBusy: %u", zoneId);
    return false;
}

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

time_t TaskScheduler::calculateNextRunTime(IrrigationTask& task, bool isRescheduleAfterSkip) {
    time_t now;
    time(&now);
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "calculateNextRunTime for Task ID: %d (State: %d, Active: %s), Days: 0x%02X, Time: %02d:%02d, Skip: %s, Current Epoch: %lu", 
                    task.id, task.state, task.active ? "T" : "F", task.days, task.hour, task.minute, isRescheduleAfterSkip ? "true" : "false", (unsigned long)now);

    if (!task.active) { // If task is inactive, it has no next run time.
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d is inactive. Setting next_run to 0.", task.id);
        return 0;
    }
    if (task.days == 0) { // If task has no scheduled days.
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d has no scheduled days (days bitmap is 0). Setting next_run to 0.", task.id);
        return 0;
    }

    struct tm timeinfo_now; 
    localtime_r(&now, &timeinfo_now);

    struct tm scheduled_tm = timeinfo_now; 
    scheduled_tm.tm_hour = task.hour;
    scheduled_tm.tm_min = task.minute;
    scheduled_tm.tm_sec = 0;
    
    for (int dayOffset = 0; dayOffset < 8; dayOffset++) { 
        struct tm nextDayCandidate_tm = timeinfo_now;
        nextDayCandidate_tm.tm_mday += dayOffset;
        mktime(&nextDayCandidate_tm); 
        
        if (task.days & (1 << nextDayCandidate_tm.tm_wday)) {
            if (dayOffset == 0) {
                if (!isRescheduleAfterSkip && 
                    (timeinfo_now.tm_hour > task.hour || (timeinfo_now.tm_hour == task.hour && timeinfo_now.tm_min >= task.minute))) {
                    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Today's (%s) time %02d:%02d has passed or is current. Skipping to next valid day.", task.id, dayNames[nextDayCandidate_tm.tm_wday], task.hour, task.minute);
                    continue; 
                }
            }

            scheduled_tm.tm_year = nextDayCandidate_tm.tm_year;
            scheduled_tm.tm_mon  = nextDayCandidate_tm.tm_mon;
            scheduled_tm.tm_mday = nextDayCandidate_tm.tm_mday;

            time_t calculated_time = mktime(&scheduled_tm);

            if (isRescheduleAfterSkip && calculated_time <= now) {
                 AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Rescheduled time %lu for day %s is still past/present (%lu). Continuing to find future slot.", task.id, (unsigned long)calculated_time, dayNames[nextDayCandidate_tm.tm_wday], (unsigned long)now);
                continue;
            }
            
            AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Found next run on %s at %02d:%02d. Epoch: %lu", task.id, dayNames[nextDayCandidate_tm.tm_wday], scheduled_tm.tm_hour, scheduled_tm.tm_min, (unsigned long)calculated_time);
            return calculated_time;
        }
    }
    
    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Task %d: calculateNextRunTime could not find a valid next run day. Setting next_run to 0.", task.id);
    return 0;
}

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

time_t TaskScheduler::getEarliestNextCheckTime() const {
    // No mutex needed for simple read by Core0, assuming _earliestNextCheckTime updates are atomic enough for this purpose.
    // For more complex scenarios or multi-writer, a mutex might be needed here too.
    return _earliestNextCheckTime;
}

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

bool TaskScheduler::hasScheduleStatusChangedAndReset() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        bool changed = _scheduleStatusChanged;
        _scheduleStatusChanged = false; // Reset the flag after reading.
        xSemaphoreGive(_mutex);
        return changed;
    }
    AppLogger.error("TaskSched", "Failed to take mutex in hasScheduleStatusChangedAndReset");
    return true; // Default to true to ensure status is sent if mutex fails, to be safe.
}

IrrigationTask* TaskScheduler::getTaskUsingZone(uint8_t zoneId, int excludeTaskId) {
    for (auto& task : _tasks) {
        if (task.id == excludeTaskId) continue;

        if (task.state == RUNNING || task.state == PAUSED) {
            for (uint8_t z : task.zones) {
                if (z == zoneId) {
                    return &task;
                }
            }
        }
    }
    return nullptr;
}

// Remove private declaration of checkTasks if it's fully replaced by update()
// void TaskScheduler::checkTasks() { /* ... */ } // Remove this if not used 
