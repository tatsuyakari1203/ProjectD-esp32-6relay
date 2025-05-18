## Guide to Implementing an Advanced Logging System and Removing Old `Serial.print` Calls

This document provides detailed steps to implement a new, more robust logging system for your ESP32-S3 6-Relay project, while also removing old `Serial.print` and `Serial.println` commands.


### 1. Implementation Goals

- **Build a Central `Logger` Class**: Create a `Logger` class to manage logging with multiple levels (DEBUG, INFO, WARNING, ERROR, CRITICAL) and support output to Serial and MQTT.

- **Integrate `Logger` into the System**: Replace existing `Serial.print`/`println` commands throughout the project with calls to `AppLogger` functions.

- **Structured and Contextual Logging**: Ensure each log message includes a timestamp, level, tag (identifying the module/task), and a clear message.

- **Configurable Logging**: Allow flexible adjustment of log levels for Serial and MQTT.


### 2. Code Implementation Steps

#### Step 2.1: Create the `Logger` Class

**1. Create `Logger.h` file (in the `include` directory)**

This file defines the interface for the `Logger` class, log levels, and the `LogEntry` structure.

    #ifndef LOGGER_H
    #define LOGGER_H

    #include <Arduino.h>
    #include <ArduinoJson.h>
    #include "NetworkManager.h" // Required for the Logger to send logs via MQTT

    // Define log levels
    enum LogLevel {
        LOG_LEVEL_NONE = 0,    // Disable logging entirely
        LOG_LEVEL_CRITICAL = 1, // Critical errors, system might crash or malfunction completely
        LOG_LEVEL_ERROR = 2,   // Errors, a specific function is not working correctly
        LOG_LEVEL_WARNING = 3, // Warnings, potential issues or unexpected situations
        LOG_LEVEL_INFO = 4,    // Information about normal operations, main system steps
        LOG_LEVEL_DEBUG = 5    // Detailed information for debugging (e.g., variable values, minor steps)
    };

    // Structure to store a single log record
    struct LogEntry {
        unsigned long timestamp; // Log timestamp (Unix time or millis() if NTP not synced)
        LogLevel level;          // Log level
        String tag;              // Tag to identify the log source (e.g., "RelayMgr", "Core0Task")
        String message;          // Log message content
        // Other fields can be added if needed: coreId, freeHeap, etc.
    };

    class Logger {
    public:
        Logger();

        // Initialize the Logger
        // networkManager: pointer to the NetworkManager object for sending MQTT logs
        // initialSerialLogLevel: Initial log level for Serial output
        // initialMqttLogLevel: Initial log level for MQTT output
        void begin(NetworkManager* networkManager, LogLevel initialSerialLogLevel = LOG_LEVEL_INFO, LogLevel initialMqttLogLevel = LOG_LEVEL_WARNING);

        // Utility functions for logging at specific levels
        void critical(const String& tag, const String& message);
        void error(const String& tag, const String& message);
        void warning(const String& tag, const String& message);
        void info(const String& tag, const String& message);
        void debug(const String& tag, const String& message);

        // General logging function, more flexible
        void log(LogLevel level, const String& tag, const String& message);
        // Logging function with printf-style formatting
        void logf(LogLevel level, const String& tag, const char* format, ...);

        // Configure log levels dynamically
        void setSerialLogLevel(LogLevel level);
        void setMqttLogLevel(LogLevel level);
        LogLevel getSerialLogLevel() const;
        LogLevel getMqttLogLevel() const;

    private:
        NetworkManager* _networkManager; // Pointer to use NetworkManager for MQTT publishing
        LogLevel _serialLogLevel;        // Current log level for Serial
        LogLevel _mqttLogLevel;          // Current log level for MQTT

        const char* _mqttLogTopic = "irrigation/esp32_6relay/logs"; // MQTT topic for logs

        // Internal functions for formatting and outputting logs
        void processLogEntry(const LogEntry& entry);
        // Function to format LogEntry into a JSON string
        String formatToJson(const LogEntry& entry);
        // Function to convert LogLevel enum to string
        String levelToString(LogLevel level);
    };

    // Declare a global AppLogger variable for easy access from anywhere in the code
    extern Logger AppLogger;

    #endif // LOGGER_H

**2. Create `Logger.cpp` file (in the `src` directory)**

