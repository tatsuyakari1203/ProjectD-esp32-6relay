#ifndef ENVIRONMENT_MANAGER_H
#define ENVIRONMENT_MANAGER_H

#include <Arduino.h>
#include <map> // For storing soil moisture per zone
#include "SensorManager.h"

class EnvironmentManager {
public:
    EnvironmentManager(SensorManager& sensorManager);
    
    // Updates environmental data (currently placeholder, main updates via setters).
    void update();
    
    // Getter methods for current environmental values.
    float getTemperature();      // Returns current temperature in Celsius.
    float getHumidity();         // Returns current air humidity in %.
    float getHeatIndex();        // Returns current heat index in Celsius.
    float getSoilMoisture(int zone); // Returns soil moisture for the specified zone (1-6) in %.
    bool isRaining();            // Returns true if it is currently raining.
    int getLightLevel();           // Returns current light level in lux.
    
    // Setter methods for manually updating environmental values (e.g., for external sensors or simulation).
    void setSoilMoisture(int zone, float value); // Sets soil moisture for a specific zone.
    void setRainStatus(bool isRaining);          // Sets the current rain status.
    void setLightLevel(int level);               // Sets the current light level.
    
    // Setter methods for updating DHT sensor values, typically called after SensorManager reads them.
    void setCurrentTemperature(float temp);    // Sets the current temperature.
    void setCurrentHumidity(float hum);        // Sets the current air humidity.
    void setCurrentHeatIndex(float hi);        // Sets the current heat index.
    
private:
    SensorManager& _sensorManager; // Reference to the SensorManager instance.
    
    // Current environmental sensor values.
    float _temperature;                       // Current temperature in Celsius.
    float _humidity;                          // Current air humidity in %.
    float _heatIndex;                         // Current heat index in Celsius.
    std::map<int, float> _soilMoisture;       // Stores soil moisture percentage per zone ID.
    bool _isRaining;                          // Current rain status (true if raining).
    int _lightLevel;                          // Current light level in lux.
    
    unsigned long _lastUpdateTime;            // Timestamp of the last update call (millis).
};

#endif // ENVIRONMENT_MANAGER_H 