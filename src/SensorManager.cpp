#include "../include/SensorManager.h"
#include "../include/Logger.h"

SensorManager::SensorManager() : _dht(DHT_PIN, DHT_TYPE) {
    _temperature = 0.0;
    _humidity = 0.0;
    _heatIndex = 0.0;
    _soilMoisture = 0.0;
    _lastReadTime = 0;
    _readSuccess = false;
}

void SensorManager::begin() {
    _dht.begin(); // Initialize the DHT sensor library
    AppLogger.info("SensorMgr", "DHT21 sensor initialized");
    AppLogger.info("SensorMgr", "Soil moisture sensor ready on GPIO " + String(SOIL_MOISTURE_PIN));
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
        
        // Read soil moisture
        int soilRawValue = analogRead(SOIL_MOISTURE_PIN);
        // Map the 12-bit ADC value (0-4095) to percentage (0-100).
        // Note: Calibration might be needed. Lower raw values might mean wetter.
        // The user's example `map(value, 0, 1023, 0, 100)` implies higher value = higher moisture %.
        // We assume the same for 0-4095 range.
        // Adjust min/max raw values (0 and 4095 here) based on sensor calibration for dry and wet conditions.
        _soilMoisture = map(soilRawValue, 0, 4095, 0, 100);
        // If sensor output is inverted (e.g. higher value for drier soil):
        // _soilMoisture = map(soilRawValue, 0, 4095, 100, 0);

        AppLogger.debug("SensorMgr", "Raw Soil: " + String(soilRawValue) + ", Percent: " + String(_soilMoisture));

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

float SensorManager::getSoilMoisture() {
    return _soilMoisture;
}

// Generates a JSON payload string with sensor data.
String SensorManager::getJsonPayload(const char* apiKey) {
    time_t now;
    time(&now); // Get current Unix timestamp
    
    StaticJsonDocument<768> doc; // Increased size for new sensor data
    
    doc["api_key"] = apiKey;
    doc["timestamp"] = (double)now; // Using double for timestamp as per some conventions
    
    JsonObject device_info = doc.createNestedObject("device_info");
    device_info["name"] = "esp32_6relay"; // Device identifier
    device_info["type"] = "DHT21_SoilMoisture"; // Updated sensor type
    device_info["firmware"] = "1.0.1";    // Updated firmware version
    
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

    JsonObject soil_moisture = doc.createNestedObject("soil_moisture");
    soil_moisture["value"] = _soilMoisture;
    soil_moisture["unit"] = "percent";
    soil_moisture["sensor_type"] = "capacitive_soil_moisture";
    
    String payload;
    serializeJson(doc, payload); // Serialize JSON document to string
    
    return payload;
} 