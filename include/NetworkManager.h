#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

typedef void (*MqttCallback)(char* topic, byte* payload, unsigned int length);

class NetworkManager {
public:
    NetworkManager();
    bool begin(const char* ssid, const char* password, const char* mqttServer, int mqttPort, const char* mqttUser = nullptr, const char* mqttPass = nullptr);
    bool reconnect();
    bool publish(const char* topic, const char* payload);
    bool subscribe(const char* topic);
    void setCallback(MqttCallback callback);
    bool isConnected();
    void loop();
    bool syncTime();
    int getMqttState();
    
private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    WiFiUDP _ntpUDP;
    NTPClient _timeClient;
    
    char _mqttServer[64];
    int _mqttPort;
    char _mqttUser[32];
    char _mqttPass[32];
    char _clientId[32];
    
    bool _wifiConnected;
    bool _mqttConnected;
    bool _timeSync;
    
    unsigned long _lastReconnectAttempt;
    const unsigned long _reconnectInterval = 5000;  // 5 giây giữa các lần kết nối lại
};

#endif // NETWORK_MANAGER_H 