#include <Arduino.h>
#include "../include/WS_GPIO.h"
#include "../include/SensorManager.h"
#include "../include/NetworkManager.h"
#include "../include/RelayManager.h"
#include "../include/TaskScheduler.h"
#include "../include/EnvironmentManager.h"
#include "../include/Logger.h"
#include <time.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "freertos/queue.h"    

// JSON keys for MQTT payloads
static const char* JSON_KEY_SOIL_MOISTURE = "soil_moisture";
static const char* JSON_KEY_ZONE = "zone";
static const char* JSON_KEY_VALUE = "value";
static const char* JSON_KEY_RAIN = "rain";
static const char* JSON_KEY_LIGHT = "light";
static const char* JSON_KEY_TARGET = "target";
static const char* JSON_KEY_LEVEL = "level";
static const char* JSON_KEY_SERIAL = "serial";
static const char* JSON_KEY_MQTT = "mqtt";
static const char* JSON_KEY_CRITICAL = "CRITICAL";
static const char* JSON_KEY_ERROR = "ERROR";
static const char* JSON_KEY_WARNING = "WARNING";
static const char* JSON_KEY_INFO = "INFO";
static const char* JSON_KEY_DEBUG = "DEBUG";

// Function prototypes
void Core0TaskCode(void * parameter);
void Core1TaskCode(void * parameter);
void printLocalTime();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Core task definitions
TaskHandle_t core0Task;  // Handles sensors, MQTT, scheduling (preemptive)
TaskHandle_t core1Task;  // Handles irrigation control events (event-driven)

// Priority levels (higher number = higher priority)
#define PRIORITY_LOW 1
#define PRIORITY_MEDIUM 2
#define PRIORITY_HIGH 3

// Stack sizes
#define STACK_SIZE_CORE0 8192
#define STACK_SIZE_CORE1 4096

// Relay pin definitions
const int relayPins[] = {
  GPIO_PIN_CH1,
  GPIO_PIN_CH2,
  GPIO_PIN_CH3,
  GPIO_PIN_CH4,
  GPIO_PIN_CH5,
  GPIO_PIN_CH6
};

// Number of relays
const int numRelays = 6;

// WiFi and MQTT configuration
const char* WIFI_SSID = "2.4 KariS";  // TODO: Replace with your WiFi name
const char* WIFI_PASSWORD = "12123402";  // TODO: Replace with your WiFi password
const char* MQTT_SERVER = "karis.cloud";
const int MQTT_PORT = 1883;
const char* API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2";  // Provided API key

// MQTT topics
const char* MQTT_TOPIC_SENSORS = "irrigation/esp32_6relay/sensors";
const char* MQTT_TOPIC_CONTROL = "irrigation/esp32_6relay/control";
const char* MQTT_TOPIC_STATUS = "irrigation/esp32_6relay/status";
const char* MQTT_TOPIC_SCHEDULE = "irrigation/esp32_6relay/schedule";
const char* MQTT_TOPIC_SCHEDULE_STATUS = "irrigation/esp32_6relay/schedule/status";
const char* MQTT_TOPIC_ENV_CONTROL = "irrigation/esp32_6relay/environment";

// MQTT topic for log configuration
const char* MQTT_TOPIC_LOG_CONFIG = "irrigation/esp32_6relay/logconfig";

// NTP configuration
const char* NTP_SERVER = "pool.ntp.org";
const char* TZ_INFO = "Asia/Ho_Chi_Minh";  // Vietnam timezone

// Sensor and manager objects
SensorManager sensorManager;
NetworkManager networkManager;
RelayManager relayManager;
EnvironmentManager envManager(sensorManager);
TaskScheduler taskScheduler(relayManager, envManager);

// Time tracking variables
unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 30000;  // Read sensors and send data every 30 seconds
unsigned long lastForcedStatusReportTime = 0;
const unsigned long forcedStatusReportInterval = 5 * 60 * 1000;  // Force status update every 5 minutes
unsigned long lastEnvUpdateTime = 0; // Not currently used, consider for future environment polling logic
const unsigned long envUpdateInterval = 2000;  // Intended interval for environment updates