This file implements the detailed functions of the `Logger` class.

    #include "../include/Logger.h" // Path to Logger.h
    #include <time.h>               // To use time() for Unix timestamp
    #include <stdarg.h>             // To use va_list, va_start, va_end for logf function

    // Define the global AppLogger variable
    Logger AppLogger;

    Logger::Logger() : _networkManager(nullptr), _serialLogLevel(LOG_LEVEL_NONE), _mqttLogLevel(LOG_LEVEL_NONE) {
        // Default constructor
    }

    void Logger::begin(NetworkManager* networkManager, LogLevel initialSerialLogLevel, LogLevel initialMqttLogLevel) {
        _networkManager = networkManager;
        setSerialLogLevel(initialSerialLogLevel); // Use setter to get a log message for the change
        setMqttLogLevel(initialMqttLogLevel);   // Use setter

        // Log Logger initialization message (will only appear if Serial is configured for INFO level or higher)
        // and Serial.begin() has been called.
        if (_serialLogLevel >= LOG_LEVEL_INFO && Serial) {
             Serial.println(String(millis()) + " [INFO] [Logger]: Logger initialized. Serial LogLevel: " + levelToString(_serialLogLevel) + ", MQTT LogLevel: " + levelToString(_mqttLogLevel));
             if (_mqttLogLevel > LOG_LEVEL_NONE) {
                if (_networkManager && _networkManager->isConnected()) {
                    Serial.println(String(millis()) + " [INFO] [Logger]: MQTT logging active.");
                } else {
                    Serial.println(String(millis()) + " [WARNING] [Logger]: MQTT logging configured, but NetworkManager not available or not connected at init.");
                }
             }
        }
    }

    // Specific level logging functions
    void Logger::critical(const String& tag, const String& message) {
        log(LOG_LEVEL_CRITICAL, tag, message);
    }

    void Logger::error(const String& tag, const String& message) {
        log(LOG_LEVEL_ERROR, tag, message);
    }

    void Logger::warning(const String& tag, const String& message) {
        log(LOG_LEVEL_WARNING, tag, message);
    }

    void Logger::info(const String& tag, const String& message) {
        log(LOG_LEVEL_INFO, tag, message);
    }

    void Logger::debug(const String& tag, const String& message) {
        log(LOG_LEVEL_DEBUG, tag, message);
    }

    // Main log function, handles LogEntry creation and calls processLogEntry
    void Logger::log(LogLevel level, const String& tag, const String& message) {
        // Only process if the message's log level is important enough to be recorded
        // by at least one target (Serial or MQTT)
        bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
        bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

        if (!shouldLogSerial && !shouldLogMqtt) {
            return; // No target wants to log at this level
        }

        LogEntry entry;
        // Prefer Unix time if NTP is synced, otherwise use millis()
        if (time(nullptr) > 1000000000L) { // Basic check if NTP has run
            entry.timestamp = time(nullptr);
        } else {
            entry.timestamp = millis(); // Use millis() as a fallback
        }
        entry.level = level;
        entry.tag = tag;
        entry.message = message;

        processLogEntry(entry); // Send the entry to the common processing function
    }

    // Log function with printf-style formatting
    void Logger::logf(LogLevel level, const String& tag, const char* format, ...) {
        // Similar to log function, check level first
        bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
        bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

        if (!shouldLogSerial && !shouldLogMqtt) {
            return;
        }

        char buffer[256]; // Buffer for the formatted string, be careful with size
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args); // Format the string
        va_end(args);

        log(level, tag, String(buffer)); // Call the main log function with the formatted string
    }

    // Internal function to handle sending logs to destinations (Serial, MQTT)
    void Logger::processLogEntry(const LogEntry& entry) {
        // 1. Log to Serial
        if (entry.level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE && Serial) { // Check if Serial is ready
            String logString = String(entry.timestamp) + " [" + levelToString(entry.level) + "]";
            if (!entry.tag.isEmpty()) {
                logString += " [" + entry.tag + "]";
            }
            logString += ": " + entry.message;
            Serial.println(logString); // Send to Serial Monitor
        }

        // 2. Log via MQTT
        if (entry.level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE) {
            if (_networkManager && _networkManager->isConnected()) {
                String jsonPayload = formatToJson(entry);
                // NetworkManager::publish should be designed to not block for too long
                // or have a retry/queue mechanism if high reliability for MQTT logs is needed.
                _networkManager->publish(_mqttLogTopic, jsonPayload.c_str());
            }
            // No else here to avoid loop logging when MQTT is disconnected
        }
    }

    // Function to convert LogEntry to JSON string for MQTT
    String Logger::formatToJson(const LogEntry& entry) {
        StaticJsonDocument<512> doc; // JSON size, adjust if more fields are needed
        doc["timestamp"] = entry.timestamp;
        doc["level_num"] = static_cast<int>(entry.level); // Send numeric level
        doc["level_str"] = levelToString(entry.level);   // and string level for readability
        doc["tag"] = entry.tag;
        doc["message"] = entry.message;
        // Other useful information can be added:
        // doc["core_id"] = xPortGetCoreID();
        // doc["free_heap"] = ESP.getFreeHeap();

        String output;
        serializeJson(doc, output); // Convert JSON object to string
        return output;
    }

    // Function to convert LogLevel enum to string for display
    String Logger::levelToString(LogLevel level) {
        switch (level) {
            case LOG_LEVEL_CRITICAL: return "CRITICAL";
            case LOG_LEVEL_ERROR:    return "ERROR";
            case LOG_LEVEL_WARNING:  return "WARNING";
            case LOG_LEVEL_INFO:     return "INFO";
            case LOG_LEVEL_DEBUG:    return "DEBUG";
            case LOG_LEVEL_NONE:     return "NONE";
            default:                 return "UNKNOWN";
        }
    }

    // Functions to set and get log levels
    void Logger::setSerialLogLevel(LogLevel level) {
        LogLevel oldLevel = _serialLogLevel;
        _serialLogLevel = level;
        if (Serial && oldLevel != _serialLogLevel) { // Only log if Serial is ready and level changed
          // Log this message using Serial.println itself to ensure it always appears on change
          Serial.println(String(millis()) + " [INFO] [Logger]: Serial log level changed from " + levelToString(oldLevel) + " to " + levelToString(_serialLogLevel));
        }
    }

    void Logger::setMqttLogLevel(LogLevel level) {
        LogLevel oldLevel = _mqttLogLevel;
        _mqttLogLevel = level;
        if (Serial && oldLevel != _mqttLogLevel) { // Only log if Serial is ready and level changed
          Serial.println(String(millis()) + " [INFO] [Logger]: MQTT log level changed from " + levelToString(oldLevel) + " to " + levelToString(_mqttLogLevel));
        }
    }

    LogLevel Logger::getSerialLogLevel() const {
        return _serialLogLevel;
    }

    LogLevel Logger::getMqttLogLevel() const {
        return _mqttLogLevel;
    }


