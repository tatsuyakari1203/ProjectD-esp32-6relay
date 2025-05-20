#include "../include/TaskScheduler.h"
#include "../include/Logger.h"
#include <Arduino.h> // For millis()

// File-scope constant for day names, used in logging
static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

TaskScheduler::TaskScheduler(RelayManager& relayManager, EnvironmentManager& envManager)
    : _relayManager(relayManager), _envManager(envManager) {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        Serial.println("FATAL ERROR: Failed to create TaskScheduler mutex!");
        // Consider ESP.restart() or other critical error handling
    }
    _lastCheckTime = 0;
    _scheduleStatusChanged = false;
    _earliestNextCheckTime = 0; // Initialize
}

void TaskScheduler::begin() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _tasks.clear();
        _activeZonesBits.reset();
        _earliestNextCheckTime = 0;
        _scheduleStatusChanged = true; // Indicate status needs to be sent
        AppLogger.info("TaskSched", "TaskScheduler initialized and tasks cleared.");
        recomputeEarliestNextCheckTime(); // Compute initial check time
        xSemaphoreGive(_mutex);
    } else {
        AppLogger.error("TaskSched", "Failed to take mutex in begin()");
    }
}

bool TaskScheduler::addOrUpdateTask(const IrrigationTask& task_param) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        IrrigationTask task_to_process = task_param; // Work with a copy

        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [&task_to_process](const IrrigationTask& t) { return t.id == task_to_process.id; });

        if (it != _tasks.end()) { // Task exists, update it
            AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Updating task ID: %d. Current state: %d, New active: %s", it->id, it->state, task_to_process.active ? "true" : "false");

            TaskState old_state = it->state;
            time_t old_start_time = it->start_time;
            unsigned long old_remaining_duration = it->remaining_duration_on_pause_ms;
            bool old_is_resuming = it->is_resuming_from_pause;

            // If the task was RUNNING and is now being deactivated
            if (old_state == RUNNING && !task_to_process.active) {
                AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task ID: %d was RUNNING and is being deactivated. Stopping relays.", it->id);
                stopTask(*it); // Stop using the existing task's zone info
                // Update the iterator directly, as task_to_process doesn't have the correct runtime context for stopTask
                it->state = IDLE;
                it->is_resuming_from_pause = false;
                it->remaining_duration_on_pause_ms = 0;
                 // Apply other properties from task_to_process to 'it'
                it->active = task_to_process.active;
                it->days = task_to_process.days;
                it->hour = task_to_process.hour;
                it->minute = task_to_process.minute;
                it->duration = task_to_process.duration;
                it->zones = task_to_process.zones;
                it->priority = task_to_process.priority;
                it->preemptable = task_to_process.preemptable;
                it->sensor_condition = task_to_process.sensor_condition;

            } else { // Preserve runtime state if not deactivating a running task, then apply update
                task_to_process.state = old_state;
                task_to_process.start_time = old_start_time;
                task_to_process.remaining_duration_on_pause_ms = old_remaining_duration;
                task_to_process.is_resuming_from_pause = old_is_resuming;
                *it = task_to_process; // Apply the update
            }


            // Recalculate next_run and adjust state if needed based on new 'active' status
            if (it->active) {
                 it->next_run = calculateNextRunTime(*it);
            } else { // Task is being made inactive (or was already inactive and re-confirmed)
                it->next_run = 0;
                if (it->state == PAUSED || it->state == WAITING) { // If it was PAUSED or WAITING, and now inactive
                    if (it->state == PAUSED) stopTask(*it); // Ensure relays are off if it was PAUSED
                    it->state = IDLE;
                }
                // If it was RUNNING and deactivated, state is already IDLE from above.
                // If it was COMPLETED, it remains COMPLETED and inactive.
                // If it was IDLE, it remains IDLE and inactive.
            }
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Updated irrigation task ID: %d. New next_run: %lu, Active: %s, State: %d", it->id, (unsigned long)it->next_run, it->active ? "true" : "false", it->state);
        } else { // Task does not exist, add it as new
            _tasks.push_back(task_to_process);
            IrrigationTask& newTaskRef = _tasks.back();
            newTaskRef.state = IDLE;
            newTaskRef.start_time = 0;
            newTaskRef.is_resuming_from_pause = false;
            newTaskRef.remaining_duration_on_pause_ms = 0;
            if (newTaskRef.active) {
                newTaskRef.next_run = calculateNextRunTime(newTaskRef);
            } else {
                newTaskRef.next_run = 0;
            }
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Added new irrigation task ID: %d, preemptable: %s, next_run: %lu", newTaskRef.id, newTaskRef.preemptable ? "true" : "false", (unsigned long)newTaskRef.next_run);
        }

        _scheduleStatusChanged = true;
        recomputeEarliestNextCheckTime();
        xSemaphoreGive(_mutex);
        return true;
    }
    AppLogger.error("TaskSched", "Failed to take mutex in addOrUpdateTask");
    return false;
}

