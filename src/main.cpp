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
#include <vector> // Required for std::vector

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
#define APP_TASK_PRIORITY_LOW    (tskIDLE_PRIORITY + 5)
#define APP_TASK_PRIORITY_MEDIUM (tskIDLE_PRIORITY + 10)
#define APP_TASK_PRIORITY_HIGH   (tskIDLE_PRIORITY + 15)

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

// Number of relays - Calculated dynamically
const int numRelays = sizeof(relayPins) / sizeof(relayPins[0]);

// WiFi and MQTT configuration
const char* WIFI_SSID = "karis";
const char* WIFI_PASSWORD = "12123402";
const char* MQTT_SERVER = "karis.cloud";
const int MQTT_PORT = 1883;
const char* API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2";

// MQTT topics
const char* MQTT_TOPIC_SENSORS = "irrigation/esp32_6relay/sensors";
const char* MQTT_TOPIC_CONTROL = "irrigation/esp32_6relay/control";
const char* MQTT_TOPIC_STATUS = "irrigation/esp32_6relay/status";
const char* MQTT_TOPIC_SCHEDULE = "irrigation/esp32_6relay/schedule";
const char* MQTT_TOPIC_SCHEDULE_STATUS = "irrigation/esp32_6relay/schedule/status";
const char* MQTT_TOPIC_ENV_CONTROL = "irrigation/esp32_6relay/environment";
const char* MQTT_TOPIC_LOG_CONFIG = "irrigation/esp32_6relay/logconfig";

// NTP configuration
std::vector<const char*> ntpServerList = {
    "0.vn.pool.ntp.org", "1.vn.pool.ntp.org", "2.vn.pool.ntp.org",
    "0.asia.pool.ntp.org", "1.asia.pool.ntp.org", "2.asia.pool.ntp.org",
    "time.google.com", "pool.ntp.org",
    "1.ntp.vnix.vn", "2.ntp.vnix.vn"
};
const char* TZ_INFO = "Asia/Ho_Chi_Minh";

// Sensor and manager objects
SensorManager sensorManager;
NetworkManager networkManager;
RelayManager relayManager;
EnvironmentManager envManager(sensorManager);
TaskScheduler taskScheduler(relayManager, envManager);

// Time tracking variables
unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 30000;
unsigned long lastForcedStatusReportTime = 0;
const unsigned long forcedStatusReportInterval = 5 * 60 * 1000;
unsigned long lastEnvUpdateTime = 0;
const unsigned long envUpdateInterval = 2000;

// LED status tracking
bool ledState = false;
unsigned long lastLedBlinkTime = 0;
const unsigned long ledBlinkInterval = 1000;

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
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  AppLogger.logf(LOG_LEVEL_DEBUG, "MQTTCallbk", "Received MQTT message on topic: %s", topic);
  AppLogger.logf(LOG_LEVEL_DEBUG, "MQTTCallbk", "Payload: %s", message);

  if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
    relayManager.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_SCHEDULE) == 0) {
    taskScheduler.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_ENV_CONTROL) == 0) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      AppLogger.logf(LOG_LEVEL_ERROR, "MQTTCallbk", "JSON parsing failed: %s", error.c_str());
      return;
    }
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
    AppLogger.logf(LOG_LEVEL_INFO, "MQTTCallbk", "Received log configuration command. Payload: %s", message);
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      AppLogger.logf(LOG_LEVEL_ERROR, "MQTTCallbk", "Log config JSON parsing failed: %s", error.c_str());
      return;
    }
    const char* target = doc[JSON_KEY_TARGET];
    const char* levelStr = doc[JSON_KEY_LEVEL];
    if (target && levelStr) {
      LogLevel newLevel = LOG_LEVEL_NONE;
      if (strcmp(levelStr, JSON_KEY_CRITICAL) == 0) newLevel = LOG_LEVEL_CRITICAL;
      else if (strcmp(levelStr, JSON_KEY_ERROR) == 0) newLevel = LOG_LEVEL_ERROR;
      else if (strcmp(levelStr, JSON_KEY_WARNING) == 0) newLevel = LOG_LEVEL_WARNING;
      else if (strcmp(levelStr, JSON_KEY_INFO) == 0) newLevel = LOG_LEVEL_INFO;
      else if (strcmp(levelStr, JSON_KEY_DEBUG) == 0) newLevel = LOG_LEVEL_DEBUG;

      if (strcmp(target, JSON_KEY_SERIAL) == 0) {
        AppLogger.setSerialLogLevel(newLevel);
      } else if (strcmp(target, JSON_KEY_MQTT) == 0) {
        AppLogger.setMqttLogLevel(newLevel);
      } else {
        AppLogger.logf(LOG_LEVEL_WARNING, "MQTTCallbk", "Invalid log config target: %s", target);
      }
    } else {
      AppLogger.warning("MQTTCallbk", "Log config command missing 'target' or 'level' field.");
    }
  }
}

