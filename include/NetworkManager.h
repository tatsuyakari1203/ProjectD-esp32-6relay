#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector> // For storing MQTT subscription topics

// Type definition for the MQTT message callback function
typedef void (*MqttCallback)(char* topic, byte* payload, unsigned int length);

// New struct for WiFi credentials
struct WiFiCredential {
    const char* ssid;
    const char* password;
};

// Configuration for WiFi and MQTT connection retry mechanisms
const uint8_t MAX_WIFI_RETRY_ATTEMPTS = 10;       // Maximum attempts to reconnect to WiFi before a longer pause
const uint8_t MAX_MQTT_RETRY_ATTEMPTS = 10;       // Maximum attempts to reconnect to MQTT before a longer pause
const unsigned long INITIAL_RETRY_INTERVAL_MS = 5000; // Initial delay (ms) before retrying a failed connection
const unsigned long MAX_RETRY_INTERVAL_MS = 60000;    // Maximum delay (ms) for exponential backoff retry
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;  // Timeout (ms) for the initial WiFi.begin() attempt

class NetworkManager {
public:
    NetworkManager();
    // Initializes WiFi, NTP, and MQTT. Attempts initial connections.
    bool begin(const std::vector<WiFiCredential>& credentials, const char* mqttServer, int mqttPort);
    
    // Publishes a message to the specified MQTT topic.
    bool publish(const char* topic, const char* payload);
    // Adds a topic to the subscription list and subscribes if currently connected.
    // Topics are automatically re-subscribed upon MQTT (re)connection.
    bool subscribe(const char* topic); 
    // Sets the callback function for incoming MQTT messages.
    void setCallback(MqttCallback callback);
    
    // Main operational loop: handles connection maintenance, retries, and MQTT client processing.
    void loop(); 
    // Synchronizes the system time using NTP.
    bool syncTime();

    // Status inquiry methods
    bool isWifiConnected() const;            // Returns true if WiFi is currently connected.
    bool isMqttConnected() const;            // Returns true if MQTT is currently connected.
    bool isConnected() const;                // Returns true if both WiFi and MQTT are connected.
    bool isAttemptingWifiReconnect() const;  // Returns true if currently in the process of WiFi reconnection attempts.
    bool isAttemptingMqttReconnect() const;  // Returns true if currently in the process of MQTT reconnection attempts.
    int getMqttState();                      // Returns the raw state from the PubSubClient.
    
private:
    WiFiClient _wifiClient;        // TCP client for WiFi communication
    PubSubClient _mqttClient;      // MQTT client
    WiFiUDP _ntpUDP;               // UDP client for NTP
    NTPClient _timeClient;         // NTP client for time synchronization
    
    std::vector<WiFiCredential> _wifiCredentials; // Stores all WiFi credentials
    char _mqttServer[64];          // MQTT broker address
    int _mqttPort;                 // MQTT broker port
    char _clientId[32];            // Unique client ID for MQTT

    bool _wifiConnected;           // Current WiFi connection status
    bool _mqttConnected;           // Current MQTT connection status
    bool _timeSync;                // NTP time synchronization status
    
    // Variables for managing connection retries and state
    unsigned long _lastWiFiReconnectAttemptTime; // Timestamp of the last WiFi reconnection attempt (millis)
    unsigned long _lastMqttReconnectAttemptTime; // Timestamp of the last MQTT reconnection attempt (millis)
    unsigned long _nextWifiRetryTime;            // Timestamp for the next scheduled WiFi retry (millis)
    unsigned long _nextMqttRetryTime;            // Timestamp for the next scheduled MQTT retry (millis)
    uint8_t _wifiRetryCount;                     // Current number of WiFi reconnection attempts in the current cycle
    uint8_t _mqttRetryCount;                     // Current number of MQTT reconnection attempts in the current cycle
    unsigned long _currentWifiRetryIntervalMs;   // Current retry interval for WiFi, used for exponential backoff
    unsigned long _currentMqttRetryIntervalMs;   // Current retry interval for MQTT, used for exponential backoff
    bool _isAttemptingWifiReconnect;             // Flag indicating if WiFi reconnection logic is active
    bool _isAttemptingMqttReconnect;             // Flag indicating if MQTT reconnection logic is active

    std::vector<String> _subscriptionTopics; // List of topics to subscribe/re-subscribe to upon MQTT connection

    // Internal helper methods for connection management
    bool _connectWifi();
    bool _tryConnectWifi(const char* ssid, const char* password);
    bool _connectMqtt();
    void _executeMqttSubscriptions(); // Subscribes to all topics in _subscriptionTopics
    void _handleWifiDisconnect();     // Handles WiFi disconnection events and initiates reconnection sequence
    void _handleMqttDisconnect();     // Handles MQTT disconnection events and initiates reconnection sequence

};

#endif // NETWORK_MANAGER_H 