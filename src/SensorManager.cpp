#include "../include/SensorManager.h"

SensorManager::SensorManager() : _dht(DHT_PIN, DHT_TYPE) {
    _temperature = 0.0;
    _humidity = 0.0;
    _heatIndex = 0.0;
    _lastReadTime = 0;
    _readSuccess = false;
}

void SensorManager::begin() {
    _dht.begin();
    Serial.println("DHT21 sensor initialized");
}

bool SensorManager::readSensors() {
    unsigned long currentTime = millis();
    
    // Only read the sensor if _readInterval time has passed
    if (currentTime - _lastReadTime >= _readInterval) {
        _lastReadTime = currentTime;
        
        // Read humidity
        _humidity = _dht.readHumidity();
        
        // Read temperature (in Celsius)
        _temperature = _dht.readTemperature();
        
        // Check if any reading failed
        if (isnan(_humidity) || isnan(_temperature)) {
            Serial.println("Failed to read from DHT sensor!");
            _readSuccess = false;
            return false;
        }
        
        // Calculate heat index
        _heatIndex = _dht.computeHeatIndex(_temperature, _humidity, false);
        
        _readSuccess = true;
        return true;
    }
    
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

String SensorManager::getJsonPayload(const char* apiKey) {
    // Get current timestamp
    time_t now;
    time(&now);
    
    // Create JSON document with appropriate size
    StaticJsonDocument<512> doc;
    
    // Add required fields
    doc["api_key"] = apiKey;
    doc["timestamp"] = (double)now;
    
    // Create device_info object
    JsonObject device_info = doc.createNestedObject("device_info");
    device_info["name"] = "esp32_6relay";
    device_info["type"] = "DHT21";
    device_info["firmware"] = "1.0.0";
    
    // Create temperature object
    JsonObject temp = doc.createNestedObject("temperature");
    temp["value"] = _temperature;
    temp["unit"] = "celsius";
    temp["sensor_type"] = "temperature";
    
    // Create humidity object
    JsonObject humidity = doc.createNestedObject("humidity");
    humidity["value"] = _humidity;
    humidity["unit"] = "percent";
    humidity["sensor_type"] = "humidity";
    
    // Create heat index object
    JsonObject heat_index = doc.createNestedObject("heat_index");
    heat_index["value"] = _heatIndex;
    heat_index["unit"] = "celsius";
    heat_index["sensor_type"] = "heat_index";
    
    // Convert JSON document to string
    String payload;
    serializeJson(doc, payload);
    
    return payload;
} 