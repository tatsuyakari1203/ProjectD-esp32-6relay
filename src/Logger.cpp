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