#### Step 2.2: Initialize and Use `AppLogger` in `main.cpp`

**1. Include `Logger.h` and Initialize `AppLogger`:**

In `src/main.cpp`, add `#include "../include/Logger.h"` at the beginning of the file. The `AppLogger` variable is already declared as `extern` in `Logger.h` and defined in `Logger.cpp`, so you can use it directly.

**2. Modify the `setup()` function:**

    // In src/main.cpp

    // ... (other includes) ...
    #include "../include/Logger.h" // Ensure Logger.h is included

    // ... (declarations of global manager objects) ...
    // SensorManager sensorManager;
    // NetworkManager networkManager; // Already exists
    // RelayManager relayManager;
    // EnvironmentManager envManager(sensorManager);
    // TaskScheduler taskScheduler(relayManager, envManager);

    void setup() {
        // 1. Initialize Serial as early as possible
        Serial.begin(115200);
        // Wait a bit for Serial to ensure initialization logs can be seen
        uint32_t serialStartTime = millis();
        while (!Serial && (millis() - serialStartTime < 2000)) { // Wait a maximum of 2 seconds
            delay(10);
        }
        Serial.println(F("\n\nMain: Serial port initialized.")); // F() to save RAM

        // 2. Initialize NetworkManager (important for MQTT logging)
        Serial.println(F("Main: Initializing NetworkManager..."));
        if (networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT /*, MQTT_USER, MQTT_PASS*/)) {
            Serial.println(F("Main: NetworkManager initialization successful (WiFi connected, MQTT attempt)."));
            // Configure NTP, MQTT callback, subscribe topics...
            // Example:
            // configTime(7 * 3600, 0, NTP_SERVER);
            // setenv("TZ", "Asia/Ho_Chi_Minh", 1);
            // tzset();
            // networkManager.setCallback(mqttCallback);
            // networkManager.subscribe(MQTT_TOPIC_CONTROL);
            // ... other subscriptions ...
        } else {
            Serial.println(F("Main: ERROR - NetworkManager initialization failed (WiFi or MQTT connection problem)."));
        }

        // 3. Initialize Logger AFTER NetworkManager has been initialized (or at least connection attempted)
        // Initial log levels: DEBUG for Serial (for development debugging), INFO for MQTT (to avoid flooding the server)
        AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO);
        // From now on, use AppLogger instead of Serial.println

        AppLogger.info("Setup", "System setup sequence started.");

        // 4. Initialize other components and replace Serial.println
        AppLogger.debug("Setup", "Initializing GPIO...");
        GPIO_Init(); // This function should log itself if it has important sub-steps

        AppLogger.debug("Setup", "Initializing RelayManager...");
        relayManager.begin(relayPins, numRelays); // RelayManager::begin() SHOULD use AppLogger internally

        AppLogger.debug("Setup", "Initializing TaskScheduler...");
        taskScheduler.begin(); // TaskScheduler::begin() SHOULD use AppLogger

        AppLogger.debug("Setup", "Initializing SensorManager...");
        sensorManager.begin(); // SensorManager::begin() SHOULD use AppLogger
        
        // ... (NTP configuration, MQTT callback, subscribe topics if not done above) ...
        // Example:
        if (networkManager.isConnected()) { // Only subscribe if MQTT is connected
            AppLogger.info("Setup", "Configuring NTP and MQTT subscriptions...");
            configTime(7 * 3600, 0, NTP_SERVER); // GMT+7 for Vietnam
            setenv("TZ", TZ_INFO, 1);
            tzset();
            AppLogger.info("Setup", "NTP Server configured: " + String(NTP_SERVER) + " with timezone " + String(TZ_INFO));

            networkManager.setCallback(mqttCallback);
            if (networkManager.subscribe(MQTT_TOPIC_CONTROL)) {
                AppLogger.info("Setup", "Subscribed to control topic: " + String(MQTT_TOPIC_CONTROL));
            } else { AppLogger.error("Setup", "Failed to subscribe to control topic."); }
            // ... other subscriptions similarly ...
             if (networkManager.subscribe(MQTT_TOPIC_SCHEDULE)) {
                AppLogger.info("Setup", "Subscribed to schedule topic: " + String(MQTT_TOPIC_SCHEDULE));
            } else { AppLogger.error("Setup", "Failed to subscribe to schedule topic."); }
            if (networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL)) {
                AppLogger.info("Setup", "Subscribed to environment control topic: " + String(MQTT_TOPIC_ENV_CONTROL));
            } else { AppLogger.error("Setup", "Failed to subscribe to environment control topic."); }
        } else {
            AppLogger.warning("Setup", "Skipping NTP/MQTT subscriptions due to no network connection.");
        }

        // 5. Create Tasks
        AppLogger.info("Setup", "Creating and pinning tasks to cores...");
        xTaskCreatePinnedToCore(
            Core0TaskCode, "Core0Task", STACK_SIZE_CORE0, NULL, PRIORITY_MEDIUM, &core0Task, 0);
        xTaskCreatePinnedToCore(
            Core1TaskCode, "Core1Task", STACK_SIZE_CORE1, NULL, PRIORITY_MEDIUM, &core1Task, 1);

        AppLogger.info("Setup", "System setup sequence completed. Tasks are running.");
        AppLogger.info("Setup", "---------------- SYSTEM READY ----------------");

        // Remove any remaining unnecessary Serial.println calls in setup()
    }