// LED status tracking
bool ledState = false;
unsigned long lastLedBlinkTime = 0;
const unsigned long ledBlinkInterval = 1000;  // Blink LED every 1 second

// Semaphores for safe access to shared resources
SemaphoreHandle_t sensorDataMutex;

// Queue for relay timer events
QueueHandle_t g_relayEventQueue; 

// Function to display current time
void printLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    AppLogger.error("Time", "Could not get time from NTP");
    return;
  }
  
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  AppLogger.logf(LOG_LEVEL_INFO, "Time", "Current time: %s, Timezone: %s (Day of week: %d)", timeString, TZ_INFO, timeinfo.tm_wday);
}

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  AppLogger.logf(LOG_LEVEL_DEBUG, "MQTTCallbk", "Received MQTT message on topic: %s", topic);
  AppLogger.logf(LOG_LEVEL_DEBUG, "MQTTCallbk", "Payload: %s", message);
  
  // Process message based on topic
  if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
    // Relay control commands are processed by RelayManager.
    // Status changes are detected and published by Core0Task.
    relayManager.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_SCHEDULE) == 0) {
    // Schedule commands are processed by TaskScheduler.
    // Status changes are detected and published by Core0Task.
    taskScheduler.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_ENV_CONTROL) == 0) {
    // Environment control command
    StaticJsonDocument<256> doc; // Estimated size for this payload
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      AppLogger.logf(LOG_LEVEL_ERROR, "MQTTCallbk", "JSON parsing failed: %s", error.c_str());
      return;
    }
    
    // Xử lý cập nhật giá trị cảm biến thủ công
    if (doc.containsKey(JSON_KEY_SOIL_MOISTURE)) {
      JsonObject soil = doc[JSON_KEY_SOIL_MOISTURE];
      int zone = soil[JSON_KEY_ZONE];
      float value = soil[JSON_KEY_VALUE];
      envManager.setSoilMoisture(zone, value);
      AppLogger.logf(LOG_LEVEL_INFO, "MQTTCallbk", "Manual soil moisture update: Zone %d, Value: %.2f", zone, value);
    }
    
    if (doc.containsKey(JSON_KEY_RAIN)) {
      bool isRaining = doc[JSON_KEY_RAIN];
      envManager.setRainStatus(isRaining);
      AppLogger.logf(LOG_LEVEL_INFO, "MQTTCallbk", "Manual rain status update: %s", isRaining ? "Raining" : "Not raining");
    }
    
    if (doc.containsKey(JSON_KEY_LIGHT)) {
      int lightLevel = doc[JSON_KEY_LIGHT];
      envManager.setLightLevel(lightLevel);
      AppLogger.logf(LOG_LEVEL_INFO, "MQTTCallbk", "Manual light level update: %d", lightLevel);
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_LOG_CONFIG) == 0) {
    // Log configuration command
    AppLogger.logf(LOG_LEVEL_INFO, "MQTTCallbk", "Received log configuration command. Payload: %s", message);

    StaticJsonDocument<128> doc; // Sufficient size for target and level
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      AppLogger.logf(LOG_LEVEL_ERROR, "MQTTCallbk", "Log config JSON parsing failed: %s", error.c_str());
      return;
    }

    const char* target = doc[JSON_KEY_TARGET]; // "serial" or "mqtt"
    const char* levelStr = doc[JSON_KEY_LEVEL];  // "NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG"

    if (target && levelStr) {
      LogLevel newLevel = LOG_LEVEL_NONE; // Default
      if (strcmp(levelStr, JSON_KEY_CRITICAL) == 0) newLevel = LOG_LEVEL_CRITICAL;
      else if (strcmp(levelStr, JSON_KEY_ERROR) == 0) newLevel = LOG_LEVEL_ERROR;
      else if (strcmp(levelStr, JSON_KEY_WARNING) == 0) newLevel = LOG_LEVEL_WARNING;
      else if (strcmp(levelStr, JSON_KEY_INFO) == 0) newLevel = LOG_LEVEL_INFO;
      else if (strcmp(levelStr, JSON_KEY_DEBUG) == 0) newLevel = LOG_LEVEL_DEBUG;

      if (strcmp(target, JSON_KEY_SERIAL) == 0) {
        AppLogger.setSerialLogLevel(newLevel);
        // AppLogger.setSerialLogLevel already logs this change
      } else if (strcmp(target, JSON_KEY_MQTT) == 0) {
        AppLogger.setMqttLogLevel(newLevel);
        // AppLogger.setMqttLogLevel already logs this change
      } else {
        AppLogger.logf(LOG_LEVEL_WARNING, "MQTTCallbk", "Invalid log config target: %s", target);
      }
    } else {
      AppLogger.warning("MQTTCallbk", "Log config command missing 'target' or 'level' field.");
    }
  }
}