bool TaskScheduler::deleteTask(int taskId) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        auto it = std::find_if(_tasks.begin(), _tasks.end(),
                              [taskId](const IrrigationTask& t) { return t.id == taskId; });

        if (it != _tasks.end()) {
            if (it->state == RUNNING || it->state == PAUSED) {
                AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task ID: %d is %s and being deleted. Stopping relays.", taskId, it->state == RUNNING ? "RUNNING" : "PAUSED");
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
    } else {
        AppLogger.error("TaskSched", "Failed to take mutex in deleteTask");
    }
    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Task ID not found for deletion: %d", taskId);
    return false;
}

String TaskScheduler::getTasksJson(const char* apiKey) {
    StaticJsonDocument<2048> doc;
    doc["api_key"] = apiKey;
    doc["timestamp"] = (uint32_t)time(NULL);
    JsonArray tasks_array = doc.createNestedArray("tasks");

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        for (const auto& task_item : _tasks) {
            JsonObject taskObj = tasks_array.createNestedObject();
            taskObj["id"] = task_item.id;
            taskObj["active"] = task_item.active;
            JsonArray days = bitmapToDaysArray(doc, task_item.days);
            taskObj["days"] = days;
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", task_item.hour, task_item.minute);
            taskObj["time"] = timeStr;
            taskObj["duration"] = task_item.duration;
            JsonArray zones = taskObj.createNestedArray("zones");
            for (uint8_t zone : task_item.zones) {
                zones.add(zone);
            }
            taskObj["priority"] = task_item.priority;
            taskObj["preemptable"] = task_item.preemptable;

            switch (task_item.state) {
                case IDLE: taskObj["state"] = "idle"; break;
                case RUNNING: taskObj["state"] = "running"; break;
                case COMPLETED: taskObj["state"] = "completed"; break;
                case PAUSED: taskObj["state"] = "paused"; break;
                case WAITING: taskObj["state"] = "waiting"; break;
            }

            if (task_item.state == PAUSED || (task_item.state == RUNNING && task_item.is_resuming_from_pause)) {
                taskObj["remaining_duration_ms"] = task_item.remaining_duration_on_pause_ms;
            }
            if (task_item.is_resuming_from_pause && task_item.state == RUNNING) { // Only set is_resuming if actually running and resuming
                taskObj["is_resuming"] = true;
            }

            if (task_item.next_run > 0) {
                char next_run_str[25];
                struct tm next_timeinfo;
                localtime_r(&task_item.next_run, &next_timeinfo);
                strftime(next_run_str, sizeof(next_run_str), "%Y-%m-%d %H:%M:%S", &next_timeinfo);
                taskObj["next_run"] = next_run_str;
            } else {
                taskObj["next_run"] = nullptr;
            }

            if (task_item.sensor_condition.enabled) {
                addSensorConditionToJson(doc, taskObj, task_item.sensor_condition);
            }
        }
        xSemaphoreGive(_mutex);
    } else {
        AppLogger.error("TaskSched", "Failed to take mutex in getTasksJson");
    }
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void TaskScheduler::addSensorConditionToJson(JsonDocument& doc, JsonObject& taskObj, const SensorCondition& condition) {
    JsonObject sensorCondition = taskObj.createNestedObject("sensor_condition");
    sensorCondition["enabled"] = condition.enabled;

    JsonObject temp_cond = sensorCondition.createNestedObject("temperature");
    temp_cond["enabled"] = condition.temperature_check;
    if (condition.temperature_check) {
        temp_cond["min"] = condition.min_temperature;
        temp_cond["max"] = condition.max_temperature;
    }

    JsonObject humidity_cond = sensorCondition.createNestedObject("humidity");
    humidity_cond["enabled"] = condition.humidity_check;
    if (condition.humidity_check) {
        humidity_cond["min"] = condition.min_humidity;
        humidity_cond["max"] = condition.max_humidity;
    }

    JsonObject soil_cond = sensorCondition.createNestedObject("soil_moisture");
    soil_cond["enabled"] = condition.soil_moisture_check;
    if (condition.soil_moisture_check) {
        soil_cond["min"] = condition.min_soil_moisture;
    }

    JsonObject rain_cond = sensorCondition.createNestedObject("rain");
    rain_cond["enabled"] = condition.rain_check;
    if (condition.rain_check) {
        rain_cond["skip_when_raining"] = condition.skip_when_raining;
    }

    JsonObject light_cond = sensorCondition.createNestedObject("light");
    light_cond["enabled"] = condition.light_check;
    if (condition.light_check) {
        light_cond["min"] = condition.min_light;
        light_cond["max"] = condition.max_light;
    }
}