#### Step 2.3: Replace `Serial.print`/`println` Throughout the Project

This is the most time-consuming step. You need to review all `.cpp` files in the `src` directory and files in `include` (if they contain `Serial.print` commands).

**General Principles:**

1. **Include `Logger.h`**: At the beginning of each `.cpp` file that needs logging, add `#include "../include/Logger.h"` (or the appropriate relative path).

2. **Determine Log Level**: For each old `Serial.println`, decide which level it belongs to:

   - Error messages, failures: `AppLogger.error("TAG", "Message");`

   - Warnings, unusual situations but not yet errors: `AppLogger.warning("TAG", "Message");`

   - Information about normal operational status, main steps: `AppLogger.info("TAG", "Message");`

   - Detailed information for debugging (variable values, minor steps): `AppLogger.debug("TAG", "Message");`

   - Critical errors: `AppLogger.critical("TAG", "Message");`

3. **Choose a Tag**: Use consistent tags for each module or context. For example:

   - `RelayManager`: `"RelayMgr"`

   - `TaskScheduler`: `"TaskSched"`

   - `SensorManager`: `"SensorMgr"`

   - `NetworkManager`: `"NetMgr"`

   - `EnvironmentManager`: `"EnvMgr"`

   - `Core0TaskCode` in `main.cpp`: `"Core0"`

   - `Core1TaskCode` in `main.cpp`: `"Core1"`

   - `mqttCallback` in `main.cpp`: `"MQTTCallbk"`

