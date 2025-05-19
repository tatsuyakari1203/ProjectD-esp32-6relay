#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// DHT Sensor Configuration
#define DHT_PIN 6      // GPIO pin for DHT21 sensor
#define DHT_TYPE DHT21 // Type of DHT sensor

class SensorManager {
public:
    SensorManager();
    void begin();
    bool readSensors(); // Reads temperature, humidity, and calculates heat index
    float getTemperature();
    float getHumidity();
    float getHeatIndex();
    String getJsonPayload(const char* apiKey); // Generates a JSON string with sensor data
    
private:
    DHT _dht;                     // DHT sensor object
    float _temperature;           // Last read temperature in Celsius
    float _humidity;              // Last read humidity in %
    float _heatIndex;             // Last calculated heat index in Celsius
    unsigned long _lastReadTime;  // Timestamp of the last sensor read attempt (millis)
    const unsigned long _readInterval = 2000; // Interval between sensor reads (milliseconds)
    bool _readSuccess;            // Status of the last sensor read attempt
};

#endif // SENSOR_MANAGER_H 