bool TaskScheduler::processCommand(const char* json) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "JSON parsing failed for command: %s", error.c_str());
        return false;
    }

    if (doc.containsKey("delete_tasks")) {
        return processDeleteCommand(json);
    }

    if (!doc.containsKey("tasks")) {
        AppLogger.error("TaskSched", "Command missing 'tasks' field");
        return false;
    }

    JsonArray tasksArray = doc["tasks"];
    if (tasksArray.isNull() || tasksArray.size() == 0) {
        AppLogger.warning("TaskSched", "'tasks' array is null or empty in command.");
        return false;
    }

    bool anyChangesMade = false;
    for (JsonObject taskJson : tasksArray) {
        if (!taskJson.containsKey("id") || !taskJson.containsKey("active") ||
            !taskJson.containsKey("days") || !taskJson.containsKey("time") ||
            !taskJson.containsKey("duration") || !taskJson.containsKey("zones")) {
            AppLogger.error("TaskSched", "Task object missing required fields. Skipping.");
            continue;
        }

        IrrigationTask task_from_json;
        task_from_json.id = taskJson["id"];
        task_from_json.active = taskJson["active"];
        task_from_json.days = daysArrayToBitmap(taskJson["days"]);
        String timeStr = taskJson["time"].as<String>();
        int separator = timeStr.indexOf(':');
        if (separator > 0 && timeStr.length() > separator) {
            task_from_json.hour = timeStr.substring(0, separator).toInt();
            task_from_json.minute = timeStr.substring(separator + 1).toInt();
        } else {
            AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Invalid time format for task %d: %s. Defaulting to 00:00.", task_from_json.id, timeStr.c_str());
            task_from_json.hour = 0; task_from_json.minute = 0;
        }
        task_from_json.duration = taskJson["duration"];
        JsonArray zonesArray = taskJson["zones"];
        task_from_json.zones.clear();
        for (JsonVariant zone : zonesArray) {
            task_from_json.zones.push_back(zone.as<uint8_t>());
        }
        task_from_json.priority = taskJson.containsKey("priority") ? taskJson["priority"].as<uint8_t>() : 5;
        task_from_json.preemptable = taskJson.containsKey("preemptable") ? taskJson["preemptable"].as<bool>() : true;
        task_from_json.sensor_condition.enabled = false;
        if (taskJson.containsKey("sensor_condition")) {
            JsonObject sensorConditionJson = taskJson["sensor_condition"];
            parseSensorCondition(sensorConditionJson, task_from_json.sensor_condition);
        }
        if (addOrUpdateTask(task_from_json)) {
            anyChangesMade = true;
        }
    }
    return anyChangesMade;
}

void TaskScheduler::parseSensorCondition(JsonObject& jsonCondition, SensorCondition& condition) {
    condition.enabled = jsonCondition.containsKey("enabled") ? jsonCondition["enabled"].as<bool>() : false;
    if (!condition.enabled) {
        condition.temperature_check = false;
        condition.humidity_check = false;
        condition.soil_moisture_check = false;
        condition.rain_check = false;
        condition.light_check = false;
        return;
    }
    condition.temperature_check = false;
    if (jsonCondition.containsKey("temperature")) {
        JsonObject temp_json = jsonCondition["temperature"];
        condition.temperature_check = temp_json.containsKey("enabled") ? temp_json["enabled"].as<bool>() : false;
        if (condition.temperature_check) {
            condition.min_temperature = temp_json.containsKey("min") ? temp_json["min"].as<float>() : 0.0f;
            condition.max_temperature = temp_json.containsKey("max") ? temp_json["max"].as<float>() : 50.0f;
        }
    }
    condition.humidity_check = false;
    if (jsonCondition.containsKey("humidity")) {
        JsonObject humidity_json = jsonCondition["humidity"];
        condition.humidity_check = humidity_json.containsKey("enabled") ? humidity_json["enabled"].as<bool>() : false;
        if (condition.humidity_check) {
            condition.min_humidity = humidity_json.containsKey("min") ? humidity_json["min"].as<float>() : 0.0f;
            condition.max_humidity = humidity_json.containsKey("max") ? humidity_json["max"].as<float>() : 100.0f;
        }
    }
    condition.soil_moisture_check = false;
    if (jsonCondition.containsKey("soil_moisture")) {
        JsonObject soil_json = jsonCondition["soil_moisture"];
        condition.soil_moisture_check = soil_json.containsKey("enabled") ? soil_json["enabled"].as<bool>() : false;
        if (condition.soil_moisture_check) {
            condition.min_soil_moisture = soil_json.containsKey("min") ? soil_json["min"].as<float>() : 30.0f;
        }
    }
    condition.rain_check = false;
    if (jsonCondition.containsKey("rain")) {
        JsonObject rain_json = jsonCondition["rain"];
        condition.rain_check = rain_json.containsKey("enabled") ? rain_json["enabled"].as<bool>() : false;
        if (condition.rain_check) {
            condition.skip_when_raining = rain_json.containsKey("skip_when_raining") ? rain_json["skip_when_raining"].as<bool>() : true;
        }
    }
    condition.light_check = false;
    if (jsonCondition.containsKey("light")) {
        JsonObject light_json = jsonCondition["light"];
        condition.light_check = light_json.containsKey("enabled") ? light_json["enabled"].as<bool>() : false;
        if (condition.light_check) {
            condition.min_light = light_json.containsKey("min") ? light_json["min"].as<int>() : 0;
            condition.max_light = light_json.containsKey("max") ? light_json["max"].as<int>() : 50000;
        }
    }
}