4. **Use `AppLogger.logf()` for Formatted Strings**: If you have `Serial.printf()`, switch to `AppLogger.logf(LEVEL, "TAG", "format string %d %s", var1, var2);`.

**Specific Example - Removing Serial in `RelayManager.cpp`:**

    // In src/RelayManager.cpp
    #include "../include/RelayManager.h"
    #include "../include/Logger.h" // << ADD THIS LINE
    #include <time.h>

    RelayManager::RelayManager() {
        _relayPins = nullptr;
        _numRelays = 0;
        _relayStatus = nullptr;
        _mutex = xSemaphoreCreateMutex();
        _statusChanged = false;
        // No more Serial.println here
    }

    void RelayManager::begin(const int* relayPins, int numRelays) {
        _relayPins = relayPins;
        _numRelays = numRelays;
        _relayStatus = new RelayStatus[numRelays];
        // ... (pin initialization) ...
        _statusChanged = true;
        // REMOVE: Serial.println("RelayManager initialized with " + String(_numRelays) + " relays");
        AppLogger.info("RelayMgr", "Initialized with " + String(_numRelays) + " relays.");
    }

    void RelayManager::setRelay(int relayIndex, bool state, unsigned long duration) {
        if (relayIndex < 0 || relayIndex >= _numRelays) {
            // REMOVE: Serial.println("ERROR: Invalid relay index: " + String(relayIndex));
            AppLogger.error("RelayMgr", "Invalid relay index: " + String(relayIndex));
            return;
        }
        if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
            bool stateChanged = (_relayStatus[relayIndex].state != state);
            // ... (current logic) ...
            if (state) {
                digitalWrite(_relayPins[relayIndex], HIGH);
                _relayStatus[relayIndex].state = true;
                if (duration > 0) {
                    _relayStatus[relayIndex].endTime = millis() + duration;
                    // REMOVE: Serial.println("Relay " + String(relayIndex + 1) + " turned ON for " + String(duration / 1000) + " seconds");
                    AppLogger.debug("RelayMgr", "Relay " + String(relayIndex + 1) + " ON. Duration: " + String(duration / 1000) + "s. End time: " + String(_relayStatus[relayIndex].endTime));
                } else {
                    _relayStatus[relayIndex].endTime = 0;
                    // REMOVE: Serial.println("Relay " + String(relayIndex + 1) + " turned ON indefinitely");
                    AppLogger.debug("RelayMgr", "Relay " + String(relayIndex + 1) + " ON indefinitely.");
                }
            } else {
                digitalWrite(_relayPins[relayIndex], LOW);
                _relayStatus[relayIndex].state = false;
                _relayStatus[relayIndex].endTime = 0;
                // REMOVE: Serial.println("Relay " + String(relayIndex + 1) + " turned OFF");
                AppLogger.debug("RelayMgr", "Relay " + String(relayIndex + 1) + " OFF.");
            }
            if (stateChanged /* || durationChanged */) { // durationChanged is already handled in the debug logic above
                _statusChanged = true;
            }
            xSemaphoreGive(_mutex);
        }
    }

    void RelayManager::update() {
        if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
            unsigned long currentTime = millis();
            bool anyRelayChanged = false;
            for (int i = 0; i < _numRelays; i++) {
                if (_relayStatus[i].state && _relayStatus[i].endTime > 0) {
                    if (currentTime >= _relayStatus[i].endTime) {
                        digitalWrite(_relayPins[i], LOW);
                        _relayStatus[i].state = false;
                        _relayStatus[i].endTime = 0;
                        anyRelayChanged = true;
                        // REMOVE: Serial.println("Auto turned OFF relay " + String(i + 1) + " (timer expired)");
                        AppLogger.info("RelayMgr", "Auto OFF relay " + String(i + 1) + " (timer expired).");
                    }
                }
            }
            if (anyRelayChanged) {
                _statusChanged = true;
            }
            xSemaphoreGive(_mutex);
        }
    }

    bool RelayManager::processCommand(const char* json) {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, json);
        if (error) {
            // REMOVE: Serial.println("JSON parsing failed: " + String(error.c_str()));
            AppLogger.error("RelayMgr", "JSON command parsing failed: " + String(error.c_str()));
            return false;
        }
        if (!doc.containsKey("relays")) {
            // REMOVE: Serial.println("Command missing 'relays' field");
            AppLogger.warning("RelayMgr", "Relay command missing 'relays' field. Payload: " + String(json));
            return false;
        }
        // ... (rest of the function, replace other Serial.println calls if any) ...
        // Example:
        // AppLogger.debug("RelayMgr", "Processing relay command: " + String(json));
        bool anyChanges = false; // Assume this variable is set based on command processing
        // ...
        return anyChanges; 
    }

    // Continue similarly for other functions and .cpp files
    // (TaskScheduler.cpp, SensorManager.cpp, NetworkManager.cpp, EnvironmentManager.cpp, main.cpp within tasks)

