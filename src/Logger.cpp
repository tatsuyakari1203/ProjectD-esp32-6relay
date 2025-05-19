#include "../include/Logger.h" // Path to Logger.h
#include <time.h>               // To use time() for Unix timestamp
#include <stdarg.h>             // For va_list, va_start, va_end in logf

// Global AppLogger instance
Logger AppLogger;

// External declaration for API_KEY (defined in main.cpp)
extern const char* API_KEY;

Logger::Logger() : _networkManager(nullptr), _serialLogLevel(LOG_LEVEL_NONE), _mqttLogLevel(LOG_LEVEL_NONE), _apiKey(nullptr) {
    // Constructor
}

void Logger::begin(NetworkManager* networkManager, LogLevel initialSerialLogLevel, LogLevel initialMqttLogLevel) {
    _networkManager = networkManager;
    setSerialLogLevel(initialSerialLogLevel); // Use setter to log the change itself
    setMqttLogLevel(initialMqttLogLevel);   // Use setter for consistency

    _apiKey = API_KEY; // Store the API key from main.cpp
    
    // Log Logger initialization. This message appears if Serial log level is INFO or higher
    // and Serial.begin() has been called.
    if (_serialLogLevel >= LOG_LEVEL_INFO && Serial) {
         char logBuffer[200];
         snprintf(logBuffer, sizeof(logBuffer), "%lu [INFO] [Logger]: Logger initialized. Serial LogLevel: %s, MQTT LogLevel: %s", millis(), levelToString(_serialLogLevel).c_str(), levelToString(_mqttLogLevel).c_str());
         Serial.println(logBuffer);
         if (_mqttLogLevel > LOG_LEVEL_NONE) {
            if (_networkManager && _networkManager->isConnected()) {
                snprintf(logBuffer, sizeof(logBuffer), "%lu [INFO] [Logger]: MQTT logging active.", millis());
                Serial.println(logBuffer);
            } else {
                snprintf(logBuffer, sizeof(logBuffer), "%lu [WARNING] [Logger]: MQTT logging configured, but NetworkManager not available or not connected at init.", millis());
                Serial.println(logBuffer);
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

// Main logging function. Creates a LogEntry and processes it.
void Logger::log(LogLevel level, const String& tag, const String& message) {
    // Only process if the log level is enabled for at least one output (Serial or MQTT)
    bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
    bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

    if (!shouldLogSerial && !shouldLogMqtt) {
        return; // No target wants to log at this level
    }

    LogEntry entry;
    // Use Unix time if NTP is synced; otherwise, fallback to millis()
    if (time(nullptr) > 1000000000L) { // Basic check for NTP sync (valid Unix timestamp)
        entry.timestamp = time(nullptr);
    } else {
        entry.timestamp = millis(); 
    }
    entry.level = level;
    entry.tag = tag;
    entry.message = message;

    processLogEntry(entry); // Send to common processing function
}

// Logging function with printf-style formatting.
void Logger::logf(LogLevel level, const String& tag, const char* format, ...) {
    // Check if level is enabled before formatting to save processing
    bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
    bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

    if (!shouldLogSerial && !shouldLogMqtt) {
        return;
    }

    char buffer[256]; // Buffer for the formatted string. Ensure it's large enough.
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); 
    va_end(args);

    log(level, tag, String(buffer)); // Call the main log function
}

// Internal function to dispatch the log entry to configured destinations (Serial, MQTT).
void Logger::processLogEntry(const LogEntry& entry) {
    // 1. Log to Serial
    if (entry.level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE && Serial) { // Check Serial readiness
        char logBuffer[300]; // Buffer for serial output line
        int offset = snprintf(logBuffer, sizeof(logBuffer), "%lu [%s]", (unsigned long)entry.timestamp, levelToString(entry.level).c_str());
        if (!entry.tag.isEmpty() && offset < sizeof(logBuffer)) {
            offset += snprintf(logBuffer + offset, sizeof(logBuffer) - offset, " [%s]", entry.tag.c_str());
        }
        if (offset < sizeof(logBuffer)) {
            snprintf(logBuffer + offset, sizeof(logBuffer) - offset, ": %s", entry.message.c_str());
        }
        Serial.println(logBuffer); 
    }

    // 2. Log via MQTT
    if (entry.level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE) {
        if (_networkManager && _networkManager->isConnected()) {
            String jsonPayload = formatToJson(entry);
            // NetworkManager::publish should ideally be non-blocking or handle queueing/retries for critical logs.
            _networkManager->publish(_mqttLogTopic, jsonPayload.c_str());
        }
        // No 'else' to avoid recursive logging attempts if MQTT is disconnected.
    }
}

// Converts a LogEntry to a JSON string for MQTT transmission.
String Logger::formatToJson(const LogEntry& entry) {
    StaticJsonDocument<512> doc; // Adjust JSON document size if more fields are added
    
    if (_apiKey != nullptr) {
        doc["api_key"] = _apiKey;
    }
    
    doc["timestamp"] = entry.timestamp;
    doc["level_num"] = static_cast<int>(entry.level); // Numeric level for easier machine processing
    doc["level_str"] = levelToString(entry.level);   // String level for human readability
    doc["tag"] = entry.tag;
    doc["message"] = entry.message;
    
    // Add Core ID and Free Heap to provide more context
    doc["core_id"] = xPortGetCoreID();
    doc["free_heap"] = ESP.getFreeHeap();

    String output;
    serializeJson(doc, output); 
    return output;
}

// Converts LogLevel enum to its string representation.
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
    if (Serial && oldLevel != _serialLogLevel) { // Log only if Serial is ready and level actually changed
       // This message is sent directly via Serial.println to ensure it appears during level changes.
       char logBuffer[150];
       snprintf(logBuffer, sizeof(logBuffer), "%lu [INFO] [Logger]: Serial log level changed from %s to %s", millis(), levelToString(oldLevel).c_str(), levelToString(_serialLogLevel).c_str());
       Serial.println(logBuffer);
    }
}

void Logger::setMqttLogLevel(LogLevel level) {
    LogLevel oldLevel = _mqttLogLevel;
    _mqttLogLevel = level;
    if (Serial && oldLevel != _mqttLogLevel) { // Log only if Serial is ready and level actually changed
       char logBuffer[150];
       snprintf(logBuffer, sizeof(logBuffer), "%lu [INFO] [Logger]: MQTT log level changed from %s to %s", millis(), levelToString(oldLevel).c_str(), levelToString(_mqttLogLevel).c_str());
       Serial.println(logBuffer);
    }
}

LogLevel Logger::getSerialLogLevel() const {
    return _serialLogLevel;
}

LogLevel Logger::getMqttLogLevel() const {
    return _mqttLogLevel;
}

// Performance Logging Function
// Logs performance metrics for specific events.
void Logger::perf(const String& tag, const String& eventName, unsigned long durationMs, bool success, const String& details) {
    // Performance logs are treated as INFO level. This can be made configurable if needed.
    LogLevel level = LOG_LEVEL_INFO;

    bool shouldLogSerial = (level <= _serialLogLevel && _serialLogLevel != LOG_LEVEL_NONE);
    bool shouldLogMqtt = (level <= _mqttLogLevel && _mqttLogLevel != LOG_LEVEL_NONE);

    if (!shouldLogSerial && !shouldLogMqtt) {
        return; 
    }

    // Create the main message part for the performance log
    char perfMessageBuffer[256]; 
    int perfMessageOffset = snprintf(perfMessageBuffer, sizeof(perfMessageBuffer), "PERF: Event='%s', Duration=%lums, Success=%s",
                                   eventName.c_str(), durationMs, success ? "true" : "false");
    if (!details.isEmpty() && perfMessageOffset < sizeof(perfMessageBuffer)) {
        snprintf(perfMessageBuffer + perfMessageOffset, sizeof(perfMessageBuffer) - perfMessageOffset, ", Details='%s'", details.c_str());
    }

    // Log to Serial (if enabled for INFO level)
    if (shouldLogSerial && Serial) {
        LogEntry entry; // Temporary LogEntry for Serial output context
        if (time(nullptr) > 1000000000L) {
            entry.timestamp = time(nullptr);
        } else {
            entry.timestamp = millis();
        }
        entry.level = level;
        entry.tag = tag;
        entry.message = perfMessageBuffer; // The pre-formatted performance message

        // Format specifically for serial, including core ID and heap for this log line
        char serialLogBuffer[350]; 
        int serialOffset = snprintf(serialLogBuffer, sizeof(serialLogBuffer), "%lu [%s] [%s] [Core:%d, Heap:%u]: %s",
                                (unsigned long)entry.timestamp,
                                levelToString(entry.level).c_str(),
                                entry.tag.c_str(),
                                (int)xPortGetCoreID(),
                                ESP.getFreeHeap(),
                                entry.message.c_str()); 
        Serial.println(serialLogBuffer);
    }

    // Log to MQTT (if enabled for INFO level and network is connected)
    if (shouldLogMqtt && _networkManager && _networkManager->isConnected()) {
        StaticJsonDocument<512> doc; // JSON document for MQTT payload
        
        if (_apiKey != nullptr) {
            doc["api_key"] = _apiKey;
        }
        
        if (time(nullptr) > 1000000000L) {
            doc["timestamp"] = time(nullptr);
        } else {
            doc["timestamp"] = millis();
        }
        doc["level_num"] = static_cast<int>(level);
        doc["level_str"] = levelToString(level); 
        doc["tag"] = tag;
        // Structure for performance event data
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
        _networkManager->publish(_mqttLogTopic, mqttPayload.c_str()); 
        // Consider a dedicated MQTT topic for performance logs if their volume is high
        // or if they need different downstream processing.
    }
} 