bool TaskScheduler::processDeleteCommand(const char* json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "JSON parsing failed for delete cmd: %s", error.c_str());
        return false;
    }
    if (!doc.containsKey("delete_tasks") || !doc["delete_tasks"].is<JsonArray>()) {
        AppLogger.error("TaskSched", "Delete command missing 'delete_tasks' array.");
        return false;
    }
    JsonArray deleteTasksArray = doc["delete_tasks"];
    bool anyTaskDeleted = false;
    for (JsonVariant taskId_var : deleteTasksArray) {
        if (taskId_var.is<int>()) {
            if (deleteTask(taskId_var.as<int>())) {
                anyTaskDeleted = true;
            }
        } else {
            AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Invalid task ID type in delete_tasks array: %s", taskId_var.as<String>().c_str());
        }
    }
    return anyTaskDeleted;
}

void TaskScheduler::update() {
    unsigned long currentMillis = millis();
    if (currentMillis - _lastCheckTime < 1000) {
        return;
    }
    _lastCheckTime = currentMillis;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        time_t now_time;
        time(&now_time);
        struct tm timeinfo;
        localtime_r(&now_time, &timeinfo);

        bool anyStateChangedThisUpdateCycle = false;

        for (auto& current_task : _tasks) {
            if (current_task.id == 1) { // Log for specific task ID 1
                 AppLogger.logf(LOG_LEVEL_DEBUG, "Task1UpdateChk", "Task 1 - State: %d, is_resuming: %s, start_time: %lu, rem_ms: %lu, now_time: %lu",
                               current_task.state, current_task.is_resuming_from_pause ? "Y":"N", (unsigned long)current_task.start_time, current_task.remaining_duration_on_pause_ms, (unsigned long)now_time);
            }

            bool should_check_task_completion = false;
            time_t target_duration_for_completion_seconds = 0;
            time_t elapsed_time_seconds = 0;

            if (current_task.state == RUNNING) {
                should_check_task_completion = true;
                if (current_task.is_resuming_from_pause) {
                    target_duration_for_completion_seconds = current_task.remaining_duration_on_pause_ms / 1000;
                } else {
                    target_duration_for_completion_seconds = current_task.duration * 60;
                }
                if (current_task.start_time > 0 && now_time >= current_task.start_time) {
                    elapsed_time_seconds = now_time - current_task.start_time;
                } else if (current_task.start_time == 0 && current_task.state == RUNNING) {
                     AppLogger.logf(LOG_LEVEL_WARNING, "TaskSchedChk", "Task %d is RUNNING but start_time is 0!", current_task.id);
                }
            }
            else if (current_task.is_resuming_from_pause && current_task.state != COMPLETED && current_task.state != IDLE) {
                AppLogger.logf(LOG_LEVEL_WARNING, "TaskSchedFix", "Task %d was resuming (is_resuming=true) but state is %d (not RUNNING/COMPLETED/IDLE). Checking completion based on remaining_duration.", current_task.id, current_task.state);
                should_check_task_completion = true;
                target_duration_for_completion_seconds = current_task.remaining_duration_on_pause_ms / 1000;
                if (current_task.start_time > 0 && now_time >= current_task.start_time) {
                    elapsed_time_seconds = now_time - current_task.start_time;
                } else if (current_task.start_time == 0 && current_task.is_resuming_from_pause) {
                    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSchedFix", "Task %d is_resuming_from_pause but start_time is 0!", current_task.id);
                    should_check_task_completion = false;
                }
            }

            if (should_check_task_completion) {
                 AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSchedDbg",
                               "Task %d CompletionCheck: now_time=%lu, start_time=%lu, elapsed=%llds, is_resuming=%s, rem_ms=%lu, target_s=%llds, current_state=%d",
                               current_task.id, (unsigned long)now_time, (unsigned long)current_task.start_time,
                               (long long)elapsed_time_seconds, current_task.is_resuming_from_pause ? "TRUE" : "FALSE",
                               current_task.remaining_duration_on_pause_ms, (long long)target_duration_for_completion_seconds, current_task.state);

                if (current_task.start_time > 0 && elapsed_time_seconds >= target_duration_for_completion_seconds) {
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (resuming: %s, state_at_comp_check: %d) COMPLETED. Elapsed: %llds, Target: %llds. Start: %lu, Now: %lu, RemMS (before stop): %lu",
                                   current_task.id, current_task.is_resuming_from_pause ? "Y":"N", current_task.state,
                                   (long long)elapsed_time_seconds, (long long)target_duration_for_completion_seconds,
                                   (unsigned long)current_task.start_time, (unsigned long)now_time,
                                   current_task.remaining_duration_on_pause_ms);

                    stopTask(current_task);
                    current_task.state = COMPLETED;
                    if (current_task.active) {
                        current_task.next_run = calculateNextRunTime(current_task);
                    } else {
                        current_task.next_run = 0;
                    }
                    anyStateChangedThisUpdateCycle = true;
                }
            }
        }

        for (auto& task_to_evaluate : _tasks) {
            if (!task_to_evaluate.active) continue;
            if (task_to_evaluate.state == RUNNING || task_to_evaluate.state == COMPLETED) continue;

            bool canProceedWithEvaluation = false;
            if (task_to_evaluate.state == IDLE) {
                bool isDayMatch = (task_to_evaluate.days & (1 << timeinfo.tm_wday));
                bool isTimeMatch = (timeinfo.tm_hour == task_to_evaluate.hour && timeinfo.tm_min == task_to_evaluate.minute);
                if ( (task_to_evaluate.next_run != 0 && now_time >= task_to_evaluate.next_run) ||
                     (task_to_evaluate.next_run == 0 && isDayMatch && isTimeMatch) ) {
                     if (isDayMatch && isTimeMatch) canProceedWithEvaluation = true;
                }
            } else if (task_to_evaluate.state == WAITING || task_to_evaluate.state == PAUSED) {
                canProceedWithEvaluation = true;
            }

            if (!canProceedWithEvaluation) continue;

            if (task_to_evaluate.state == IDLE || task_to_evaluate.state == WAITING) {
                if (!checkSensorConditions(task_to_evaluate)) {
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d conditions not met. State: %d -> WAITING. Rescheduling.", task_to_evaluate.id, task_to_evaluate.state);
                    if (task_to_evaluate.state == IDLE) {
                        task_to_evaluate.state = WAITING;
                        anyStateChangedThisUpdateCycle = true;
                    }
                    task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true);
                    continue;
                }
            }

            bool canRunThisTaskDueToZoneAvailability = true;
            std::vector<IrrigationTask*> tasksToPauseForThisAction;
            for (uint8_t zoneId_to_use : task_to_evaluate.zones) {
                IrrigationTask* conflictingTaskPtr = getTaskUsingZone(zoneId_to_use, task_to_evaluate.id);
                if (conflictingTaskPtr) {
                    if (task_to_evaluate.priority > conflictingTaskPtr->priority) {
                        if (conflictingTaskPtr->preemptable && conflictingTaskPtr->state == RUNNING) {
                            bool alreadyMarkedToPause = false;
                            for(const auto* tp : tasksToPauseForThisAction) if(tp->id == conflictingTaskPtr->id) alreadyMarkedToPause = true;
                            if(!alreadyMarkedToPause) tasksToPauseForThisAction.push_back(conflictingTaskPtr);
                        } else if (!conflictingTaskPtr->preemptable || conflictingTaskPtr->state == PAUSED) {
                            canRunThisTaskDueToZoneAvailability = false;
                            if (task_to_evaluate.state != WAITING) {
                                task_to_evaluate.state = WAITING;
                                task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true);
                                anyStateChangedThisUpdateCycle = true;
                            }
                            AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (state %d, prio %d) cannot start/resume, zone %u busy with non-preemptable/paused task %d (prio %d). Setting/keeping WAITING.",
                                           task_to_evaluate.id, task_to_evaluate.state, task_to_evaluate.priority, zoneId_to_use, conflictingTaskPtr->id, conflictingTaskPtr->priority);
                            break;
                        }
                    } else {
                        canRunThisTaskDueToZoneAvailability = false;
                        if (task_to_evaluate.state != WAITING) {
                            task_to_evaluate.state = WAITING;
                            task_to_evaluate.next_run = calculateNextRunTime(task_to_evaluate, true);
                            anyStateChangedThisUpdateCycle = true;
                        }
                        AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d (state %d, prio %d) cannot start/resume, zone %u busy with higher/equal prio task %d (prio %d). Setting/keeping WAITING.",
                                       task_to_evaluate.id, task_to_evaluate.state, task_to_evaluate.priority, zoneId_to_use, conflictingTaskPtr->id, conflictingTaskPtr->priority);
                        break;
                    }
                }
            }

            if (canRunThisTaskDueToZoneAvailability) {
                for (IrrigationTask* taskToActuallyPause : tasksToPauseForThisAction) {
                    if (taskToActuallyPause->state == RUNNING) {
                       pauseTask(*taskToActuallyPause);
                       AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d PAUSED by higher priority task %d.", taskToActuallyPause->id, task_to_evaluate.id);
                       anyStateChangedThisUpdateCycle = true;
                    }
                }
                TaskState state_before_action = task_to_evaluate.state;
                if (task_to_evaluate.state == PAUSED) {
                    resumeTask(task_to_evaluate);
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d RESUMED.", task_to_evaluate.id);
                } else {
                    startTask(task_to_evaluate);
                    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d STARTED (from %s).", task_to_evaluate.id, (state_before_action == IDLE ? "IDLE" : "WAITING"));
                }
                if (task_to_evaluate.state != state_before_action) {
                    anyStateChangedThisUpdateCycle = true;
                }
            }
        }

        if (anyStateChangedThisUpdateCycle) {
            _scheduleStatusChanged = true;
        }
        recomputeEarliestNextCheckTime();
        xSemaphoreGive(_mutex);
    } else {
        AppLogger.error("TaskSched", "Failed to take mutex in update()");
    }
}

