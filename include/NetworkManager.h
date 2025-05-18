#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector> // For storing subscription topics

typedef void (*MqttCallback)(char* topic, byte* payload, unsigned int length);

// Configuration for retry mechanisms
const uint8_t MAX_WIFI_RETRY_ATTEMPTS = 10;
const uint8_t MAX_MQTT_RETRY_ATTEMPTS = 10;
const unsigned long INITIAL_RETRY_INTERVAL_MS = 5000;     // 5 seconds
const unsigned long MAX_RETRY_INTERVAL_MS = 60000;        // 1 minute
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;      // 15 seconds for WiFi.begin()

class NetworkManager {
public:
    NetworkManager();
    bool begin(const char* ssid, const char* password, const char* mqttServer, int mqttPort);
    // void addSubscriptionTopic(const char* topic); // Suggestion for more flexible topic management
    bool publish(const char* topic, const char* payload);
    bool subscribe(const char* topic); // Will add to list and attempt to subscribe if connected
    void setCallback(MqttCallback callback);
    
    void loop(); // Main loop for handling connections and retries
    bool syncTime();

    // Status getters
    bool isWifiConnected() const;
    bool isMqttConnected() const;
    bool isConnected() const; // True if both WiFi and MQTT are connected
    bool isAttemptingWifiReconnect() const;
    bool isAttemptingMqttReconnect() const;
    int getMqttState();
    
private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    WiFiUDP _ntpUDP;
    NTPClient _timeClient;
    
    char _ssid[64];
    char _password[64];
    char _mqttServer[64];
    int _mqttPort;
    char _clientId[32];

    bool _wifiConnected;
    bool _mqttConnected;
    bool _timeSync;
    
    // Retry and state variables
    unsigned long _lastWiFiReconnectAttemptTime;
    unsigned long _lastMqttReconnectAttemptTime;
    unsigned long _nextWifiRetryTime;
    unsigned long _nextMqttRetryTime;
    uint8_t _wifiRetryCount;
    uint8_t _mqttRetryCount;
    unsigned long _currentWifiRetryIntervalMs;
    unsigned long _currentMqttRetryIntervalMs;
    bool _isAttemptingWifiReconnect;
    bool _isAttemptingMqttReconnect;

    std::vector<String> _subscriptionTopics; // Store topics to subscribe/resubscribe

    // Private helper methods
    bool _connectWifi();
    bool _connectMqtt(); // Renamed from reconnect for clarity
    void _executeMqttSubscriptions();
    void _handleWifiDisconnect();
    void _handleMqttDisconnect();

};

#endif // NETWORK_MANAGER_H 