**Example in `Core0TaskCode` (`main.cpp`):**

    void Core0TaskCode(void * parameter) {
      AppLogger.info("Core0", "Task started on core " + String(xPortGetCoreID()));
      
      for(;;) {
        networkManager.loop(); // NetworkManager::loop() should log its important events internally
        unsigned long currentTime = millis();
        
        if (currentTime - lastSensorReadTime >= sensorReadInterval) {
          lastSensorReadTime = currentTime;
          if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
            if (sensorManager.readSensors()) { // sensorManager.readSensors() should log errors internally
              envManager.setCurrentTemperature(sensorManager.getTemperature());
              envManager.setCurrentHumidity(sensorManager.getHumidity());
              envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());
              
              // REMOVE verbose Serial.print calls here
              // Replace with a more concise DEBUG or INFO log if needed
              AppLogger.debug("Core0", "Sensors read: T=" + String(sensorManager.getTemperature()) +
                                      "C, H=" + String(sensorManager.getHumidity()) +
                                      "%, HI=" + String(sensorManager.getHeatIndex()) + "C");
              // printLocalTime(); // This function can be kept if it only Serial.prints time, or convert it to AppLogger.debug
              
              if (networkManager.isConnected()) {
                String payload = sensorManager.getJsonPayload(API_KEY);
                networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str()); // NetworkManager::publish should log the publish action
              } else {
                // REMOVE: Serial.println("ERROR: No network connection, cannot send data");
                AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT.");
                // RGB_Light(255, 0, 0); delay(100); RGB_Light(0, 0, 0); // Keep if this is a physical error indicator
              }
            } else {
               // sensorManager.readSensors() already logged the error, no need to log again here unless adding Core0 context
               // AppLogger.error("Core0", "Failed to read from sensors, check SensorMgr logs.");
            }
            xSemaphoreGive(sensorDataMutex);
          }
        }
        // ... (rest of Core0TaskCode, replace other Serial.println calls) ...
        // Example:
        // AppLogger.debug("Core0", "Checking task scheduler...");
        // taskScheduler.update(); // taskScheduler.update() should log its activities internally
      }
    }


#### Step 2.4: (Optional) Implement Runtime Log Configuration

If you want the ability to change log levels remotely via MQTT:

1. **Define a New MQTT Topic**: In `main.cpp`, add: `const char* MQTT_TOPIC_LOG_CONFIG = "irrigation/esp32_6relay/logconfig";`

2. **Subscribe to the Topic in `setup()`**: After `networkManager.setCallback(mqttCallback);` and MQTT is connected:

       if (networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG)) {
           AppLogger.info("Setup", "Subscribed to log config topic: " + String(MQTT_TOPIC_LOG_CONFIG));
       } else {
           AppLogger.error("Setup", "Failed to subscribe to log config topic.");
       }