bool TaskScheduler::checkSensorConditions(const IrrigationTask& task) {
    if (!task.sensor_condition.enabled) return true;
    const SensorCondition& condition = task.sensor_condition;
    if (condition.temperature_check) {
        float temp = _envManager.getTemperature();
        if (temp < condition.min_temperature || temp > condition.max_temperature) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSensCond", "Task %d skipped: Temp (%.1f) out of range [%.1f, %.1f]", task.id, temp, condition.min_temperature, condition.max_temperature);
            return false;
        }
    }
    if (condition.humidity_check) {
        float humidity = _envManager.getHumidity();
        if (humidity < condition.min_humidity || humidity > condition.max_humidity) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSensCond", "Task %d skipped: Humidity (%.1f) out of range [%.1f, %.1f]", task.id, humidity, condition.min_humidity, condition.max_humidity);
            return false;
        }
    }
    if (condition.soil_moisture_check) {
        for (uint8_t zoneId : task.zones) {
            float moisture = _envManager.getSoilMoisture(zoneId);
            if (moisture > condition.min_soil_moisture) {
                AppLogger.logf(LOG_LEVEL_INFO, "TaskSensCond", "Task %d skipped: Zone %d SoilM (%.1f) > min_needed (%.1f)", task.id, zoneId, moisture, condition.min_soil_moisture);
                return false;
            }
        }
    }
    if (condition.rain_check && condition.skip_when_raining) {
        if (_envManager.isRaining()) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSensCond", "Task %d skipped: Raining", task.id);
            return false;
        }
    }
    if (condition.light_check) {
        int light = _envManager.getLightLevel();
        if (light < condition.min_light || light > condition.max_light) {
            AppLogger.logf(LOG_LEVEL_INFO, "TaskSensCond", "Task %d skipped: Light (%d) out of range [%d, %d]", task.id, light, condition.min_light, condition.max_light);
            return false;
        }
    }
    return true;
}

