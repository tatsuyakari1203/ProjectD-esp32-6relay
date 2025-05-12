#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// Định nghĩa chân kết nối cho DHT21
#define DHT_PIN 6  // Sử dụng GPIO6 cho cảm biến DHT21
#define DHT_TYPE DHT21

class SensorManager {
public:
    SensorManager();
    void begin();
    bool readSensors();
    float getTemperature();
    float getHumidity();
    float getHeatIndex();
    String getJsonPayload(const char* apiKey);
    
private:
    DHT _dht;
    float _temperature;
    float _humidity;
    float _heatIndex;
    unsigned long _lastReadTime;
    const unsigned long _readInterval = 2000; // Đọc cảm biến mỗi 2 giây
    bool _readSuccess;
};

#endif // SENSOR_MANAGER_H 