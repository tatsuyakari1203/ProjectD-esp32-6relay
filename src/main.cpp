#include <Arduino.h>
#include "../include/WS_GPIO.h"
#include "../include/SensorManager.h"
#include "../include/NetworkManager.h"
#include "../include/RelayManager.h"
#include "../include/TaskScheduler.h"
#include "../include/EnvironmentManager.h"
#include "../include/Logger.h"
#include <time.h>

// Function prototypes
void Core0TaskCode(void * parameter);
void Core1TaskCode(void * parameter);
void printLocalTime();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Core task definitions
TaskHandle_t core0Task;  // Preemptive tasks: sensors, MQTT, scheduling
TaskHandle_t core1Task;  // Non-preemptive tasks: irrigation control

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
const char* WIFI_SSID = "2.4 KariS";  // Replace with your WiFi name
const char* WIFI_PASSWORD = "12123402";  // Replace with your WiFi password
const char* MQTT_SERVER = "karis.cloud";
const int MQTT_PORT = 1883;
const char* API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2";  // Provided API key

// MQTT topic configuration
const char* MQTT_TOPIC_SENSORS = "irrigation/esp32_6relay/sensors";
const char* MQTT_TOPIC_CONTROL = "irrigation/esp32_6relay/control";
const char* MQTT_TOPIC_STATUS = "irrigation/esp32_6relay/status";
const char* MQTT_TOPIC_SCHEDULE = "irrigation/esp32_6relay/schedule";
const char* MQTT_TOPIC_SCHEDULE_STATUS = "irrigation/esp32_6relay/schedule/status";
const char* MQTT_TOPIC_ENV_CONTROL = "irrigation/esp32_6relay/environment";

// Add a new MQTT topic for log configuration
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
const unsigned long sensorReadInterval = 5000;  // Read sensors and send data every 5 seconds
unsigned long lastForcedStatusReportTime = 0;
const unsigned long forcedStatusReportInterval = 5 * 60 * 1000;  // Force status update every 5 minutes
unsigned long lastEnvUpdateTime = 0;
const unsigned long envUpdateInterval = 2000;  // Update environment readings every 2 seconds

// LED status tracking
bool ledState = false;
unsigned long lastLedBlinkTime = 0;
const unsigned long ledBlinkInterval = 1000;  // Blink LED every 1 second

// Semaphores for safe access to shared resources
SemaphoreHandle_t sensorDataMutex;

// Function to display current time
void printLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    AppLogger.error("Time", "Could not get time from NTP");
    return;
  }
  
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  AppLogger.info("Time", "Current time: " + String(timeString) + 
                ", Timezone: " + String(TZ_INFO) + 
                " (Day of week: " + String(timeinfo.tm_wday) + ")");
}

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  AppLogger.debug("MQTTCallbk", "Received MQTT message on topic: " + String(topic));
  AppLogger.debug("MQTTCallbk", "Payload: " + String(message));
  
  // Process message based on topic
  if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
    // Process relay control command - không publish ngay, để do phát hiện thay đổi
    relayManager.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_SCHEDULE) == 0) {
    // Process scheduling command - không publish ngay, để do phát hiện thay đổi
    taskScheduler.processCommand(message);
  }
  else if (strcmp(topic, MQTT_TOPIC_ENV_CONTROL) == 0) {
    // Process environment control command
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      AppLogger.error("MQTTCallbk", "JSON parsing failed: " + String(error.c_str()));
      return;
    }
    
    // Xử lý cập nhật giá trị cảm biến thủ công
    if (doc.containsKey("soil_moisture")) {
      JsonObject soil = doc["soil_moisture"];
      int zone = soil["zone"];
      float value = soil["value"];
      envManager.setSoilMoisture(zone, value);
      AppLogger.info("MQTTCallbk", "Manual soil moisture update: Zone " + String(zone) + ", Value: " + String(value));
    }
    
    if (doc.containsKey("rain")) {
      bool isRaining = doc["rain"];
      envManager.setRainStatus(isRaining);
      AppLogger.info("MQTTCallbk", "Manual rain status update: " + String(isRaining ? "Raining" : "Not raining"));
    }
    
    if (doc.containsKey("light")) {
      int lightLevel = doc["light"];
      envManager.setLightLevel(lightLevel);
      AppLogger.info("MQTTCallbk", "Manual light level update: " + String(lightLevel));
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_LOG_CONFIG) == 0) {
    // Process log configuration command
    AppLogger.info("MQTTCallbk", "Received log configuration command. Payload: " + String(message));

    DynamicJsonDocument doc(256); // Small payload for config
    DeserializationError error = deserializeJson(doc, message);

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
        // AppLogger.setSerialLogLevel already logs this change
      } else if (strcmp(target, "mqtt") == 0) {
        AppLogger.setMqttLogLevel(newLevel);
        // AppLogger.setMqttLogLevel already logs this change
      } else {
        AppLogger.warning("MQTTCallbk", "Invalid log config target: " + String(target));
      }
    } else {
      AppLogger.warning("MQTTCallbk", "Log config command missing 'target' or 'level' field.");
    }
  }
}

