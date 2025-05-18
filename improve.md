## This document outlines the steps to enhance your ESP32 project's logging system. The primary goals are to:1) Add more contextual information to your logs (CPU core ID and free heap memory).

2) Introduce a structured way to log performance metrics.

3) Ensure all relevant information is captured for statistics, performance evaluation, and easier debugging, for both Serial and MQTT outputs.We will modify the existing `Logger.cpp` and demonstrate how to use the new features in other parts of your project.

### Step 1: Enhance JSON Log Output in `Logger.cpp`

The current `formatToJson` function in your `Logger.cpp` is a good starting point. We will add `core_id` and `free_heap` to the JSON payload.**File: `src/Logger.cpp`**Modify the `formatToJson` function as follows:    // Function to convert LogEntry to JSON string for MQTT
    String Logger::formatToJson(const LogEntry& entry) {
        StaticJsonDocument<512> doc; // JSON size, adjust if more fields are needed
        doc["timestamp"] = entry.timestamp;
        doc["level_num"] = static_cast<int>(entry.level); // Send numeric level
        doc["level_str"] = levelToString(entry.level);   // and string level for readability
        doc["tag"] = entry.tag;
        doc["message"] = entry.message;

        // --- NEW: Add Core ID and Free Heap ---
        doc["core_id"] = xPortGetCoreID();
        doc["free_heap"] = ESP.getFreeHeap();
        // --- END NEW ---

        String output;
        serializeJson(doc, output); // Convert JSON object to string
        return output;
    }**Explanation of Changes:*** `doc["core_id"] = xPortGetCoreID();`: This line adds the ID of the CPU core that executed the logging call. This is very useful for debugging multi-core issues.

* `doc["free_heap"] = ESP.getFreeHeap();`: This line adds the amount of free heap memory at the time of logging. Tracking this can help identify memory leaks or understand memory usage patterns.
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### Step 2: Introduce Structured Performance Logging

## To log performance metrics in a structured way, we can add a new dedicated function to the `Logger` class. This function will take specific performance-related parameters.

#### 2.1. Add a new method declaration in `Logger.h`

**File: `include/Logger.h`**Add the following declaration within the `public` section of the `Logger` class:    class Logger {
    public:
        // ... (existing methods) ...

        // Logging function with printf-style formatting
        void logf(LogLevel level, const String& tag, const char* format, ...);

        // --- NEW: Performance Logging Function ---
        // Logs an event with a specific duration and optional additional metrics.
        // eventName: A descriptive name for the event being measured (e.g., "SensorRead", "TaskXExecution").
        // durationMs: The duration of the event in milliseconds.
        // success: Optional boolean to indicate if the operation was successful.
        // details: Optional String for any additional relevant details or metrics.
        void perf(const String& tag, const String& eventName, unsigned long durationMs, bool success = true, const String& details = "");
        // --- END NEW ---


        // Configure log levels dynamically
        // ... (rest of the class) ...
    };
------

#### 2.2. Implement the new method in `Logger.cpp`

**File: `src/Logger.cpp`**Add the implementation for the `perf` function:    // ... (existing implementations) ...

    // --- NEW: Performance Logging Function Implementation ---
    void Logger::perf(const String& tag, const String& eventName, unsigned long durationMs, bool success, const String& details) {
        // Performance logs are typically INFO or DEBUG level. Let's use INFO.
        // You can make the level a parameter if more flexibility is needed.
        LogLevel level = LOG_LEVEL_INFO;

        bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
        bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

        if (!shouldLogSerial && !shouldLogMqtt) {
            return; // No target wants to log at this level
        }

        // Create the main message part
        String perfMessage = "PERF: Event='" + eventName + "', Duration=" + String(durationMs) + "ms, Success=" + (success ? "true" : "false");
        if (!details.isEmpty()) {
            perfMessage += ", Details='" + details + "'";
        }

        // For Serial, just print the composed message
        if (shouldLogSerial && Serial) {
            LogEntry entry;
            if (time(nullptr) > 1000000000L) {
                entry.timestamp = time(nullptr);
            } else {
                entry.timestamp = millis();
            }
            entry.level = level;
            entry.tag = tag; // Use the provided tag
            entry.message = perfMessage;

            // Directly format for serial to include core_id and free_heap contextually for this specific log
            String logString = String(entry.timestamp) + " [" + levelToString(entry.level) + "]";
            logString += " [" + entry.tag + "]";
            logString += " [Core:" + String(xPortGetCoreID()) + ", Heap:" + String(ESP.getFreeHeap()) + "]"; // Add context here too
            logString += ": " + entry.message;
            Serial.println(logString);
        }

        // For MQTT, create a structured JSON payload
        if (shouldLogMqtt && _networkManager && _networkManager->isConnected()) {
            StaticJsonDocument<512> doc; // Adjust size as needed
            if (time(nullptr) > 1000000000L) {
                doc["timestamp"] = time(nullptr);
            } else {
                doc["timestamp"] = millis();
            }
            doc["level_num"] = static_cast<int>(level);
            doc["level_str"] = levelToString(level); // Or a dedicated "PERF" level string
            doc["tag"] = tag;
            // Differentiate performance logs, e.g., by adding a "type" field or specific structure
            doc["type"] = "performance";
            doc["event_name"] = eventName;
            doc["duration_ms"] = durationMs;
            doc["success"] = success;
            if (!details.isEmpty()) {
                doc["details"] = details;
            }
            doc["core_id"] = xPortGetCoreID();
            doc["free_heap"] = ESP.getFreeHeap();

            String mqttPayload;
            serializeJson(doc, mqttPayload);
            _networkManager->publish(_mqttLogTopic, mqttPayload.c_str()); // Assuming _mqttLogTopic is your general log topic
                                                                        // Consider a dedicated topic for performance logs if volume is high.
        }
    }
    // --- END NEW ---

    // Functions to set and get log levels
    // ... (rest of the file) ...**Explanation of `perf` function:*** It takes a `tag`, `eventName`, `durationMs`, an optional `success` flag, and optional `details`.