// Core 0 Task: Handles sensor readings, network communication, MQTT, and scheduled task updates.
// This task is preemptive.
void Core0TaskCode(void * parameter) {
  AppLogger.logf(LOG_LEVEL_INFO, "Core0", "Task started on core %d", xPortGetCoreID());
  
  for(;;) {
    // Maintain network connection
    networkManager.loop();
    
    unsigned long currentTime = millis();
    
    // Read data from sensors and send to MQTT server
    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
      lastSensorReadTime = currentTime;
      
      // Ensure exclusive access to sensor data
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
        // Performance Logging: Sensor Read Operation
        unsigned long sensorReadStartTime = millis();
        bool readSuccess = false;

        // Read data from sensors
        if (sensorManager.readSensors()) {
          readSuccess = true;
          
          // Update EnvironmentManager with latest sensor readings
          envManager.setCurrentTemperature(sensorManager.getTemperature());
          envManager.setCurrentHumidity(sensorManager.getHumidity());
          envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());
          
          // Log sensor data
          AppLogger.logf(LOG_LEVEL_DEBUG, "Core0", "Sensors read: T=%.2f°C, H=%.2f%%, HI=%.2f°C", 
                           sensorManager.getTemperature(), sensorManager.getHumidity(), sensorManager.getHeatIndex());
          
          // Send data to MQTT server immediately after reading
          if (networkManager.isConnected()) {
            // Create JSON payload
            String payload = sensorManager.getJsonPayload(API_KEY);
            
            // Performance Logging: MQTT Sensor Data Publish
            unsigned long mqttPublishStartTime = millis();
            bool mqttSuccess = networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
            unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
            AppLogger.perf("Core0", "MQTTSensorDataPublish", mqttPublishDuration, mqttSuccess);
            
            AppLogger.debug("Core0", "Sensor data published to MQTT");
          } else {
            AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT");
          }
        } else {
          AppLogger.error("Core0", "Failed to read from sensors");
          readSuccess = false;
        }
        
        // Performance Logging: End Sensor Read Operation
        unsigned long sensorReadDuration = millis() - sensorReadStartTime;
        AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);
        
        // Release mutex
        xSemaphoreGive(sensorDataMutex);
      }
    }
    
    // Publish relay and scheduler status if changed or for a forced periodic report
    if (networkManager.isConnected()) {
      bool forcedReport = (currentTime - lastForcedStatusReportTime >= forcedStatusReportInterval);
      
      // Publish relay status if changed or forced report
      if (relayManager.hasStatusChangedAndReset() || forcedReport) {
        String statusPayload = relayManager.getStatusJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
        if (forcedReport) {
          AppLogger.debug("Core0", "Relay status published to MQTT (forced report)");
        } else {
          AppLogger.debug("Core0", "Relay status published to MQTT");
        }
      }
      
      // Publish schedule status if changed or forced report
      if (taskScheduler.hasScheduleStatusChangedAndReset() || forcedReport) {
        String schedulePayload = taskScheduler.getTasksJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
        if (forcedReport) {
          AppLogger.debug("Core0", "Schedule status published to MQTT (forced report)");
        } else {
          AppLogger.debug("Core0", "Schedule status published to MQTT");
        }
      }
      
      // Update last forced report time if a forced report was sent
      if (forcedReport) {
        lastForcedStatusReportTime = currentTime;
      }
    }
    
    // Check and update irrigation schedules based on their next check time
    time_t current_time_for_scheduler;
    time(&current_time_for_scheduler);
    time_t next_check = taskScheduler.getEarliestNextCheckTime();
    
    // If no schedule, or if current time is past the next check time
    if (next_check == 0 || current_time_for_scheduler >= next_check) {
      taskScheduler.update();
    }
    
    // Blink LED to indicate system status and activity
    if (currentTime - lastLedBlinkTime >= ledBlinkInterval) {
      lastLedBlinkTime = currentTime;
      ledState = !ledState; 

      if (networkManager.isConnected()) {
        // Green LED: Connected to WiFi and MQTT
        if (ledState) {
          RGB_Light(0, 20, 0);  // Low brightness green
        } else {
          RGB_Light(0, 0, 0);
        }
      } else if (networkManager.isAttemptingWifiReconnect() || networkManager.isAttemptingMqttReconnect()) {
        // Blue LED: Attempting to reconnect WiFi or MQTT
        if (ledState) {
          RGB_Light(0, 0, 20);  // Low brightness blue
        } else {
          RGB_Light(0, 0, 0);
        }
      } else if (!networkManager.isWifiConnected()){
        // Red LED: WiFi disconnected (and not actively reconnecting)
        if (ledState) {
          RGB_Light(20, 0, 0); // Low brightness red
        } else {
          RGB_Light(0, 0, 0);
        }
      } else { // WiFi connected, but MQTT is not (and not actively reconnecting)
        // Yellow LED: MQTT disconnected (WiFi is connected)
         if (ledState) {
          RGB_Light(20, 20, 0); // Low brightness yellow
        } else {
          RGB_Light(0, 0, 0);
        }
      }
    }
    
    // Small delay to yield to other tasks and prevent Watchdog Timer (WDT) reset
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Core 1 Task: Handles events from the relay timer queue.
// This task is event-driven, blocking on xQueueReceive.
void Core1TaskCode(void * parameter) {
  AppLogger.logf(LOG_LEVEL_INFO, "Core1", "Task bắt đầu trên core %d", xPortGetCoreID());
  RelayTimerEvent_t receivedEvent;

  for(;;) {
    // Wait for an event from the g_relayEventQueue
    if (xQueueReceive(g_relayEventQueue, &receivedEvent, portMAX_DELAY) == pdPASS) {
      AppLogger.logf(LOG_LEVEL_INFO, "Core1", "Đã nhận sự kiện hết hạn timer cho relay index: %d", receivedEvent.relayIndex);
      // RelayManager turns off the relay.
      // Core0Task will detect the status change and publish MQTT update.
      relayManager.turnOff(receivedEvent.relayIndex);
    }
    // Task blocks on xQueueReceive, no explicit vTaskDelay needed here.
  }
}

