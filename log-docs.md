# ESP32 Irrigation System: Logging Documentation

This document provides detailed information about the logging system implemented in the ESP32 Irrigation Controller project. The logger provides a unified way to send log messages to both Serial output (for local debugging) and MQTT (for remote monitoring).

## Table of Contents

1. [Logging Overview](#logging-overview)
2. [Log Levels](#log-levels)
3. [Logging to Serial and MQTT](#logging-to-serial-and-mqtt)
4. [Log Output Format Details](#log-output-format-details)
5. [Performance Logging](#performance-logging)
6. [Runtime Configuration](#runtime-configuration)
7. [API Reference](#api-reference)
8. [JSON Format](#json-format)
9. [Best Practices](#best-practices)

## Logging Overview

The logging system is built around the `Logger` class and the global `AppLogger` instance. It provides:

- Multi-level logging (critical, error, warning, info, debug)
- Dual-target output (Serial console and MQTT)
- Runtime-configurable log levels
- Contextual data (timestamp, core ID, free heap)
- Performance metrics logging
- API key authentication for MQTT logs

## Log Levels

The system defines the following log levels in order of severity:

| Level | Enum Value | Purpose |
|-------|------------|---------|
| NONE | 0 | Disable logging entirely for the target |
| CRITICAL | 1 | Critical errors, system might crash or malfunction completely |
| ERROR | 2 | Errors, a specific function is not working correctly |
| WARNING | 3 | Warnings, potential issues or unexpected situations |
| INFO | 4 | Information about normal operations, main system steps |
| DEBUG | 5 | Detailed information for debugging (e.g., variable values, minor steps) |

Each log level includes all messages of that level and below (more severe). For example, setting the log level to WARNING will include WARNING, ERROR, and CRITICAL messages, but will filter out INFO and DEBUG messages.

## Logging to Serial and MQTT

The system can log to:

1. **Serial Console**: For local debugging during development
2. **MQTT**: For remote monitoring in production

Each destination has its own log level configuration. For example, you might set Serial to DEBUG during development, but set MQTT to INFO or WARNING in production to reduce network traffic.

## Log Output Format Details

### Serial Output Format Explained

The Serial output uses a human-readable format that includes timestamp, log level, tag, and message. Here's a breakdown of a typical log line:

```
1747582906 [INFO] [Setup]: System setup sequence started.
```

Breaking down the components:
- `1747582906` - Timestamp (either Unix epoch seconds if NTP is synced, or milliseconds since boot)
- `[INFO]` - Log level
- `[Setup]` - Module/component tag
- `System setup sequence started.` - The actual log message

For performance logs, additional contextual information is included:

```
1747582934 [INFO] [Core0] [Core:0, Heap:260852]: PERF: Event='SensorReadOperation', Duration=13ms, Success=true
```

Breaking down the components:
- `1747582934` - Timestamp
- `[INFO]` - Log level (always INFO for performance logs)
- `[Core0]` - Module/component tag
- `[Core:0, Heap:260852]` - Contextual information showing the core ID and available heap memory
- `PERF: Event='SensorReadOperation', Duration=13ms, Success=true` - Performance data

### MQTT Output Format Explained

MQTT logs are transmitted as JSON objects, making them easy to process in backend systems. Here's an example of a standard log message:

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1747582906,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "Setup",
  "message": "System setup sequence started.",
  "core_id": 0,
  "free_heap": 258852
}
```

Breaking down the components:
- `api_key` - API key for authentication with the server
- `timestamp` - Unix timestamp or milliseconds since boot
- `level_num` - Numeric log level (1-5)
- `level_str` - String representation of log level
- `tag` - Module/component tag
- `message` - The actual log message
- `core_id` - ESP32 CPU core that generated the log (0 or 1)
- `free_heap` - Available heap memory at the time of logging (useful for tracking memory leaks)

Performance logs include additional fields:

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1747582934,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "Core0",
  "type": "performance",
  "event_name": "SensorReadOperation",
  "duration_ms": 13,
  "success": true,
  "core_id": 0,
  "free_heap": 260852
}
```

Breaking down the additional fields:
- `type` - Always "performance" for performance logs
- `event_name` - Name of the operation being measured
- `duration_ms` - Duration of the operation in milliseconds
- `success` - Boolean indicating whether the operation succeeded

### Actual Log Output Examples

Here are examples of actual log output as seen in the Serial monitor:

```
1747582906 [INFO] [Setup]: System setup sequence started.
1747582906 [INFO] [Setup]: ESP32-S3 Dual-Core Irrigation System
1747582906 [DEBUG] [Setup]: Initializing GPIO...
1747582906 [INFO] [Setup]: GPIO initialized
1747582906 [DEBUG] [Setup]: Initializing RelayManager...
1747582906 [INFO] [RelayMgr]: Initialized with 6 relays
1747582906 [DEBUG] [Setup]: Initializing TaskScheduler...
1747582906 [INFO] [Core1]: Task started on core 1
1747582907 [INFO] [Core0]: Task started on core 0
1747582907 [INFO] [Setup]: System setup sequence completed. Tasks are running.
1747582907 [INFO] [Setup]: ---------------- SYSTEM READY ----------------
1747582909 [DEBUG] [Core0]: Sensors read: T=27.60°C, H=52.10%, HI=28.18°C
1747582909 [INFO] [Core0] [Core:0, Heap:258852]: PERF: Event='MQTTSensorDataPublish', Duration=2ms, Success=true
1747582909 [DEBUG] [Core0]: Sensor data published to MQTT
1747582909 [INFO] [Core0] [Core:0, Heap:259252]: PERF: Event='SensorReadOperation', Duration=10ms, Success=true
```

The structured nature of these logs makes it easy to:
1. Filter logs by level for troubleshooting
2. Search for specific components using the tag
3. Track operations over time using timestamps
4. Monitor system resources with free heap information
5. Identify performance bottlenecks with performance logs

## Performance Logging

The logging system includes a specialized function for measuring and logging performance metrics:

```cpp
AppLogger.perf("Tag", "EventName", durationMs, success, "optional details");
```

This creates a structured log entry with:
- Event name
- Duration in milliseconds
- Success/failure indicator
- Optional details
- Contextual data (core ID, free heap)

Example use case:

```cpp
// Start timing
unsigned long startTime = millis();

// Perform operation
bool success = someOperation();

// End timing and log performance
unsigned long duration = millis() - startTime;
AppLogger.perf("ModuleName", "OperationName", duration, success);
```

## Runtime Configuration

Log levels can be changed at runtime via MQTT. Send a message to `irrigation/esp32_6relay/logconfig` with the following JSON structure:

```json
{
  "target": "serial",  // or "mqtt"
  "level": "DEBUG"     // "NONE", "CRITICAL", "ERROR", "WARNING", "INFO", or "DEBUG"
}
```

## API Reference

### Logger Initialization

```cpp
// Initialize logger with NetworkManager and optional initial log levels
AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO);
```

### Basic Logging Functions

```cpp
// Log at specific levels
AppLogger.critical("Tag", "Critical message");
AppLogger.error("Tag", "Error message");
AppLogger.warning("Tag", "Warning message");
AppLogger.info("Tag", "Info message");
AppLogger.debug("Tag", "Debug message");

// Log with printf-style formatting
AppLogger.logf(LOG_LEVEL_INFO, "Tag", "Formatted message: Value=%d", someValue);
```

### Performance Logging

```cpp
// Log performance metrics
AppLogger.perf("Tag", "EventName", durationMs, success, "Optional details");
```

### Log Level Configuration

```cpp
// Get current levels
LogLevel serialLevel = AppLogger.getSerialLogLevel();
LogLevel mqttLevel = AppLogger.getMqttLogLevel();

// Set new levels
AppLogger.setSerialLogLevel(LOG_LEVEL_DEBUG);
AppLogger.setMqttLogLevel(LOG_LEVEL_WARNING);
```

## JSON Format

### Standard Log JSON Format

```json
{
  "api_key": "your-api-key",
  "timestamp": 1634567890,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "ModuleName",
  "message": "Log message",
  "core_id": 0,
  "free_heap": 123456
}
```

### Performance Log JSON Format

```json
{
  "api_key": "your-api-key",
  "timestamp": 1634567890,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "ModuleName",
  "type": "performance",
  "event_name": "OperationName",
  "duration_ms": 42,
  "success": true,
  "details": "Optional details",
  "core_id": 0,
  "free_heap": 123456
}
```

## Best Practices

### Choosing Log Levels

- **CRITICAL**: System-wide failures (e.g., "Cannot initialize hardware", "Out of memory")
- **ERROR**: Operation-specific failures (e.g., "Failed to read sensor", "MQTT connection failed")
- **WARNING**: Potential issues that don't cause failure (e.g., "Battery low", "Sensor reading outside normal range")
- **INFO**: Normal operations (e.g., "System started", "Schedule updated", "Relay activated")
- **DEBUG**: Detailed debug information (e.g., "Current temperature: 25.4°C", "Processing command")

### Choosing Tags

Use consistent, meaningful tags to categorize logs by module. Examples:
- `Setup`: System initialization
- `Core0`/`Core1`: For core-specific tasks
- `SensorMgr`: Sensor operations
- `RelayMgr`: Relay operations
- `NetMgr`: Network operations
- `TaskSched`: Scheduling operations
- `EnvMgr`: Environment monitoring
- `MQTTCallbk`: MQTT callback processing

### Performance Monitoring

Use performance logging to monitor:
- Sensor reading duration
- Network operations (MQTT publish/subscribe)
- Relay switching operations
- Task scheduling operations
- JSON parsing/serialization
- Any operation that might impact system responsiveness

### Storage and Analysis

The JSON format of MQTT logs makes them easy to:
- Store in time-series databases
- Parse and analyze with standard tools
- Visualize in dashboards
- Filter and search
- Generate alerts based on log levels or content 