// Core 0 Task: Handles sensor readings, network communication, MQTT, and scheduled task updates.
void Core0TaskCode(void * parameter) {
  AppLogger.logf(LOG_LEVEL_INFO, "Core0", "Task started on core %d", xPortGetCoreID());
  static bool mqttPreviouslyConnected = false;

  for(;;) {
    networkManager.loop(); // Handles WiFi/MQTT connection and reconnection

    bool mqttCurrentlyConnected = networkManager.isConnected();
    if (mqttCurrentlyConnected && !mqttPreviouslyConnected) {
      AppLogger.info("Core0", "MQTT (re)connected. Re-subscribing to topics...");
      networkManager.subscribe(MQTT_TOPIC_CONTROL);
      networkManager.subscribe(MQTT_TOPIC_SCHEDULE);
      networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL);
      networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG);
    }
    mqttPreviouslyConnected = mqttCurrentlyConnected;

    unsigned long currentTime = millis();

    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
      lastSensorReadTime = currentTime;
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        unsigned long sensorReadStartTime = millis();
        bool readSuccess = false;
        if (sensorManager.readSensors()) {
          readSuccess = true;
          envManager.setCurrentTemperature(sensorManager.getTemperature());
          envManager.setCurrentHumidity(sensorManager.getHumidity());
          envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());

          AppLogger.logf(LOG_LEVEL_DEBUG, "Core0", "Sensors read: T=%.2f°C, H=%.2f%%, HI=%.2f°C, Soil=%.2f%%",
                           sensorManager.getTemperature(), sensorManager.getHumidity(), sensorManager.getHeatIndex(), sensorManager.getSoilMoisture());

          if (networkManager.isConnected()) {
            String payload = sensorManager.getJsonPayload(API_KEY);
            unsigned long mqttPublishStartTime = millis();
            bool mqttSuccess = networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
            unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
            AppLogger.perf("Core0", "MQTTSensorDataPublish", mqttPublishDuration, mqttSuccess);
            if(mqttSuccess) AppLogger.debug("Core0", "Sensor data published to MQTT");
            else AppLogger.warning("Core0", "Failed to publish sensor data to MQTT");
          } else {
            AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT");
          }
        } else {
          AppLogger.error("Core0", "Failed to read from sensors");
          readSuccess = false;
        }
        unsigned long sensorReadDuration = millis() - sensorReadStartTime;
        AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);
        xSemaphoreGive(sensorDataMutex);
      }
    }

    if (networkManager.isConnected()) {
      bool forcedReport = (currentTime - lastForcedStatusReportTime >= forcedStatusReportInterval);
      if (relayManager.hasStatusChangedAndReset() || forcedReport) {
        String statusPayload = relayManager.getStatusJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
        AppLogger.debug("Core0", forcedReport ? "Relay status published (forced)" : "Relay status published");
      }
      if (taskScheduler.hasScheduleStatusChangedAndReset() || forcedReport) {
        String schedulePayload = taskScheduler.getTasksJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
        AppLogger.debug("Core0", forcedReport ? "Schedule status published (forced)" : "Schedule status published");
      }
      if (forcedReport) {
        lastForcedStatusReportTime = currentTime;
      }
    }

    // *** CHỈNH SỬA QUAN TRỌNG Ở ĐÂY ***
    // Luôn gọi taskScheduler.update().
    // TaskScheduler::update() có cơ chế giới hạn tần suất chạy bằng millis() bên trong (thường là 1 giây).
    taskScheduler.update();
    // *** KẾT THÚC CHỈNH SỬA QUAN TRỌNG ***


    if (currentTime - lastLedBlinkTime >= ledBlinkInterval) {
      lastLedBlinkTime = currentTime;
      ledState = !ledState;
      if (networkManager.isConnected()) {
        RGB_Light(0, ledState ? 20 : 0, 0); // Green
      } else if (networkManager.isAttemptingWifiReconnect() || networkManager.isAttemptingMqttReconnect()) {
        RGB_Light(0, 0, ledState ? 20 : 0); // Blue
      } else if (!networkManager.isWifiConnected()){
        RGB_Light(ledState ? 20 : 0, 0, 0); // Red
      } else {
        RGB_Light(ledState ? 20 : 0, ledState ? 20 : 0, 0); // Yellow
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); // Yield to other tasks
  }
}

