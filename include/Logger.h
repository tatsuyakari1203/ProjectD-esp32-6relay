#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "NetworkManager.h" // Required for the Logger to send logs via MQTT

// Defines the severity levels for log messages.
enum LogLevel {
    LOG_LEVEL_NONE = 0,     // Disables logging.
    LOG_LEVEL_CRITICAL = 1, // Critical errors; system integrity may be compromised.
    LOG_LEVEL_ERROR = 2,    // Errors indicating a specific function or operation failed.
    LOG_LEVEL_WARNING = 3,  // Warnings about potential issues or unexpected situations.
    LOG_LEVEL_INFO = 4,     // Informational messages about normal system operations.
    LOG_LEVEL_DEBUG = 5     // Detailed debugging information (e.g., variable values, granular steps).
};

// Represents a single log record.
struct LogEntry {
    unsigned long timestamp; // Timestamp of the log event (Unix time or millis() if NTP not synced).
    LogLevel level;          // Severity level of the log.
    String tag;              // Tag to categorize or identify the source of the log (e.g., "RelayMgr", "Core0Task").
    String message;          // The content of the log message.
};

class Logger {
public:
    Logger();

    // Initializes the Logger.
    // networkManager: Pointer to the NetworkManager instance for MQTT log transmission.
    // initialSerialLogLevel: Initial logging threshold for Serial output.
    // initialMqttLogLevel: Initial logging threshold for MQTT output.
    void begin(NetworkManager* networkManager, LogLevel initialSerialLogLevel = LOG_LEVEL_INFO, LogLevel initialMqttLogLevel = LOG_LEVEL_WARNING);

    // Convenience methods for logging at specific severity levels.
    void critical(const String& tag, const String& message);
    void error(const String& tag, const String& message);
    void warning(const String& tag, const String& message);
    void info(const String& tag, const String& message);
    void debug(const String& tag, const String& message);

    // Core logging method.
    void log(LogLevel level, const String& tag, const String& message);
    // Core logging method with printf-style string formatting.
    void logf(LogLevel level, const String& tag, const char* format, ...);

    // Logs a performance metric for a specific event.
    // tag: Source tag for the performance log.
    // eventName: Descriptive name of the event being measured (e.g., "SensorRead", "MqttPublish").
    // durationMs: Duration of the event in milliseconds.
    // success: Optional boolean indicating if the operation was successful (defaults to true).
    // details: Optional string for any additional relevant details or metrics.
    void perf(const String& tag, const String& eventName, unsigned long durationMs, bool success = true, const String& details = "");

    // Methods to dynamically configure log output levels.
    void setSerialLogLevel(LogLevel level);
    void setMqttLogLevel(LogLevel level);
    LogLevel getSerialLogLevel() const;
    LogLevel getMqttLogLevel() const;

private:
    NetworkManager* _networkManager; // Pointer to NetworkManager for MQTT log publishing.
    LogLevel _serialLogLevel;        // Current logging threshold for Serial output.
    LogLevel _mqttLogLevel;          // Current logging threshold for MQTT output.
    const char* _apiKey;             // API key, potentially for authenticating logs (if implemented).

    const char* _mqttLogTopic = "irrigation/esp32_6relay/logs"; // Default MQTT topic for publishing logs.

    // Internal helper methods for log processing.
    void processLogEntry(const LogEntry& entry); // Dispatches a log entry to configured outputs.
    String formatToJson(const LogEntry& entry);  // Formats a LogEntry into a JSON string for MQTT.
    String levelToString(LogLevel level);        // Converts a LogLevel enum to its string representation.
};

// Global instance of the Logger, accessible throughout the application.
extern Logger AppLogger;

#endif // LOGGER_H 