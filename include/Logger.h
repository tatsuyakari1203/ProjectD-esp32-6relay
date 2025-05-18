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