* It logs to Serial with a clear prefix and includes Core ID and Free Heap.

* For MQTT, it creates a structured JSON object specifically for performance data, including a `"type": "performance"` field to easily filter these logs on your backend.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### Step 3: Using the Enhanced Logger

## Now, let's see how to use these enhancements.

#### 3.1. Automatic `core_id` and `free_heap`

## No changes are needed in your existing `AppLogger.info()`, `AppLogger.debug()`, etc., calls. The `core_id` and `free_heap` will be automatically added to the JSON payload sent via MQTT due to the changes in `formatToJson`.

#### 3.2. Logging Performance Metrics

Identify operations in your code where performance is critical or you want to monitor execution time.**Example: Logging Sensor Reading Duration in `Core0TaskCode` (inside `src/main.cpp`)**    // Inside Core0TaskCode in src/main.cpp

    // ... (other code) ...
    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
        lastSensorReadTime = currentTime;

        if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
            // --- NEW: Performance Logging Start ---
            unsigned long sensorReadStartTime = millis();
            bool readSuccess = false;
            // --- END NEW ---

            if (sensorManager.readSensors()) { // sensorManager.readSensors() should log errors internally
                readSuccess = true; // Mark as success
                envManager.setCurrentTemperature(sensorManager.getTemperature());
                envManager.setCurrentHumidity(sensorManager.getHumidity());
                envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());

                AppLogger.debug("Core0", "Sensors read: T=" + String(sensorManager.getTemperature()) +
                                       "C, H=" + String(sensorManager.getHumidity()) +
                                       "%, HI=" + String(sensorManager.getHeatIndex()) + "C");

                if (networkManager.isConnected()) {
                    String payload = sensorManager.getJsonPayload(API_KEY);
                    // --- NEW: Performance Logging for MQTT Publish Start (Optional but good) ---
                    unsigned long mqttPublishStartTime = millis();
                    bool mqttSuccess = networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
                    unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
                    AppLogger.perf("Core0", "MQTTSensorDataPublish", mqttPublishDuration, mqttSuccess);
                    // --- END NEW ---
                    // AppLogger.debug("Core0", "Sensor data published to MQTT"); // Can be covered by perf log
                } else {
                    AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT.");
                    RGB_Light(255, 0, 0); delay(100); RGB_Light(0, 0, 0);
                }
            } else {
                // sensorManager.readSensors() already logged the error
                readSuccess = false; // Mark as failure
            }

            // --- NEW: Performance Logging End ---
            unsigned long sensorReadDuration = millis() - sensorReadStartTime;
            AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);
            // --- END NEW ---

            xSemaphoreGive(sensorDataMutex);
        }
    }
    // ... (rest of Core0TaskCode) ...**Explanation of Performance Logging Example:**1) We record `millis()` before starting the `sensorManager.readSensors()` operation.

2) We record `millis()` again after the operation completes.

3) The difference gives us `sensorReadDuration`.

4) We call `AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);` to log this metric.

   - `"Core0"` is the tag.

   - `"SensorReadOperation"` is the event name.

   - `sensorReadDuration` is the measured time.

   - `readSuccess` indicates if the sensor reading was successful.

