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
#include <nvs_flash.h>

// THÊM VÀO: Hằng số cho JSON keys
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
// const char* WIFI_SSID = "2.4 KariS";  // Sẽ không dùng trực tiếp nữa, NetworkManager sẽ xử lý
// const char* WIFI_PASSWORD = "12123402";  // Sẽ không dùng trực tiếp nữa
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
const unsigned long sensorReadInterval = 30000;  // Read sensors and send data every 30 seconds
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
    StaticJsonDocument<256> doc; // Ước tính kích thước đủ cho payload này
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      AppLogger.error("MQTTCallbk", "JSON parsing failed: " + String(error.c_str()));
      return;
    }
    
    // Xử lý cập nhật giá trị cảm biến thủ công
    if (doc.containsKey(JSON_KEY_SOIL_MOISTURE)) {
      JsonObject soil = doc[JSON_KEY_SOIL_MOISTURE];
      int zone = soil[JSON_KEY_ZONE];
      float value = soil[JSON_KEY_VALUE];
      envManager.setSoilMoisture(zone, value);
      AppLogger.info("MQTTCallbk", "Manual soil moisture update: Zone " + String(zone) + ", Value: " + String(value));
    }
    
    if (doc.containsKey(JSON_KEY_RAIN)) {
      bool isRaining = doc[JSON_KEY_RAIN];
      envManager.setRainStatus(isRaining);
      AppLogger.info("MQTTCallbk", "Manual rain status update: " + String(isRaining ? "Raining" : "Not raining"));
    }
    
    if (doc.containsKey(JSON_KEY_LIGHT)) {
      int lightLevel = doc[JSON_KEY_LIGHT];
      envManager.setLightLevel(lightLevel);
      AppLogger.info("MQTTCallbk", "Manual light level update: " + String(lightLevel));
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_LOG_CONFIG) == 0) {
    // Process log configuration command
    AppLogger.info("MQTTCallbk", "Received log configuration command. Payload: " + String(message));

    StaticJsonDocument<128> doc; // Kích thước này đủ cho target và level
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      AppLogger.error("MQTTCallbk", "Log config JSON parsing failed: " + String(error.c_str()));
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
        // --- NEW: Performance Logging Start ---
        unsigned long sensorReadStartTime = millis();
        bool readSuccess = false;
        // --- END NEW ---

        // Read data from sensors
        if (sensorManager.readSensors()) {
          // Mark as success
          readSuccess = true;
          
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
            
            // --- NEW: Performance Logging for MQTT Publish Start ---
            unsigned long mqttPublishStartTime = millis();
            bool mqttSuccess = networkManager.publish(MQTT_TOPIC_SENSORS, payload.c_str());
            unsigned long mqttPublishDuration = millis() - mqttPublishStartTime;
            AppLogger.perf("Core0", "MQTTSensorDataPublish", mqttPublishDuration, mqttSuccess);
            // --- END NEW ---
            
            // Existing log can be kept or removed since we now have the perf log
            AppLogger.debug("Core0", "Sensor data published to MQTT");
          } else {
            AppLogger.warning("Core0", "No network connection, cannot send sensor data via MQTT");
            // LED status handled globally below
          }
        } else {
          AppLogger.error("Core0", "Failed to read from sensors");
          // Mark as failure
          readSuccess = false;
        }
        
        // --- NEW: Performance Logging End ---
        unsigned long sensorReadDuration = millis() - sensorReadStartTime;
        AppLogger.perf("Core0", "SensorReadOperation", sensorReadDuration, readSuccess);
        // --- END NEW ---
        
        // Release mutex after accessing sensor data
        xSemaphoreGive(sensorDataMutex);
      }
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
      ledState = !ledState; // Toggle state for blinking effect

      if (networkManager.isConfigPortalActive()) { // Ưu tiên hiển thị trạng thái Portal
        // Blink Purple LED when Config Portal is active
        if (ledState) {
          RGB_Light(50, 0, 50);  // Low brightness purple (điều chỉnh màu nếu cần)
        } else {
          RGB_Light(0, 0, 0);
        }
      } else if (networkManager.isConnected()) {
        // Blink green LED when connected
        if (ledState) {
          RGB_Light(0, 20, 0);  // Low brightness green
        } else {
          RGB_Light(0, 0, 0);
        }
      } else if (networkManager.isAttemptingWifiReconnect() || networkManager.isAttemptingMqttReconnect()) {
        // Blink blue LED when attempting to reconnect
        if (ledState) {
          RGB_Light(0, 0, 20);  // Low brightness blue
        } else {
          RGB_Light(0, 0, 0);
        }
      } else if (!networkManager.isWifiConnected()){
        // Blink red LED when WiFi is disconnected and not actively trying to reconnect (e.g. max retries reached for a while)
        if (ledState) {
          RGB_Light(20, 0, 0); // Low brightness red
        } else {
          RGB_Light(0, 0, 0);
        }
      } else { // WiFi connected, but MQTT is not, and not actively trying to reconnect MQTT
        // Blink yellow LED when MQTT is disconnected (and WiFi is up)
         if (ledState) {
          RGB_Light(20, 20, 0); // Low brightness yellow
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

// THÊM VÀO: Khai báo đối tượng Preferences
Preferences preferences;

void setup() {
  // Initialize Serial as early as possible
  Serial.begin(115200);
  
  // Wait a bit for Serial to ensure initialization logs can be seen
  uint32_t serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 2000)) { // Wait a maximum of 2 seconds
    delay(10);
  }
  Serial.println(F("\n\nMain: Serial port initialized.")); // F() to save RAM

  // --- NEW: Explicit NVS Initialization ---
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS: Initialization failed (no free pages or new version), attempting to erase NVS partition...");
    if (nvs_flash_erase() != ESP_OK) {
        Serial.println("NVS: Failed to erase NVS partition.");
    } else {
        Serial.println("NVS: Erased NVS partition successfully.");
    }
    // Try to init NVS again after erase
    ret = nvs_flash_init();
  }
  
  if (ret != ESP_OK) {
    Serial.printf("NVS: Failed to initialize NVS! Error: %s. System may not function correctly with NVS features.\n", esp_err_to_name(ret));
    // Depending on the severity, you might halt or set a flag
  } else {
    Serial.println("NVS: Successfully initialized.");
  }
  // --- END NEW ---

  // Initialize Logger 
  // AppLogger.begin() MUST be called before any AppLogger calls in NetworkManager's constructor or begin method.
  // Since NetworkManager's constructor doesn't use AppLogger anymore, and its begin() method does, 
  // placing AppLogger.begin() here is fine.
  AppLogger.begin(&networkManager, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO); 

  // THÊM VÀO: Khởi tạo và đọc cấu hình từ NVS (ví dụ)
  // preferences.begin("app-config", false); // false = read/write, true = read-only
  // String storedSsid = preferences.getString("wifi_ssid", WIFI_SSID);
  // String storedPassword = preferences.getString("wifi_pass", WIFI_PASSWORD);
  // String storedMqttServer = preferences.getString("mqtt_server", MQTT_SERVER);
  // int storedMqttPort = preferences.getInt("mqtt_port", MQTT_PORT);
  // String storedApiKey = preferences.getString("api_key", API_KEY);
  // preferences.end();
  // Sau đó sử dụng các biến storedSsid, storedPassword,... khi gọi networkManager.begin()

  // Initialize NetworkManager FIRST
  // Truyền "" cho SSID và Password ban đầu, NetworkManager sẽ xử lý việc đọc NVS (chưa làm) hoặc mở portal
  // if (!networkManager.begin(storedSsid.c_str(), storedPassword.c_str(), MQTT_SERVER, MQTT_PORT)) { // Khi có NVS
  if (!networkManager.begin("", "", MQTT_SERVER, MQTT_PORT)) { // Hiện tại, luôn truyền rỗng để test portal
    // AppLogger.begin() has been moved before this, so it should be available.
    AppLogger.warning("Main", "NetworkManager did not connect to WiFi/MQTT. Config Portal might be active or system will retry.");
    // LED sẽ được xử lý trong Core0Task dựa trên trạng thái NetworkManager
  } else {
    AppLogger.info("Main", "NetworkManager initialized and connected to WiFi/MQTT successfully.");
  }

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
  // VIỆC SUBSCRIBE SẼ DO NETWORKMANAGER TỰ QUẢN LÝ SAU KHI KẾT NỐI
  // Các lệnh networkManager.subscribe() ở đây sẽ được xóa và chuyển logic vào NetworkManager

  // Đăng ký các topic cần thiết thông qua NetworkManager. 
  // NetworkManager sẽ lưu chúng lại và tự động subscribe/resubscribe khi kết nối MQTT.
  networkManager.subscribe(MQTT_TOPIC_CONTROL);
  networkManager.subscribe(MQTT_TOPIC_SCHEDULE);
  networkManager.subscribe(MQTT_TOPIC_ENV_CONTROL);
  networkManager.subscribe(MQTT_TOPIC_LOG_CONFIG);

  // Đèn LED và Buzzer báo hiệu trạng thái sẽ được quản lý trong Core0TaskCode dựa trên networkManager.isConnected()
  // Bỏ các lệnh LED và Buzzer trực tiếp ở đây để tránh xung đột
  /*
  if (networkManager.isConnected()) {
    AppLogger.info("Setup", "Initial network connection successful.");
    RGB_Light(0, 255, 0); // Green
    delay(1000);
    RGB_Light(0, 0, 0);
    Buzzer_PWM(300);
  } else {
    AppLogger.warning("Setup", "Initial network connection failed. Will retry in background.");
    RGB_Light(255, 0, 0); // Red
    delay(1000);
    RGB_Light(0, 0, 0);
  }
  */

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

  // THÊM VÀO: In ra Stack High Water Mark để theo dõi bộ nhớ
  // Nên thực hiện sau khi hệ thống đã chạy ổn định một thời gian để có số liệu chính xác.
  // Có thể đặt trong một điều kiện if để chỉ in định kỳ (ví dụ: mỗi 60 giây)
  static unsigned long lastStackCheckTime = 0;
  if (millis() - lastStackCheckTime > 60000) { // Mỗi 60 giây
    lastStackCheckTime = millis();
    UBaseType_t core0StackHWM = uxTaskGetStackHighWaterMark(core0Task);
    UBaseType_t core1StackHWM = uxTaskGetStackHighWaterMark(core1Task);
    AppLogger.info("StackCheck", "Core0Task HWM: " + String(core0StackHWM) + " words (" + String(core0StackHWM * sizeof(StackType_t)) + " bytes)");
    AppLogger.info("StackCheck", "Core1Task HWM: " + String(core1StackHWM) + " words (" + String(core1StackHWM * sizeof(StackType_t)) + " bytes)");
    // Lưu ý: "words" ở đây là StackType_t, thường là 4 byte trên ESP32.
    // Dựa vào kết quả này, bạn có thể điều chỉnh STACK_SIZE_CORE0 và STACK_SIZE_CORE1.
    // Ví dụ, nếu HWM là 1000 words (4000 bytes) và STACK_SIZE là 8192 bytes, bạn có thể giảm STACK_SIZE.
  }
}