3. **Handle in `mqttCallback()` (`main.cpp`):** Add an `else if` block to process this topic:

       // ... (in mqttCallback function)
       else if (strcmp(topic, MQTT_TOPIC_LOG_CONFIG) == 0) {
           // Convert payload to string for logging before parsing
           char messageStr[length + 1];
           memcpy(messageStr, payload, length);
           messageStr[length] = '\0';
           AppLogger.info("MQTTCallbk", "Received log configuration command. Payload: " + String(messageStr));

           DynamicJsonDocument doc(256); // Small payload for config
           DeserializationError error = deserializeJson(doc, payload, length); // Use original payload

           if (error) {
               AppLogger.error("MQTTCallbk", "Log config JSON parsing failed: " + String(error.c_str()));
               return;
           }

           const char* target = doc["target"]; // "serial" or "mqtt"
           const char* levelStr = doc["level"];  // "NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG"

           if (target && levelStr) {
               LogLevel newLevel = LOG_LEVEL_NONE; // Default
               if (strcmp(levelStr, "CRITICAL") == 0) newLevel = LOG_LEVEL_CRITICAL;
               else if (strcmp(levelStr, "ERROR") == 0) newLevel = LOG_LEVEL_ERROR;
               else if (strcmp(levelStr, "WARNING") == 0) newLevel = LOG_LEVEL_WARNING;
               else if (strcmp(levelStr, "INFO") == 0) newLevel = LOG_LEVEL_INFO;
               else if (strcmp(levelStr, "DEBUG") == 0) newLevel = LOG_LEVEL_DEBUG;

               if (strcmp(target, "serial") == 0) {
                   AppLogger.setSerialLogLevel(newLevel);
                   // AppLogger.info("MQTTCallbk", "Serial log level changed to: " + String(levelStr)); // setSerialLogLevel already logs this
               } else if (strcmp(target, "mqtt") == 0) {
                   AppLogger.setMqttLogLevel(newLevel);
                   // AppLogger.info("MQTTCallbk", "MQTT log level changed to: " + String(levelStr)); // setMqttLogLevel already logs this
               } else {
                   AppLogger.warning("MQTTCallbk", "Invalid log config target: " + String(target));
               }
           } else {
               AppLogger.warning("MQTTCallbk", "Log config command missing 'target' or 'level' field.");
           }
       }
       // ...


### 3. Compile and Test

After making the changes:

1. **Recompile the Project**: Use PlatformIO to build.

2. **Check Serial Logs**: Open PlatformIO's Serial Monitor. You should see the new logs formatted as: `[TIMESTAMP] [LEVEL] [TAG]: Message`.

3. **Check MQTT Logs**: Use an MQTT client (like MQTT Explorer) to subscribe to the `irrigation/esp32_6relay/logs` topic. You should see logs sent as JSON.

4. **Test Log Levels**:

   - Change `initialSerialLogLevel` and `initialMqttLogLevel` in `AppLogger.begin()` to see how different levels work.

   - If runtime configuration is implemented, send MQTT commands to change log levels and observe the results.

5. **Test Error Cases**: Induce some errors (e.g., disconnect WiFi, send malformed JSON) to see if error logs are recorded correctly.


### 4. Important Notes

- **Initialization Order**: Ensure `Serial.begin()` is called before `AppLogger.begin()`. `NetworkManager` should also be initialized (or at least start its connection process) before `AppLogger.begin()` if you want MQTT logging to work from the start.

- **Blocking Calls**: Be cautious of potentially blocking functions within the Logger, especially `_networkManager->publish()`. If MQTT publishing takes a long time, it could affect the performance of the task calling the log function.

- **Buffer Sizes**: `StaticJsonDocument` in `formatToJson` and the buffer in `logf` have fixed sizes. Ensure they are large enough for your log messages but not excessively large to waste memory.

- **Check `Serial` Object**: Before calling `Serial.println()` within `Logger`'s methods (e.g., in `setSerialLogLevel`), always check `if (Serial)` to ensure the serial port is initialized and ready, avoiding runtime errors.

By following these steps, you will have a flexible, structured logging system that significantly improves your ability to monitor and debug your ESP32-S3 project.