// Core 0 Task - Handles sensors, network, MQTT (preemptive)
void Core0TaskCode(void * parameter) {
  AppLogger.info("Core0", "Task started on core " + String(xPortGetCoreID()));
  
  for(;;) {
    // Maintain network connection
    networkManager.loop();
    
    unsigned long currentTime = millis();
    
    // Read data from sensors and send to MQTT server
    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
      lastSensorReadTime = currentTime;
      
      // Take mutex before accessing sensor data
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
        // Read data from sensors
        if (sensorManager.readSensors()) {
          // Cập nhật ngay vào EnvironmentManager
          envManager.setCurrentTemperature(sensorManager.getTemperature());
          envManager.setCurrentHumidity(sensorManager.getHumidity());
          envManager.setCurrentHeatIndex(sensorManager.getHeatIndex());
          
          // Log sensor data
          AppLogger.debug("Core0", "Sensors read: T=" + String(sensorManager.getTemperature()) +
                          "°C, H=" + String(sensorManager.getHumidity()) +
                          "%, HI=" + String(sensorManager.getHeatIndex()) + "°C");
          
          // Send data to MQTT server immediately after reading
          if (networkManager.isConnected()) {
            // Create JSON payload
            String payload = sensorManager.getJsonPayload(API_KEY);
            
            // Send to MQTT server
            networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
            AppLogger.debug("Core0", "Sensor data published to MQTT");
          } else {
            AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT");
            
            // Red LED blink when no connection
            RGB_Light(255, 0, 0);
            delay(100);
            RGB_Light(0, 0, 0);
          }
        } else {
          AppLogger.error("Core0", "Failed to read from sensors");
        }
        
        // Release mutex after accessing sensor data
        xSemaphoreGive(sensorDataMutex);
      }
    }
    
    // Update environment manager (cho các logic khác ngoài việc lấy từ DHT)
    if (currentTime - lastEnvUpdateTime >= envUpdateInterval) {
      lastEnvUpdateTime = currentTime;
      envManager.update();
    }
    
    // Publish relay and scheduler status khi có thay đổi hoặc cần gửi dự phòng
    if (networkManager.isConnected()) {
      bool forcedReport = (currentTime - lastForcedStatusReportTime >= forcedStatusReportInterval);
      
      // Publish relay status khi có thay đổi hoặc gửi dự phòng
      if (relayManager.hasStatusChangedAndReset() || forcedReport) {
        String statusPayload = relayManager.getStatusJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
        if (forcedReport) {
          AppLogger.debug("Core0", "Relay status published to MQTT (forced report)");
        } else {
          AppLogger.debug("Core0", "Relay status published to MQTT");
        }
      }
      
      // Publish schedule status khi có thay đổi hoặc gửi dự phòng
      if (taskScheduler.hasScheduleStatusChangedAndReset() || forcedReport) {
        String schedulePayload = taskScheduler.getTasksJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
        if (forcedReport) {
          AppLogger.debug("Core0", "Schedule status published to MQTT (forced report)");
        } else {
          AppLogger.debug("Core0", "Schedule status published to MQTT");
        }
      }
      
      // Cập nhật thời gian gửi dự phòng nếu đã gửi dự phòng
      if (forcedReport) {
        lastForcedStatusReportTime = currentTime;
      }
    }
    
    // Check and update irrigation schedules - Event-driven approach
    time_t current_time_for_scheduler;
    time(&current_time_for_scheduler);
    time_t next_check = taskScheduler.getEarliestNextCheckTime();
    
    if (next_check == 0 || current_time_for_scheduler >= next_check) {
      // Nếu next_check là 0 (chưa có lịch, hoặc cần tính toán lại lần đầu)
      // hoặc đã đến/qua thời điểm kiểm tra
      taskScheduler.update();
    }
    
    // Blink LED to show activity status
    if (currentTime - lastLedBlinkTime >= ledBlinkInterval) {
      lastLedBlinkTime = currentTime;
      
      if (networkManager.isConnected()) {
        // Blink green LED when connected
        ledState = !ledState;
        if (ledState) {
          RGB_Light(0, 20, 0);  // Low brightness to save power
        } else {
          RGB_Light(0, 0, 0);
        }
      } else {
        // Blink red LED when not connected
        ledState = !ledState;
        if (ledState) {
          RGB_Light(20, 0, 0);  // Low brightness to save power
        } else {
          RGB_Light(0, 0, 0);
        }
      }
    }
    
    // Small delay to prevent WDT reset
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Core 1 Task - Handles irrigation control (non-preemptive)
void Core1TaskCode(void * parameter) {
  AppLogger.info("Core1", "Task started on core " + String(xPortGetCoreID()));
  
  for(;;) {
    // Update relay manager to handle timer-based relay control
    relayManager.update();
    
    // This is a non-preemptive task, so we can have longer delay
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  // Initialize Serial as early as possible
  Serial.begin(115200);
  
  // Wait a bit for Serial to ensure initialization logs can be seen
  uint32_t serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 2000)) { // Wait a maximum of 2 seconds
    delay(10);
  }
  Serial.println(F("\n\nMain: Serial port initialized.")); // F() to save RAM

  // Initialize NetworkManager (important for MQTT logging)
  Serial.println(F("Main: Initializing NetworkManager..."));
  bool networkInitSuccess = networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT);
  if (networkInitSuccess) {
    Serial.println(F("Main: NetworkManager initialization successful (WiFi connected, MQTT attempt)."));
  } else {
    Serial.println(F("Main: ERROR - NetworkManager initialization failed (WiFi or MQTT connection problem)."));
  }

  // Initialize Logger AFTER NetworkManager has been initialized
  // Initial log levels: DEBUG for Serial (for development), INFO for MQTT
  AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO);
  
  AppLogger.info("Setup", "System setup sequence started.");
  AppLogger.info("Setup", "ESP32-S3 Dual-Core Irrigation System");

  // Create semaphore
  sensorDataMutex = xSemaphoreCreateMutex();
  
  // Initialize GPIO
  AppLogger.debug("Setup", "Initializing GPIO...");
  GPIO_Init();
  AppLogger.info("Setup", "GPIO initialized");
  
  // Initialize relay manager
  AppLogger.debug("Setup", "Initializing RelayManager...");
  relayManager.begin(relayPins, numRelays);
  
  // Initialize task scheduler
  AppLogger.debug("Setup", "Initializing TaskScheduler...");
  taskScheduler.begin();
  
  // Initialize sensors
  AppLogger.debug("Setup", "Initializing SensorManager...");
  sensorManager.begin();
  
  // Configure NTP and MQTT subscriptions if network is connected
  if (networkManager.isConnected()) {
    AppLogger.info("Setup", "Configuring NTP and MQTT subscriptions...");
    configTime(7 * 3600, 0, NTP_SERVER); // GMT+7 for Vietnam
    setenv("TZ", TZ_INFO, 1);
    tzset();
    AppLogger.info("Setup", "NTP Server configured: " + String(NTP_SERVER) + " with timezone " + String(TZ_INFO));

    networkManager.setCallback(mqttCallback);
    if (networkManager.subscribe(MQTT_TOPIC_CONTROL)) {
      AppLogger.info("Setup", "Subscribed to control topic: " + String(MQTT_TOPIC_CONTROL));
    } else { 
      AppLogger.error("Setup", "Failed to subscribe to control topic."); 
    }
    
    if (networkManager.subscribe(MQTT_TOPIC_SCHEDULE)) {
      AppLogger.info("Setup", "Subscribed to schedule topic: " + String(MQTT_TOPIC_SCHEDULE));
    } else { 
      AppLogger.error("Setup", "Failed to subscribe to schedule topic."); 
    }
    
    if (networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL)) {
      AppLogger.info("Setup", "Subscribed to environment control topic: " + String(MQTT_TOPIC_ENV_CONTROL));
    } else { 
      AppLogger.error("Setup", "Failed to subscribe to environment control topic."); 
    }
    
    // Subscribe to log configuration topic
    if (networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG)) {
      AppLogger.info("Setup", "Subscribed to log config topic: " + String(MQTT_TOPIC_LOG_CONFIG));
    } else {
      AppLogger.error("Setup", "Failed to subscribe to log config topic.");
    }
    
    // Green LED to indicate successful connection
    RGB_Light(0, 255, 0);
    delay(1000);
    RGB_Light(0, 0, 0);
    
    // Beep to indicate startup completed
    Buzzer_PWM(300);
  } else {
    AppLogger.warning("Setup", "Network disconnected. Skipping NTP/MQTT subscriptions.");
    
    // Red LED to indicate connection failure
    RGB_Light(255, 0, 0);
    delay(1000);
    RGB_Light(0, 0, 0);
  }
  
  // Create tasks and pin to cores
  AppLogger.info("Setup", "Creating and pinning tasks to cores...");
  xTaskCreatePinnedToCore(
    Core0TaskCode, "Core0Task", STACK_SIZE_CORE0, NULL, PRIORITY_MEDIUM, &core0Task, 0);
  xTaskCreatePinnedToCore(
    Core1TaskCode, "Core1Task", STACK_SIZE_CORE1, NULL, PRIORITY_MEDIUM, &core1Task, 1);
    
  AppLogger.info("Setup", "System setup sequence completed. Tasks are running.");
  AppLogger.info("Setup", "---------------- SYSTEM READY ----------------");
}

void loop() {
  // Empty loop - all work is done in tasks
  delay(1000);
}