// Core 1 Task: Handles events from the relay timer queue.
void Core1TaskCode(void * parameter) {
  AppLogger.logf(LOG_LEVEL_INFO, "Core1", "Task bắt đầu trên core %d", xPortGetCoreID());
  RelayTimerEvent_t receivedEvent;

  for(;;) {
    if (xQueueReceive(g_relayEventQueue, &receivedEvent, portMAX_DELAY) == pdPASS) {
      AppLogger.logf(LOG_LEVEL_INFO, "Core1", "Đã nhận sự kiện hết hạn timer cho relay index: %d", receivedEvent.relayIndex);
      relayManager.turnOff(receivedEvent.relayIndex);
    }
  }
}

Preferences preferences; // For NVS

void setup() {
  Serial.begin(115200);
  uint32_t serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 2000)) { delay(10); }
  Serial.println(F("\n\nMain: Serial port initialized."));

  g_relayEventQueue = xQueueCreate(10, sizeof(RelayTimerEvent_t));
  if (g_relayEventQueue == NULL) {
      Serial.println(F("Main: LỖI - Không thể tạo relay event queue!"));
      AppLogger.critical("Setup", "FATAL - Failed to create relay event queue!");
      ESP.restart();
  } else {
      Serial.println(F("Main: Relay event queue đã được tạo."));
  }

  AppLogger.begin(nullptr, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO);
  AppLogger.info("Setup", "Logger initialized.");

  if (!networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT)) {
    AppLogger.error("Setup", "NetworkManager failed to initialize properly. System will attempt to reconnect.");
  } else {
    AppLogger.info("Setup", "NetworkManager initialized. Attempting to connect...");
  }

  AppLogger.begin(&networkManager, AppLogger.getSerialLogLevel(), AppLogger.getMqttLogLevel());
  AppLogger.info("Setup", "AppLogger re-initialized with NetworkManager link for MQTT logs.");

  networkManager.setCallback(mqttCallback);
  AppLogger.info("Setup", "MQTT Callback function set.");

  AppLogger.info("Setup", "System setup sequence started.");
  AppLogger.info("Setup", "ESP32-S3 Dual-Core Irrigation System");

  sensorDataMutex = xSemaphoreCreateMutex();
   if (sensorDataMutex == NULL) {
      Serial.println(F("Main: FATAL ERROR - Failed to create sensorDataMutex!"));
      AppLogger.critical("Setup", "FATAL - Failed to create sensorDataMutex!");
      ESP.restart();
  }


  AppLogger.debug("Setup", "Initializing GPIO...");
  GPIO_Init();
  AppLogger.info("Setup", "GPIO initialized");

  AppLogger.debug("Setup", "Đang khởi tạo RelayManager...");
  relayManager.begin(relayPins, numRelays, g_relayEventQueue);

  AppLogger.debug("Setup", "Đang khởi tạo TaskScheduler...");
  taskScheduler.begin();

  AppLogger.debug("Setup", "Initializing SensorManager...");
  sensorManager.begin();

  AppLogger.info("Setup", "Creating and pinning tasks to cores...");
  xTaskCreatePinnedToCore(
    Core0TaskCode, "Core0Task", STACK_SIZE_CORE0, NULL, APP_TASK_PRIORITY_MEDIUM, &core0Task, 0);
  xTaskCreatePinnedToCore(
    Core1TaskCode, "Core1Task", STACK_SIZE_CORE1, NULL, APP_TASK_PRIORITY_MEDIUM, &core1Task, 1);

  AppLogger.info("Setup", "System setup sequence completed. Tasks are running.");
  AppLogger.info("Setup", "---------------- SYSTEM READY ----------------");
}

void loop() {
  delay(1000);
  static unsigned long lastStackCheckTime = 0;
  if (millis() - lastStackCheckTime > 60000) {
    lastStackCheckTime = millis();
    if(core0Task != NULL) { // Check if task handle is valid
        UBaseType_t core0StackHWM = uxTaskGetStackHighWaterMark(core0Task);
        AppLogger.logf(LOG_LEVEL_INFO, "StackCheck", "Core0Task HWM: %u words (%u bytes)", core0StackHWM, core0StackHWM * sizeof(StackType_t));
    }
    if(core1Task != NULL) { // Check if task handle is valid
        UBaseType_t core1StackHWM = uxTaskGetStackHighWaterMark(core1Task);
        AppLogger.logf(LOG_LEVEL_INFO, "StackCheck", "Core1Task HWM: %u words (%u bytes)", core1StackHWM, core1StackHWM * sizeof(StackType_t));
    }
  }
}
