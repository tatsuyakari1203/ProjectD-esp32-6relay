#include "../include/SensorManager.h"
#include "../include/Logger.h"

SensorManager::SensorManager() : _dht(DHT_PIN, DHT_TYPE) {
    _temperature = 0.0;
    _humidity = 0.0;
    _heatIndex = 0.0;
    _lastReadTime = 0;
    _readSuccess = false;
}

void SensorManager::begin() {
    _dht.begin(); // Initialize the DHT sensor library
    AppLogger.info("SensorMgr", "DHT21 sensor initialized");
}

// Reads sensor data if the _readInterval has elapsed since the last read.
// Returns true if the read was successful in the current call or the previous successful read is still valid.
// Returns false if a new read attempt failed.
bool SensorManager::readSensors() {
    unsigned long currentTime = millis();
    
    // Only read from the sensor if _readInterval has passed to avoid overwhelming it.
    if (currentTime - _lastReadTime >= _readInterval) {
        _lastReadTime = currentTime;
        
        _humidity = _dht.readHumidity();
        _temperature = _dht.readTemperature(); // Celsius by default
        
        // Check if any reading failed (isnan is for float Not-a-Number)
        if (isnan(_humidity) || isnan(_temperature)) {
            AppLogger.error("SensorMgr", "Failed to read from DHT sensor!");
            _readSuccess = false; // Mark current read attempt as failed
            return false;
        }
        
        // Calculate heat index (in Celsius)
        _heatIndex = _dht.computeHeatIndex(_temperature, _humidity, false); // false for Celsius
        
        _readSuccess = true; // Mark current read attempt as successful
        return true;
    }
    
    // If interval hasn't passed, return status of the last read attempt
    return _readSuccess;
}

float SensorManager::getTemperature() {
    return _temperature;
}

float SensorManager::getHumidity() {
    return _humidity;
}

float SensorManager::getHeatIndex() {
    return _heatIndex;
}

// Generates a JSON payload string with sensor data.
String SensorManager::getJsonPayload(const char* apiKey) {
    time_t now;
    time(&now); // Get current Unix timestamp
    
    StaticJsonDocument<512> doc; // Adjust size if payload structure changes
    
    doc["api_key"] = apiKey;
    doc["timestamp"] = (double)now; // Using double for timestamp as per some conventions
    
    JsonObject device_info = doc.createNestedObject("device_info");
    device_info["name"] = "esp32_6relay"; // Device identifier
    device_info["type"] = "DHT21";        // Sensor type
    device_info["firmware"] = "1.0.0";    // Firmware version
    
    JsonObject temp = doc.createNestedObject("temperature");
    temp["value"] = _temperature;
    temp["unit"] = "celsius";
    temp["sensor_type"] = "temperature";
    
    JsonObject humidity = doc.createNestedObject("humidity");
    humidity["value"] = _humidity;
    humidity["unit"] = "percent";
    humidity["sensor_type"] = "humidity";
    
    JsonObject heat_index = doc.createNestedObject("heat_index");
    heat_index["value"] = _heatIndex;
    heat_index["unit"] = "celsius";
    heat_index["sensor_type"] = "heat_index";
    
    String payload;
    serializeJson(doc, payload); // Serialize JSON document to string
    
    return payload;
} 