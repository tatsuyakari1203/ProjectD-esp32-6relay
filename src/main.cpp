#include <Arduino.h>
#include "../include/WS_GPIO.h"
#include "../include/SensorManager.h"
#include "../include/NetworkManager.h"
#include "../include/RelayManager.h"
#include "../include/TaskScheduler.h"
#include "../include/EnvironmentManager.h"
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
unsigned long lastStatusReportTime = 0;
const unsigned long statusReportInterval = 10000;  // Send status every 10 seconds
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
    Serial.println("ERROR: Could not get time from NTP");
    return;
  }
  
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.print("Current time: ");
  Serial.println(timeString);
  
  // Print timezone information
  Serial.print("Timezone: ");
  Serial.print(TZ_INFO);
  Serial.print(" (Day of week: ");
  Serial.print(timeinfo.tm_wday);  // 0 is Sunday
  Serial.println(")");
}

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.println("Received MQTT message:");
  Serial.println("- Topic: " + String(topic));
  Serial.println("- Payload: " + String(message));
  
  // Process message based on topic
  if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
    // Process relay control command
    if (relayManager.processCommand(message)) {
      // If command was processed successfully, publish status
      String statusPayload = relayManager.getStatusJson(API_KEY);
      networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_SCHEDULE) == 0) {
    // Process scheduling command
    if (taskScheduler.processCommand(message)) {
      // If schedule was processed successfully, publish schedule status
      String schedulePayload = taskScheduler.getTasksJson(API_KEY);
      networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_ENV_CONTROL) == 0) {
    // Process environment control command
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.println("JSON parsing failed: " + String(error.c_str()));
      return;
    }
    
    // Xử lý cập nhật giá trị cảm biến thủ công
    if (doc.containsKey("soil_moisture")) {
      JsonObject soil = doc["soil_moisture"];
      int zone = soil["zone"];
      float value = soil["value"];
      envManager.setSoilMoisture(zone, value);
    }
    
    if (doc.containsKey("rain")) {
      bool isRaining = doc["rain"];
      envManager.setRainStatus(isRaining);
    }
    
    if (doc.containsKey("light")) {
      int lightLevel = doc["light"];
      envManager.setLightLevel(lightLevel);
    }
  }
}