// Preferences object for Non-Volatile Storage (NVS)
Preferences preferences;

void setup() {
  Serial.begin(115200);
  
  // Wait for Serial to initialize to ensure boot messages are visible
  uint32_t serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 2000)) { // Max 2 seconds wait
    delay(10);
  }
  Serial.println(F("\n\nMain: Serial port initialized.")); // F() macro saves RAM

  // TODO: Consider implementing NVS for WiFi/MQTT credentials
  // preferences.begin("app-config", false); 
  // String storedSsid = preferences.getString("wifi_ssid", WIFI_SSID);
  // ... and so on for other credentials
  // preferences.end();
  // Then use stored values in networkManager.begin()

  // Initialize queue for relay timer events
  g_relayEventQueue = xQueueCreate(10, sizeof(RelayTimerEvent_t)); // Queue size of 10
  if (g_relayEventQueue == NULL) {
      Serial.println(F("Main: LỖI - Không thể tạo relay event queue!")); // Error during critical setup
      AppLogger.critical("Setup", "FATAL - Failed to create relay event queue!"); // Log before restart
      ESP.restart(); // Restart on critical failure
  } else {
      Serial.println(F("Main: Relay event queue đã được tạo."));
  }

  // Initialize NetworkManager first. It attempts WiFi/MQTT connection.
  // Reconnection attempts are handled by NetworkManager::loop() in Core0Task.
  if (!networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT)) {
    AppLogger.error("Setup", "NetworkManager failed to initialize properly. System will attempt to reconnect.");
    // LED status reflecting connection state is handled in Core0Task.
  } else {
    AppLogger.info("Setup", "NetworkManager initialized. Attempting to connect...");
  }

  // Initialize Logger after NetworkManager (Logger might use MQTT).
  AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO); // Default log levels
  
  // Set the MQTT callback function
  networkManager.setCallback(mqttCallback);
  AppLogger.info("Setup", "MQTT Callback function set.");

  AppLogger.info("Setup", "System setup sequence started.");
  AppLogger.info("Setup", "ESP32-S3 Dual-Core Irrigation System");

  // Create mutex for shared sensor data
  sensorDataMutex = xSemaphoreCreateMutex();
  
  // Initialize GPIO
  AppLogger.debug("Setup", "Initializing GPIO...");
  GPIO_Init();
  AppLogger.info("Setup", "GPIO initialized");
  
  // Initialize relay manager
  AppLogger.debug("Setup", "Đang khởi tạo RelayManager...");
  relayManager.begin(relayPins, numRelays, g_relayEventQueue); // Pass the event queue
  
  // Initialize task scheduler
  AppLogger.debug("Setup", "Đang khởi tạo TaskScheduler...");
  taskScheduler.begin();
  
  // Initialize sensors
  AppLogger.debug("Setup", "Initializing SensorManager...");
  sensorManager.begin();
  
  // NetworkManager handles subscriptions internally upon successful MQTT connection.
  // No need to call subscribe directly here after initial setup.
  networkManager.subscribe(MQTT_TOPIC_CONTROL);
  networkManager.subscribe(MQTT_TOPIC_SCHEDULE);
  networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL);
  networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG);

  // LED and Buzzer status indicators are managed in Core0TaskCode based on networkManager.isConnected().
  // Direct signal commands here are removed to avoid conflicts.

  // Create tasks and pin them to specific cores
  AppLogger.info("Setup", "Creating and pinning tasks to cores...");
  xTaskCreatePinnedToCore(
    Core0TaskCode, "Core0Task", STACK_SIZE_CORE0, NULL, PRIORITY_MEDIUM, &core0Task, 0);
  xTaskCreatePinnedToCore(
    Core1TaskCode, "Core1Task", STACK_SIZE_CORE1, NULL, PRIORITY_MEDIUM, &core1Task, 1);
    
  AppLogger.info("Setup", "System setup sequence completed. Tasks are running.");
  AppLogger.info("Setup", "---------------- SYSTEM READY ----------------");
}