void TaskScheduler::startTask(IrrigationTask& task) {
    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Attempting to start task %d. Duration: %u mins.", task.id, task.duration);
    task.start_time = time(NULL);
    task.is_resuming_from_pause = false;
    task.remaining_duration_on_pause_ms = 0;
    uint32_t duration_ms = task.duration * 60 * 1000;
    String zonesStr = "";
    if (!task.zones.empty()) {
        for (size_t i = 0; i < task.zones.size(); ++i) {
            zonesStr += String(task.zones[i]);
            if (i < task.zones.size() - 1) zonesStr += ", ";
        }
    } else {
        zonesStr = "None";
    }
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d starting with zones: [%s]", task.id, zonesStr.c_str());
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= (uint8_t)_relayManager.getNumRelays()) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOn(relayIndex, duration_ms);
            _activeZonesBits.set(relayIndex);
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Invalid zoneId %u in startTask for task %d", zoneId, task.id);
        }
    }
    task.state = RUNNING;
    _scheduleStatusChanged = true;
}

void TaskScheduler::stopTask(IrrigationTask& task) {
    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Stopping task %d (current state %d). Clearing active zones.", task.id, task.state);
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= (uint8_t)_relayManager.getNumRelays()) {
            uint8_t relayIndex = zoneId - 1;
            _relayManager.turnOff(relayIndex);
            _activeZonesBits.reset(relayIndex);
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Invalid zoneId %u in stopTask for task %d", zoneId, task.id);
        }
    }
    task.is_resuming_from_pause = false;
    task.remaining_duration_on_pause_ms = 0;
    _scheduleStatusChanged = true;
}