// Core 0 Task - Handles sensors, network, MQTT (preemptive)
void Core0TaskCode(void * parameter) {
  Serial.println("Core 0 Task started on core " + String(xPortGetCoreID()));
  
  for(;;) {
    // Maintain network connection
    networkManager.loop();
    
    unsigned long currentTime = millis();
    
    // Update environment manager
    if (currentTime - lastEnvUpdateTime >= envUpdateInterval) {
      lastEnvUpdateTime = currentTime;
      envManager.update();
    }
    
    // Read data from sensors and send to MQTT server
    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
      lastSensorReadTime = currentTime;
      
      // Take mutex before accessing sensor data
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY)) {
        // Read data from sensors
        if (sensorManager.readSensors()) {
          // Print data to Serial for debugging
          Serial.print("Temperature: ");
          Serial.print(sensorManager.getTemperature());
          Serial.print("°C, Humidity: ");
          Serial.print(sensorManager.getHumidity());
          Serial.print("%, Heat Index: ");
          Serial.print(sensorManager.getHeatIndex());
          Serial.println("°C");
          
          // Display current time along with sensor data
          printLocalTime();
          
          // Send data to MQTT server immediately after reading
          if (networkManager.isConnected()) {
            // Create JSON payload
            String payload = sensorManager.getJsonPayload(API_KEY);
            
            // Send to MQTT server
            networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
          } else {
            Serial.println("ERROR: No network connection, cannot send data");
            
            // Red LED blink when no connection
            RGB_Light(255, 0, 0);
            delay(100);
            RGB_Light(0, 0, 0);
          }
        } else {
          Serial.println("ERROR: Failed to read from sensors");
        }
        
        // Release mutex after accessing sensor data
        xSemaphoreGive(sensorDataMutex);
      }
    }
    
    // Send relay status periodically
    if (currentTime - lastStatusReportTime >= statusReportInterval) {
      lastStatusReportTime = currentTime;
      
      if (networkManager.isConnected()) {
        // Send relay status
        String statusPayload = relayManager.getStatusJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_STATUS, statusPayload.c_str());
        
        // Send schedule status
        String schedulePayload = taskScheduler.getTasksJson(API_KEY);
        networkManager.publish(MQTT_TOPIC_SCHEDULE_STATUS, schedulePayload.c_str());
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
  Serial.println("Core 1 Task started on core " + String(xPortGetCoreID()));
  
  for(;;) {
    // Update relay manager to handle timer-based relay control
    relayManager.update();
    
    // This is a non-preemptive task, so we can have longer delay
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  
  // Wait for Serial connection or maximum 5 seconds
  uint32_t startTime = millis();
  while (!Serial && millis() - startTime < 5000) {
    delay(100);
  }
  
  // Startup message
  Serial.println("");
  Serial.println("=============================================");
  Serial.println("ESP32-S3 Dual-Core Irrigation System");
  Serial.println("=============================================");
  
  // Create semaphore
  sensorDataMutex = xSemaphoreCreateMutex();
  
  // Initialize GPIO
  GPIO_Init();
  Serial.println("GPIO initialized");
  
  // Initialize relay manager
  relayManager.begin(relayPins, numRelays);
  
  // Initialize task scheduler
  taskScheduler.begin();
  
  // Initialize sensors
  sensorManager.begin();
  
  // Initialize network connection
  if (networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT)) {
    Serial.println("Network connection successful");
    
    // Configure NTP time for Vietnam timezone (GMT+7)
    configTime(7 * 3600, 0, NTP_SERVER); // 7 hours offset for Vietnam
    setenv("TZ", "Asia/Ho_Chi_Minh", 1); // Set Vietnam timezone
    tzset();
    Serial.println("NTP Server configured: " + String(NTP_SERVER) + " with Vietnam timezone");
    
    // Set MQTT callback and subscribe to control topics
    networkManager.setCallback(mqttCallback);
    
    // Subscribe to relay control topic
    if (networkManager.subscribe(MQTT_TOPIC_CONTROL)) {
      Serial.println("Subscribed to control topic: " + String(MQTT_TOPIC_CONTROL));
    } else {
      Serial.println("Failed to subscribe to control topic");
    }
    
    // Subscribe to schedule topic
    if (networkManager.subscribe(MQTT_TOPIC_SCHEDULE)) {
      Serial.println("Subscribed to schedule topic: " + String(MQTT_TOPIC_SCHEDULE));
    } else {
      Serial.println("Failed to subscribe to schedule topic");
    }
    
    // Subscribe to environment control topic
    if (networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL)) {
      Serial.println("Subscribed to environment control topic: " + String(MQTT_TOPIC_ENV_CONTROL));
    } else {
      Serial.println("Failed to subscribe to environment control topic");
    }
    
    // Green LED to indicate successful connection
    RGB_Light(0, 255, 0);
    delay(1000);
    RGB_Light(0, 0, 0);
    
    // Beep to indicate startup completed
    Buzzer_PWM(300);
  } else {
    Serial.println("ERROR: Network connection failed");
    
    // Red LED to indicate connection failure
    RGB_Light(255, 0, 0);
    delay(1000);
    RGB_Light(0, 0, 0);
  }
  
  // Create tasks that will run on specific cores
  xTaskCreatePinnedToCore(
    Core0TaskCode,    // Task function
    "Core0Task",      // Name of task
    STACK_SIZE_CORE0, // Stack size
    NULL,             // Task input parameter
    PRIORITY_MEDIUM,  // Priority (higher number = higher priority)
    &core0Task,       // Task handle
    0);               // Core where the task should run (Core 0)
    
  xTaskCreatePinnedToCore(
    Core1TaskCode,    // Task function
    "Core1Task",      // Name of task
    STACK_SIZE_CORE1, // Stack size
    NULL,             // Task input parameter
    PRIORITY_MEDIUM,  // Priority
    &core1Task,       // Task handle
    1);               // Core where the task should run (Core 1)
    
  // Print debug info
  delay(500);
  Serial.println("Tasks created and running");
  Serial.println("Core 0: Handling sensors, MQTT, scheduling (preemptive)");
  Serial.println("Core 1: Handling relay control (non-preemptive)");
  Serial.println("System ready");
}

void loop() {
  // Empty loop - all work is done in tasks
  delay(1000);
}