void loop() {
  // The main loop is intentionally empty as all operations are handled by FreeRTOS tasks.
  delay(1000); // Minimal delay to keep loop() responsive if needed in future.

  // Periodically log Stack High Water Mark (HWM) for task memory usage monitoring.
  // This helps in optimizing task stack sizes.
  static unsigned long lastStackCheckTime = 0;
  if (millis() - lastStackCheckTime > 60000) { // Check every 60 seconds
    lastStackCheckTime = millis();
    UBaseType_t core0StackHWM = uxTaskGetStackHighWaterMark(core0Task);
    UBaseType_t core1StackHWM = uxTaskGetStackHighWaterMark(core1Task);
    AppLogger.logf(LOG_LEVEL_INFO, "StackCheck", "Core0Task HWM: %u words (%u bytes)", core0StackHWM, core0StackHWM * sizeof(StackType_t));
    AppLogger.logf(LOG_LEVEL_INFO, "StackCheck", "Core1Task HWM: %u words (%u bytes)", core1StackHWM, core1StackHWM * sizeof(StackType_t));
    // Note: "words" here refers to StackType_t, typically 4 bytes on ESP32.
    // Use these HWM values to fine-tune STACK_SIZE_CORE0 and STACK_SIZE_CORE1.
    // E.g., if HWM is 1000 words (4000 bytes) and STACK_SIZE is 8192 bytes, STACK_SIZE can be reduced.
  }
}