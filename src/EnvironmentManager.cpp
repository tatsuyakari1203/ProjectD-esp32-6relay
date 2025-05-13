#include "../include/EnvironmentManager.h"

EnvironmentManager::EnvironmentManager(SensorManager& sensorManager) : _sensorManager(sensorManager) {
    _temperature = 0.0;
    _humidity = 0.0;
    _heatIndex = 0.0;
    _isRaining = false;
    _lightLevel = 0;
    _lastUpdateTime = 0;
    
    // Thiết lập giá trị mặc định cho độ ẩm đất (50% - giá trị trung bình)
    for (int i = 1; i <= 6; i++) {
        _soilMoisture[i] = 50.0;
    }
}

void EnvironmentManager::update() {
    unsigned long currentTime = millis();
    
    // Giới hạn tần suất cập nhật (mỗi 1 giây)
    if (currentTime - _lastUpdateTime < 1000) {
        return;
    }
    _lastUpdateTime = currentTime;
    
    // Đọc nhiệt độ, độ ẩm từ SensorManager
    if (_sensorManager.readSensors()) {
        _temperature = _sensorManager.getTemperature();
        _humidity = _sensorManager.getHumidity();
        _heatIndex = _sensorManager.getHeatIndex();
    }
    
    // Các cảm biến khác hiện chưa có, sẽ được cập nhật sau khi kết nối
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
    // Kiểm tra xem zone có trong map không
    if (_soilMoisture.find(zone) != _soilMoisture.end()) {
        return _soilMoisture[zone];
    }
    
    // Trả về giá trị mặc định nếu không tìm thấy zone
    return 50.0;
}

bool EnvironmentManager::isRaining() {
    return _isRaining;
}

int EnvironmentManager::getLightLevel() {
    return _lightLevel;
}

void EnvironmentManager::setSoilMoisture(int zone, float value) {
    if (zone >= 1 && zone <= 6) {
        _soilMoisture[zone] = value;
        Serial.println("Set soil moisture for zone " + String(zone) + " to " + String(value) + "%");
    }
}

void EnvironmentManager::setRainStatus(bool isRaining) {
    _isRaining = isRaining;
    Serial.println("Set rain status to " + String(isRaining ? "raining" : "not raining"));
}

void EnvironmentManager::setLightLevel(int level) {
    _lightLevel = level;
    Serial.println("Set light level to " + String(level) + " lux");
} 