#include "../include/EnvironmentManager.h"
#include "../include/Logger.h"

EnvironmentManager::EnvironmentManager(SensorManager& sensorManager) : _sensorManager(sensorManager) {
    _temperature = 0.0;
    _humidity = 0.0;
    _heatIndex = 0.0;
    _isRaining = false;
    _lightLevel = 0;
    _lastUpdateTime = 0;
    
    // Initialize soil moisture to a default value (e.g., 50%) for all zones.
    for (int i = 1; i <= 6; i++) {
        _soilMoisture[i] = 50.0;
    }
}

// The update() method is currently rate-limited but does not perform active sensor reading itself.
// Temperature, humidity, and heat index are updated externally via setters (e.g., from main.cpp after SensorManager reads them).
// This method can be expanded to include other environment processing logic if needed.
void EnvironmentManager::update() {
    unsigned long currentTime = millis();
    
    // Limit update frequency (e.g., once per second)
    if (currentTime - _lastUpdateTime < 1000) {
        return;
    }
    _lastUpdateTime = currentTime;
    
    // Temperature, humidity, and heat index data from DHT sensors are pushed from main.cpp via setters.
    // Other sensor processing or logic can be added here.
}

float EnvironmentManager::getTemperature() {
    return _temperature;
}

float EnvironmentManager::getHumidity() {
    return _humidity;
}

float EnvironmentManager::getHeatIndex() {
    return _heatIndex;
}

float EnvironmentManager::getSoilMoisture(int zone) {
    // Check if the zone exists in the map.
    if (_soilMoisture.find(zone) != _soilMoisture.end()) {
        return _soilMoisture[zone];
    }
    
    // Return a default value if the zone is not found.
    AppLogger.logf(LOG_LEVEL_WARNING, "EnvMgr", "Requested soil moisture for invalid zone %d, returning default.", zone);
    return 50.0; // Default value
}

bool EnvironmentManager::isRaining() {
    return _isRaining;
}

int EnvironmentManager::getLightLevel() {
    return _lightLevel;
}

// Setter for temperature, typically called from outside after sensor read.
void EnvironmentManager::setCurrentTemperature(float temp) {
    _temperature = temp;
}

// Setter for humidity, typically called from outside after sensor read.
void EnvironmentManager::setCurrentHumidity(float hum) {
    _humidity = hum;
}

// Setter for heat index, typically called from outside after sensor read.
void EnvironmentManager::setCurrentHeatIndex(float hi) {
    _heatIndex = hi;
}

// Sets the soil moisture for a specific zone.
void EnvironmentManager::setSoilMoisture(int zone, float value) {
    if (zone >= 1 && zone <= 6) { // Assuming 6 zones max, adjust if necessary
        _soilMoisture[zone] = value;
        AppLogger.info("EnvMgr", "Set soil moisture for zone " + String(zone) + " to " + String(value) + "%");
    }
}

// Sets the current rain status.
void EnvironmentManager::setRainStatus(bool isRaining) {
    _isRaining = isRaining;
    AppLogger.info("EnvMgr", "Set rain status to " + String(isRaining ? "raining" : "not raining"));
}

// Sets the current light level.
void EnvironmentManager::setLightLevel(int level) {
    _lightLevel = level;
    AppLogger.info("EnvMgr", "Set light level to " + String(level) + " lux");
} 