void TaskScheduler::pauseTask(IrrigationTask& task) {
    if (task.state != RUNNING) {
        AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Attempted to pause task %d but it was not RUNNING (state: %d).", task.id, task.state);
        return;
    }
    time_t current_time_for_pause = time(NULL);
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSchedDbg", "PauseTask ID %d: current_time_for_pause=%lu, task.start_time=%lu, task.is_resuming_from_pause_before_calc=%s",
                   task.id, (unsigned long)current_time_for_pause, (unsigned long)task.start_time, task.is_resuming_from_pause ? "Y":"N");
    if (task.start_time == 0) {
        AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Task %d is RUNNING but start_time is 0. Cannot accurately calculate remaining time for pause.", task.id);
        task.remaining_duration_on_pause_ms = 0;
    } else {
        time_t elapsed_seconds_before_pause = current_time_for_pause - task.start_time;
        uint32_t original_duration_seconds_this_segment;
        if (task.is_resuming_from_pause) {
            original_duration_seconds_this_segment = task.remaining_duration_on_pause_ms / 1000;
        } else {
            original_duration_seconds_this_segment = task.duration * 60;
        }
        if (elapsed_seconds_before_pause < 0) elapsed_seconds_before_pause = 0;
        int32_t remaining_sec = original_duration_seconds_this_segment - elapsed_seconds_before_pause;
        task.remaining_duration_on_pause_ms = (remaining_sec > 0) ? (unsigned long)remaining_sec * 1000 : 0;
        AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Pausing task %d. Original duration for this segment: %u s. Elapsed (calc): %ld s. Stored Remaining MS: %lu ms.",
                    task.id, original_duration_seconds_this_segment, (long)elapsed_seconds_before_pause, task.remaining_duration_on_pause_ms);
    }
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= (uint8_t)_relayManager.getNumRelays()) {
            _relayManager.turnOff(zoneId - 1);
        } else {
             AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Invalid zoneId %u in pauseTask for task %d", zoneId, task.id);
        }
    }
    task.state = PAUSED;
    task.is_resuming_from_pause = false;
    _scheduleStatusChanged = true;
}

void TaskScheduler::resumeTask(IrrigationTask& task) {
    if (task.state != PAUSED) {
         AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Attempted to resume task %d but it was not PAUSED (state: %d).", task.id, task.state);
        return;
    }
    if (task.remaining_duration_on_pause_ms == 0) {
        AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Task %d was PAUSED but has no remaining duration. Marking COMPLETED.", task.id);
        stopTask(task);
        task.state = COMPLETED;
        if (task.active) {
            task.next_run = calculateNextRunTime(task);
        } else {
            task.next_run = 0;
        }
        _scheduleStatusChanged = true;
        return;
    }
    AppLogger.logf(LOG_LEVEL_INFO, "TaskSched", "Resuming task %d with %lu ms remaining.", task.id, task.remaining_duration_on_pause_ms);
    task.start_time = time(NULL);
    task.is_resuming_from_pause = true;
    for (uint8_t zoneId : task.zones) {
        if (zoneId >= 1 && zoneId <= (uint8_t)_relayManager.getNumRelays()) {
            _relayManager.turnOn(zoneId - 1, task.remaining_duration_on_pause_ms);
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Invalid zoneId %u in resumeTask for task %d", zoneId, task.id);
        }
    }
    task.state = RUNNING;
    _scheduleStatusChanged = true;
}

bool TaskScheduler::isZoneBusy(uint8_t zoneId) {
    if (zoneId >= 1 && zoneId <= (uint8_t)_relayManager.getNumRelays()) {
        return _activeZonesBits.test(zoneId - 1);
    }
    AppLogger.logf(LOG_LEVEL_ERROR, "TaskSched", "Invalid zoneId %u in isZoneBusy", zoneId);
    return true;
}

bool TaskScheduler::isHigherPriority(int checkingTaskId, uint8_t conflictingZoneId) {
    // This function needs careful review if used. Currently, preemption logic is within update().
    return true; // Placeholder
}

