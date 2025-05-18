#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector> // For storing subscription topics

// THÊM VÀO: Include cho AsyncWebServer và SPIFFS
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
// #include <ArduinoJson.h> // Removing as it seems unused by NetworkManager now
#include <Preferences.h> // THÊM VÀO: Thư viện Preferences cho NVS

// For FreeRTOS task and semaphores - SẼ BỊ XÓA NẾU KHÔNG CÒN CẦN
// #include <freertos/FreeRTOS.h> 
// #include <freertos/task.h>
// #include <freertos/semphr.h>

// THÊM VÀO: Enum for WiFi connection states
enum WifiConnectionState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_START_PORTAL_PENDING // Added for managing portal start
};

typedef void (*MqttCallback)(char* topic, byte* payload, unsigned int length);

// Configuration for retry mechanisms
const uint8_t MAX_WIFI_RETRY_ATTEMPTS = 10; // Sẽ điều chỉnh lại khi có config portal
const uint8_t MAX_MQTT_RETRY_ATTEMPTS = 10;
const unsigned long INITIAL_RETRY_INTERVAL_MS = 5000;     // 5 seconds
const unsigned long MAX_RETRY_INTERVAL_MS = 60000;        // 1 minute
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;      // 15 seconds for WiFi.begin()
const int NTP_ATTEMPTS_PER_SERVER = 2; // Max attempts for each NTP server before trying next

// Add NTP sync interval constants
const unsigned long NTP_SYNC_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes
const unsigned long NTP_FORCE_SYNC_RETRY_INTERVAL_MS = 60 * 1000; // 1 minute if initial/forced sync fails

// THÊM VÀO: Hằng số cho AP Mode và WebServer
// const char* AP_SSID = "ESP32-Config"; // Sẽ định nghĩa trong .cpp
// const char* AP_PASSWORD = "password123"; // Sẽ định nghĩa trong .cpp
// const int WEB_SERVER_PORT = 80; // Sẽ định nghĩa trong .cpp
// const char* ADMIN_LOGIN_PASSWORD = "admin123"; // Sẽ định nghĩa trong .cpp

extern const char* AP_SSID;
extern const char* AP_PASSWORD;
extern const int WEB_SERVER_PORT; 
extern const char* ADMIN_LOGIN_PASSWORD;

class NetworkManager {
public:
    NetworkManager();
    bool begin(const char* initial_ssid, const char* initial_password);
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
    WifiConnectionState getWifiConnectionState() const; // Getter for the new state
    
    // THÊM VÀO: Phương thức cho Config Portal
    void startConfigPortal();
    void stopConfigPortal();
    bool isConfigPortalActive() const;
    IPAddress getLocalIP() const; // Để lấy IP khi đã kết nối WiFi
    IPAddress getSoftAPIP() const; // Để lấy IP của AP

    // Getters for the new configuration items
    String getMqttServer() const;
    int getMqttPort() const;
    String getApiKey() const;

private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    WiFiUDP _ntpUDP;
    NTPClient _timeClient;
    
    char _targetSsid[64]; // SSID mục tiêu (từ NVS hoặc config portal)
    char _targetPassword[64]; // Password mục tiêu
    char _mqttServer[64];
    int _mqttPort;
    char _apiKey[64]; // New member for API Key
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

    // THÊM VÀO: WiFi connection state management variables
    WifiConnectionState _wifiConnectionState;
    unsigned long _wifiConnectStartTime; 

    // NTP timing variables
    unsigned long _lastNtpSyncAttempt;
    unsigned long _lastSuccessfulNtpSync;

    std::vector<String> _subscriptionTopics; // Store topics to subscribe/resubscribe

    // THÊM VÀO: AsyncWebServer object
    AsyncWebServer _server;
    bool _configPortalActive;

    // THÊM VÀO: Đối tượng Preferences
    Preferences _preferences;

    // Private helper methods
    void _initiateWifiConnection(const char* ssid, const char* password); // New non-blocking initiator
    bool _connectMqtt(); // Renamed from reconnect for clarity
    void _executeMqttSubscriptions();
    void _handleWifiDisconnect();
    void _handleMqttDisconnect();

    // THÊM VÀO: Web server request handlers
    void _handleRoot(AsyncWebServerRequest *request);
    void _handleSave(AsyncWebServerRequest *request);
    void _handleGetConfig(AsyncWebServerRequest *request);
    void _handleGetSystemInfo(AsyncWebServerRequest *request);
    // void _handleScanWifi(AsyncWebServerRequest *request); // XÓA BỎ
    void _handleNotFound(AsyncWebServerRequest *request);
    void _serveStaticFile(AsyncWebServerRequest *request, const char* path, const char* contentType);

    // THÊM VÀO: Preferences/NVS helper
    bool _loadNetworkConfig(); 
    void _saveNetworkConfig();

    // --- XÓA BỎ PHẦN For asynchronous WiFi Scan ---
    // static void _wifiScanTaskRunner(void* pvParameters); 
    // void _performActualWifiScan();                      
    // TaskHandle_t _wifiScanTaskHandle;          
    // SemaphoreHandle_t _scanRequestSemaphore;    
    // SemaphoreHandle_t _scanResultReadySemaphore; 
    // String _lastScanResultsJson;                
    // volatile bool _isScanInProgressFlag;        
    // --- KẾT THÚC PHẦN XÓA BỎ ---

    std::vector<String> _ntpServerList; // List of NTP servers
    int _currentNtpServerIndex;         // Index for the current NTP server
};

#endif // NETWORK_MANAGER_H 