5) A similar pattern is shown for logging the duration of the MQTT publish operation.You can apply this pattern to other critical sections:* `TaskScheduler::update()` execution time.

* `RelayManager::processCommand()` execution time.

* Network connection attempts.

* JSON parsing operations.
--------------------------

### Step 4: Systematically Replace Old `Serial.print` Calls

Your `logs.md` file already provides an excellent guide for this. The key is to:1) **Include `Logger.h`**: In every `.cpp` file where logging is needed.

2) **Use `AppLogger`**: Replace all `Serial.print()`, `Serial.println()`, and `Serial.printf()` with appropriate `AppLogger` calls (`.debug()`, `.info()`, `.warning()`, `.error()`, `.critical()`, or `.logf()` for formatted output).

3) **Assign Tags**: Use meaningful tags for each module (e.g., `"RelayMgr"`, `"TaskSched"`, `"NetMgr"`, `"SensorMgr"`, `"EnvMgr"`, `"Core0"`, `"Core1"`, `"MQTTCallbk"`).

4) **Choose Correct Log Levels**:

   - `LOG_LEVEL_DEBUG`: For detailed developer-specific information, variable states, minor steps.

   - `LOG_LEVEL_INFO`: For normal operational messages, major system events, successful operations.

   - `LOG_LEVEL_WARNING`: For potential issues, unexpected but recoverable situations, or minor failures.

   - `LOG_LEVEL_ERROR`: For functional errors where a specific operation failed.

   - `LOG_LEVEL_CRITICAL`: For severe errors that might lead to system instability or complete malfunction.

5) **Remove Redundant Logging**: If a lower-level function already logs an error, the calling function might not need to log the same error again, or it can log it at a higher level with more context.**Example (from your `src/EnvironmentManager.cpp`):** Currently, you have direct `Serial.println` calls in setters:    // In src/EnvironmentManager.cpp
    // ...
    void EnvironmentManager::setSoilMoisture(int zone, float value) {
        if (zone >= 1 && zone <= 6) {
            _soilMoisture[zone] = value;
            // OLD: Serial.println("Set soil moisture for zone " + String(zone) + " to " + String(value) + "%");
            // NEW:
            AppLogger.info("EnvMgr", "Set soil moisture for zone " + String(zone) + " to " + String(value) + "%");
        }
    }

    void EnvironmentManager::setRainStatus(bool isRaining) {
        _isRaining = isRaining;
        // OLD: Serial.println("Set rain status to " + String(isRaining ? "raining" : "not raining"));
        // NEW:
        AppLogger.info("EnvMgr", "Set rain status to " + String(isRaining ? "raining" : "not raining"));
    }

    void EnvironmentManager::setLightLevel(int level) {
        _lightLevel = level;
        // OLD: Serial.println("Set light level to " + String(level) + " lux");
        // NEW:
        AppLogger.info("EnvMgr", "Set light level to " + String(level) + " lux");
    }Make sure to include `../include/Logger.h` at the top of `EnvironmentManager.cpp`.
---------------------------------------------------------------------------------------

### Step 5: Compile, Test, and Iterate

## 1. **Compile**: After making the changes, recompile your project using PlatformIO.

2. **Test Serial Output**: Check the Serial Monitor. You should see:

   - Standard logs with the format: `[TIMESTAMP] [LEVEL] [TAG]: Message`.

   - Performance logs with the format: `[TIMESTAMP] [INFO] [TAG] [Core:X, Heap:Y]: PERF: Event='EventName', Duration=Zms, Success=true/false, Details='...'`.

3. **Test MQTT Output**: Use an MQTT client (like MQTT Explorer) to subscribe to `irrigation/esp32_6relay/logs`.

   - Standard logs should now include `core_id` and `free_heap`.

   - Performance logs should appear as structured JSON with the `type: "performance"` field and other performance-specific data.

4. **Verify Log Levels**: Test changing log levels dynamically via the `irrigation/esp32_6relay/logconfig` MQTT topic to ensure filtering works as expected for both Serial and MQTT.

5. **Iterate**: Analyze the logs. Are they providing the insights you need? Adjust log messages, levels, tags, and add more performance logging points as necessary. For example, if MQTT logs for performance become too verbose, you might consider:

   - Sending them to a different MQTT topic (e.g., `irrigation/esp32_6relay/logs/perf`).

   - Making the log level for `AppLogger.perf` configurable or defaulting it to `LOG_LEVEL_DEBUG`.By following these steps, your logging system will be significantly more powerful, providing valuable data for performance analysis, debugging, and overall system monitoring.
