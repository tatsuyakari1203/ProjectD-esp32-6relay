#ifndef ENVIRONMENT_MANAGER_H
#define ENVIRONMENT_MANAGER_H

#include <Arduino.h>
#include <map>
#include "SensorManager.h"

class EnvironmentManager {
public:
    EnvironmentManager(SensorManager& sensorManager);
    
    // Cập nhật các giá trị cảm biến
    void update();
    
    // Lấy giá trị cảm biến hiện tại
    float getTemperature();
    float getHumidity();
    float getHeatIndex();
    float getSoilMoisture(int zone);
    bool isRaining();
    int getLightLevel();
    
    // Cập nhật giá trị cảm biến thủ công (cho cảm biến chưa kết nối)
    void setSoilMoisture(int zone, float value);
    void setRainStatus(bool isRaining);
    void setLightLevel(int level);
    
private:
    SensorManager& _sensorManager;
    
    // Giá trị cảm biến hiện tại
    float _temperature;
    float _humidity;
    float _heatIndex;
    std::map<int, float> _soilMoisture;  // <zone_id, moisture_value>
    bool _isRaining;
    int _lightLevel;
    
    // Thời điểm cập nhật gần nhất
    unsigned long _lastUpdateTime;
};

#endif // ENVIRONMENT_MANAGER_H 