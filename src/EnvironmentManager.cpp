#include "../include/EnvironmentManager.h"
#include "../include/Logger.h"

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
    
    // KHÔNG đọc nhiệt độ, độ ẩm từ SensorManager ở đây nữa
    // Dữ liệu nhiệt độ, độ ẩm, chỉ số nhiệt từ DHT sẽ được đẩy từ main.cpp
    
    // Các cảm biến khác hoặc logic xử lý khác có thể được thêm vào đây
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

// Setter cho nhiệt độ từ bên ngoài
void EnvironmentManager::setCurrentTemperature(float temp) {
    _temperature = temp;
}

// Setter cho độ ẩm từ bên ngoài
void EnvironmentManager::setCurrentHumidity(float hum) {
    _humidity = hum;
}

// Setter cho chỉ số nhiệt từ bên ngoài
void EnvironmentManager::setCurrentHeatIndex(float hi) {
    _heatIndex = hi;
}

void EnvironmentManager::setSoilMoisture(int zone, float value) {
    if (zone >= 1 && zone <= 6) {
        _soilMoisture[zone] = value;
        AppLogger.info("EnvMgr", "Set soil moisture for zone " + String(zone) + " to " + String(value) + "%");
    }
}

void EnvironmentManager::setRainStatus(bool isRaining) {
    _isRaining = isRaining;
    AppLogger.info("EnvMgr", "Set rain status to " + String(isRaining ? "raining" : "not raining"));
}

void EnvironmentManager::setLightLevel(int level) {
    _lightLevel = level;
    AppLogger.info("EnvMgr", "Set light level to " + String(level) + " lux");
} 