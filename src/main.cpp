#include <Arduino.h>
#include "../include/WS_GPIO.h"
#include "../include/SensorManager.h"
#include "../include/NetworkManager.h"
#include <time.h>

// Function prototypes
void Core0TaskCode(void * parameter);
void Core1TaskCode(void * parameter);
void printLocalTime();
void startIrrigation(int zone, unsigned long duration);
void stopIrrigation(int zone);

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
const char* MQTT_TOPIC = "irrigation/esp32_6relay/sensors";

// NTP configuration
const char* NTP_SERVER = "pool.ntp.org";
const char* TZ_INFO = "GMT+7";  // Vietnam timezone (GMT+7)

// Sensor and network management objects
SensorManager sensorManager;
NetworkManager networkManager;

// Time tracking variables
unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 5000;  // Read sensors and send data every 5 seconds
unsigned long lastTimeCheckTime = 0;
const unsigned long timeCheckInterval = 5000;  // Check time every 5 seconds

// LED status tracking
bool ledState = false;
unsigned long lastLedBlinkTime = 0;
const unsigned long ledBlinkInterval = 1000;  // Blink LED every 1 second

// Relay control variables
bool irrigationActive = false;
unsigned long irrigationStartTime = 0;
unsigned long irrigationDuration = 0;  // Duration in milliseconds

// Semaphores for safe access to shared resources
SemaphoreHandle_t sensorDataMutex;
SemaphoreHandle_t irrigationControlMutex;

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
}

// Function to control irrigation system
void startIrrigation(int zone, unsigned long duration) {
  if (xSemaphoreTake(irrigationControlMutex, portMAX_DELAY)) {
    irrigationActive = true;
    irrigationStartTime = millis();
    irrigationDuration = duration;
    
    // Turn on the selected relay/zone
    if (zone >= 0 && zone < numRelays) {
      digitalWrite(relayPins[zone], HIGH);
      Serial.println("Started irrigation on zone " + String(zone) + " for " + String(duration/1000) + " seconds");
    }
    
    xSemaphoreGive(irrigationControlMutex);
  }
}

void stopIrrigation(int zone) {
  if (xSemaphoreTake(irrigationControlMutex, portMAX_DELAY)) {
    irrigationActive = false;
    
    // Turn off the selected relay/zone
    if (zone >= 0 && zone < numRelays) {
      digitalWrite(relayPins[zone], LOW);
      Serial.println("Stopped irrigation on zone " + String(zone));
    }
    
    xSemaphoreGive(irrigationControlMutex);
  }
}

// Core 0 Task - Handles sensors, network, MQTT (preemptive)
void Core0TaskCode(void * parameter) {
  Serial.println("Core 0 Task started on core " + String(xPortGetCoreID()));
  
  for(;;) {
    // Maintain network connection
    networkManager.loop();
    
    unsigned long currentTime = millis();
    
    // Read data from sensors and send to MQTT server every 5 seconds
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
            
            // Send to MQTT server and check detailed status
            int publishResult = networkManager.publish(MQTT_TOPIC, payload.c_str());
            if (publishResult) {
              Serial.println("SUCCESS: Data sent to MQTT server");
              
              // Green LED blink when sending successful
              RGB_Light(0, 255, 0);
              delay(100);
              RGB_Light(0, 0, 0);
            } else {
              Serial.println("ERROR: Failed to send data to MQTT server");
              Serial.println("MQTT Error Details:");
              Serial.println("- Connection state: " + String(networkManager.isConnected() ? "Connected" : "Disconnected"));
              Serial.println("- Topic: " + String(MQTT_TOPIC));
              Serial.println("- Payload size: " + String(payload.length()) + " bytes");
              Serial.println("- Payload: " + payload);
              
              // Try to diagnose the issue
              int mqttState = networkManager.getMqttState();
              Serial.println("- MQTT state code: " + String(mqttState));
              
              // Print possible solutions
              Serial.println("Possible solutions:");
              Serial.println("- Check MQTT server availability");
              Serial.println("- Verify server credentials and permissions");
              Serial.println("- Check network connectivity");
              Serial.println("- Restart the device if issues persist");
              
              // Red LED blink when sending failed
              RGB_Light(255, 0, 0);
              delay(100);
              RGB_Light(0, 0, 0);
            }
          } else {
            Serial.println("ERROR: No network connection, cannot send data");
            Serial.println("- Check WiFi signal strength");
            Serial.println("- Check if router is functioning");
            Serial.println("- Try moving the device closer to WiFi router");
            
            // Red LED blink when no connection
            RGB_Light(255, 0, 0);
            delay(100);
            RGB_Light(0, 0, 0);
          }
        } else {
          Serial.println("ERROR: Failed to read from sensors");
          Serial.println("- Check sensor connections");
          Serial.println("- Verify DHT21 is correctly powered");
          Serial.println("- Make sure data pin is connected to GPIO" + String(DHT_PIN));
        }
        
        // Release mutex after accessing sensor data
        xSemaphoreGive(sensorDataMutex);
      }
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
    // Handle irrigation control
    if (xSemaphoreTake(irrigationControlMutex, portMAX_DELAY)) {
      if (irrigationActive) {
        unsigned long currentTime = millis();
        
        // Check if irrigation should stop
        if (currentTime - irrigationStartTime >= irrigationDuration) {
          // Find which zone is active and stop it
          for (int i = 0; i < numRelays; i++) {
            if (digitalRead(relayPins[i]) == HIGH) {
              stopIrrigation(i);
              break;
            }
          }
        }
      }
      xSemaphoreGive(irrigationControlMutex);
    }
    
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
  
  // Create semaphores before starting tasks
  sensorDataMutex = xSemaphoreCreateMutex();
  irrigationControlMutex = xSemaphoreCreateMutex();
  
  // Print connection pin information
  Serial.println("DHT21 connection pin: GPIO" + String(DHT_PIN));
  Serial.println("Please ensure the sensor is connected correctly:");
  Serial.println("- VCC: 3.3V or 5V");
  Serial.println("- GND: GND");
  Serial.println("- DATA: GPIO" + String(DHT_PIN));
  
  // Initialize GPIO
  GPIO_Init();
  Serial.println("GPIO initialized");
  
  // Turn off all relays initially
  for (int i = 0; i < numRelays; i++) {
    digitalWrite(relayPins[i], LOW);
  }
  Serial.println("All relays turned off");
  
  // Initialize sensors
  sensorManager.begin();
  
  // Initialize network connection
  if (networkManager.begin(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT)) {
    Serial.println("Network connection successful");
    
    // Configure NTP time
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);
    tzset();
    Serial.println("NTP Server configured: " + String(NTP_SERVER));
    
    // Green LED to indicate successful connection
    RGB_Light(0, 255, 0);
    delay(1000);
    RGB_Light(0, 0, 0);
    
    // Beep to indicate startup completed
    Buzzer_PWM(300);
  } else {
    Serial.println("ERROR: Network connection failed");
    Serial.println("- Check WiFi credentials");
    Serial.println("- Check if WiFi network is available");
    Serial.println("- Check if the router is on and functioning");
    
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
  Serial.println("Core 1: Handling irrigation control (non-preemptive)");
  Serial.println("System ready");
  
  // Test irrigation control (uncomment for testing)
  // startIrrigation(0, 10000); // Start irrigation on zone 0 for 10 seconds
}

void loop() {
  // Empty loop - all work is done in tasks
  delay(1000);
}