time_t TaskScheduler::calculateNextRunTime(IrrigationTask& task, bool isRescheduleAfterSkip) {
    time_t current_epoch_time;
    time(&current_epoch_time);
    AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "calculateNextRunTime for Task ID: %d (State: %d, Active: %s), Days: 0x%02X, Time: %02d:%02d, Skip: %s, Current Epoch: %lu",
                    task.id, task.state, task.active ? "T" : "F", task.days, task.hour, task.minute, isRescheduleAfterSkip ? "true" : "false", (unsigned long)current_epoch_time);

    if (!task.active) {
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d is inactive. Setting next_run to 0.", task.id);
        return 0;
    }
    if (task.days == 0 && task.state != COMPLETED) {
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d has no scheduled days (days bitmap is 0) and is not COMPLETED. Setting next_run to 0.", task.id);
        return 0;
    }
     if (task.days == 0 && task.state == COMPLETED) { // One-shot task that just completed
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d is a one-shot task that completed. No next run.", task.id);
        return 0;
    }

    struct tm timeinfo_current_epoch;
    localtime_r(&current_epoch_time, &timeinfo_current_epoch);

    for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
        struct tm nextDayCandidate_tm;
        time_t temp_time = current_epoch_time + (dayOffset * 24L * 60L * 60L);
        localtime_r(&temp_time, &nextDayCandidate_tm);
        nextDayCandidate_tm.tm_hour = task.hour;
        nextDayCandidate_tm.tm_min = task.minute;
        nextDayCandidate_tm.tm_sec = 0;
        time_t candidate_run_time = mktime(&nextDayCandidate_tm);

        if (task.days & (1 << nextDayCandidate_tm.tm_wday)) {
            if (dayOffset == 0 && !isRescheduleAfterSkip && candidate_run_time < current_epoch_time) {
                 AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Today's (%s) time %02d:%02d has passed. Checking next valid day.", task.id, dayNames[nextDayCandidate_tm.tm_wday], task.hour, task.minute);
                continue;
            }
            if (isRescheduleAfterSkip && candidate_run_time <= current_epoch_time) {
                 AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Rescheduled time %lu for day %s is still past/present (%lu). Continuing.", task.id, (unsigned long)candidate_run_time, dayNames[nextDayCandidate_tm.tm_wday], (unsigned long)current_epoch_time);
                continue;
            }
            AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Task %d: Found next run on %s at %02d:%02d. Epoch: %lu", task.id, dayNames[nextDayCandidate_tm.tm_wday], nextDayCandidate_tm.tm_hour, nextDayCandidate_tm.tm_min, (unsigned long)candidate_run_time);
            return candidate_run_time;
        }
    }
    AppLogger.logf(LOG_LEVEL_WARNING, "TaskSched", "Task %d: calculateNextRunTime could not find any valid future run day. Setting next_run to 0.", task.id);
    return 0;
}

uint8_t TaskScheduler::daysArrayToBitmap(JsonArray daysArray) {
    uint8_t bitmap = 0;
    for (JsonVariant dayVar : daysArray) {
        if (dayVar.is<int>()) {
            int day = dayVar.as<int>();
            if (day >= 1 && day <= 7) {
                int wday_idx = (day == 7) ? 0 : day;
                bitmap |= (1 << wday_idx);
            }
        }
    }
    return bitmap;
}

JsonArray TaskScheduler::bitmapToDaysArray(JsonDocument& doc, uint8_t daysBitmap) {
    JsonArray array = doc.createNestedArray();
    for (int wday_idx = 0; wday_idx <= 6; wday_idx++) {
        if (daysBitmap & (1 << wday_idx)) {
            int userDay = (wday_idx == 0) ? 7 : wday_idx;
            array.add(userDay);
        }
    }
    return array;
}

time_t TaskScheduler::getEarliestNextCheckTime() const {
    return _earliestNextCheckTime;
}

void TaskScheduler::recomputeEarliestNextCheckTime() {
    _earliestNextCheckTime = 0;
    time_t current_time_for_recompute;
    time(&current_time_for_recompute);

    for (const auto& task_item : _tasks) {
        if (task_item.active && task_item.next_run > 0 && task_item.next_run > current_time_for_recompute) {
            if (_earliestNextCheckTime == 0 || task_item.next_run < _earliestNextCheckTime) {
                _earliestNextCheckTime = task_item.next_run;
            }
        }
    }

    if (_earliestNextCheckTime == 0) {
        _earliestNextCheckTime = current_time_for_recompute + 60; // Default to check in 1 minute
        AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "No active future tasks or no tasks. Setting earliestNextCheckTime to 1 min from now: %lu", (unsigned long)_earliestNextCheckTime);
    } else {
         AppLogger.logf(LOG_LEVEL_DEBUG, "TaskSched", "Recomputed earliestNextCheckTime: %lu", (unsigned long)_earliestNextCheckTime);
    }
}

bool TaskScheduler::hasScheduleStatusChangedAndReset() {
    bool changed = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        changed = _scheduleStatusChanged;
        _scheduleStatusChanged = false;
        xSemaphoreGive(_mutex);
    } else {
        // AppLogger.warning("TaskSched", "Failed to take mutex in hasScheduleStatusChangedAndReset, returning true as fallback.");
        return true; // Fallback if mutex fails, to ensure status might be sent
    }
    return changed;
}

IrrigationTask* TaskScheduler::getTaskUsingZone(uint8_t zoneId, int excludeTaskId) {
    for (auto& task_item : _tasks) {
        if (task_item.id == excludeTaskId) continue;
        if (task_item.state == RUNNING || task_item.state == PAUSED) {
            for (uint8_t z : task_item.zones) {
                if (z == zoneId) {
                    return &task_item;
                }
            }
        }
    }
    return nullptr;
}