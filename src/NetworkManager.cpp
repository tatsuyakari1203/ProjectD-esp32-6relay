#include "../include/NetworkManager.h"
#include "../include/Logger.h"
#include <vector> // Required for std::vector

// Make ntpServerList from main.cpp accessible
extern std::vector<const char*> ntpServerList;
// Make TZ_INFO from main.cpp accessible
extern const char* TZ_INFO;

NetworkManager::NetworkManager() : _timeClient(_ntpUDP) { // Server set dynamically in syncTime, default offset for consistency
    _wifiConnected = false;
    _mqttConnected = false;
    _timeSync = false;
    
    _lastWiFiReconnectAttemptTime = 0;
    _lastMqttReconnectAttemptTime = 0;
    _nextWifiRetryTime = 0;
    _nextMqttRetryTime = 0;
    _wifiRetryCount = 0;
    _mqttRetryCount = 0;
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
    _isAttemptingWifiReconnect = false;
    _isAttemptingMqttReconnect = false;

    // Generate a unique MQTT client ID using part of MAC address and timestamp
    uint32_t random_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    uint32_t timestamp = millis();
    snprintf(_clientId, sizeof(_clientId), "ESP32Client-%06X-%u", random_id, timestamp % 1000000);
    
    // Initial log, AppLogger might not be fully set up yet
    Serial.println("Generated MQTT Client ID: " + String(_clientId));
}

bool NetworkManager::begin(const char* ssid, const char* password, const char* mqttServer, int mqttPort) {
    strncpy(_ssid, ssid, sizeof(_ssid) -1); _ssid[sizeof(_ssid)-1] = '\0';
    strncpy(_password, password, sizeof(_password) -1); _password[sizeof(_password)-1] = '\0';
    strncpy(_mqttServer, mqttServer, sizeof(_mqttServer) -1); _mqttServer[sizeof(_mqttServer)-1] = '\0';
    _mqttPort = mqttPort;

    AppLogger.info("NetMgr", "Initializing network connection...");
    if (_connectWifi()) {
        // Initialize NTP client and synchronize time now that WiFi is available
        _timeClient.begin();
        if (syncTime()) {
            AppLogger.info("NetMgr", "Time synchronized via NTP.");
            _timeSync = true;
        } else {
            AppLogger.warning("NetMgr", "Time sync failed via NTP.");
        }
        
        // Initialize MQTT client settings
        _mqttClient.setClient(_wifiClient);
        _mqttClient.setServer(_mqttServer, _mqttPort);
        _mqttClient.setKeepAlive(60);
        _mqttClient.setSocketTimeout(15);
        _mqttClient.setBufferSize(1024);

        if (_connectMqtt()) {
            AppLogger.info("NetMgr", "Initial MQTT connection successful.");
            return true;
        }
        AppLogger.error("NetMgr", "Initial MQTT connection failed.");
        _handleMqttDisconnect(); // Initiate MQTT reconnection process
        return false; // MQTT failed, but WiFi is up; allow operation in a degraded state or rely on reconnect logic
    }
    AppLogger.error("NetMgr", "Initial WiFi connection failed.");
    _handleWifiDisconnect(); // Initiate WiFi reconnection process
    return false;
}

bool NetworkManager::_connectWifi() {
    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Connecting to WiFi SSID: %s", _ssid);
    _isAttemptingWifiReconnect = true;
    WiFi.begin(_ssid, _password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
            AppLogger.error("NetMgr", "WiFi connection timed out.");
            WiFi.disconnect(true); // Ensure proper disconnection
            _isAttemptingWifiReconnect = false;
            _wifiConnected = false;
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.println();
    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
    _wifiConnected = true;
    _isAttemptingWifiReconnect = false;
    _wifiRetryCount = 0; // Reset retry counters on successful connection
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; 
    return true;
}

bool NetworkManager::_connectMqtt() {
    if (!_wifiConnected) {
        AppLogger.warning("NetMgr", "Cannot connect MQTT, WiFi is not connected.");
        return false;
    }
    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Attempting MQTT connection to %s:%d", _mqttServer, _mqttPort);
    _isAttemptingMqttReconnect = true;
    bool connected = _mqttClient.connect(_clientId);
    
    if (connected) {
        AppLogger.info("NetMgr", "MQTT connected.");
        _mqttConnected = true;
        _isAttemptingMqttReconnect = false;
        _mqttRetryCount = 0; // Reset retry counters
        _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; 
        _executeMqttSubscriptions(); // Subscribe to all registered topics
    } else {
        AppLogger.logf(LOG_LEVEL_WARNING, "NetMgr", "MQTT connection failed, rc=%d", _mqttClient.state());
        _mqttConnected = false; 
        // _isAttemptingMqttReconnect remains true, allowing loop() to handle next attempt
    }
    return connected;
}

void NetworkManager::_executeMqttSubscriptions() {
    if (!_mqttClient.connected()) {
        AppLogger.warning("NetMgr", "Cannot subscribe, MQTT not connected.");
        return;
    }
    AppLogger.info("NetMgr", "Executing MQTT subscriptions...");
    for (const String& topic : _subscriptionTopics) {
        if (_mqttClient.subscribe(topic.c_str())) {
            AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Successfully subscribed to: %s", topic.c_str());
        } else {
            AppLogger.logf(LOG_LEVEL_ERROR, "NetMgr", "Failed to subscribe to: %s", topic.c_str());
        }
    }
}

void NetworkManager::_handleWifiDisconnect() {
    AppLogger.warning("NetMgr", "WiFi disconnected. Initiating reconnection process.");
    _wifiConnected = false;
    _mqttConnected = false; // MQTT cannot be connected if WiFi is down
    _isAttemptingWifiReconnect = true;
    _isAttemptingMqttReconnect = false; // Halt MQTT reconnection attempts if WiFi is down
    _nextWifiRetryTime = millis() + _currentWifiRetryIntervalMs; 
}

void NetworkManager::_handleMqttDisconnect() {
    if (!_wifiConnected) { // If WiFi is also down, WiFi disconnection handler takes precedence
        _isAttemptingMqttReconnect = false; // No need to attempt MQTT if WiFi is not up
        return;
    }
    AppLogger.warning("NetMgr", "MQTT disconnected. Initiating reconnection process.");
    _mqttConnected = false;
    _isAttemptingMqttReconnect = true;
    _nextMqttRetryTime = millis() + _currentMqttRetryIntervalMs; 
}

void NetworkManager::loop() {
    unsigned long currentTime = millis();

    // --- WiFi Connection Management ---
    if (!_wifiConnected && _isAttemptingWifiReconnect) {
        if (currentTime >= _nextWifiRetryTime) {
            if (_wifiRetryCount < MAX_WIFI_RETRY_ATTEMPTS) {
                AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Retrying WiFi connection (Attempt %d)...", _wifiRetryCount + 1);
                if (_connectWifi()) {
                    // WiFi reconnected successfully
                    _isAttemptingMqttReconnect = true; // Signal to attempt MQTT connection next
                    _nextMqttRetryTime = millis();     // Attempt MQTT immediately
                    _mqttRetryCount = 0;               // Reset MQTT retries as WiFi is newly up
                    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                } else {
                    _wifiRetryCount++;
                    _currentWifiRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentWifiRetryIntervalMs * 2); // Apply exponential backoff
                    _nextWifiRetryTime = currentTime + _currentWifiRetryIntervalMs;
                    AppLogger.logf(LOG_LEVEL_WARNING, "NetMgr", "WiFi retry failed. Next attempt in %lus.", _currentWifiRetryIntervalMs / 1000);
                }
            } else {
                AppLogger.error("NetMgr", "Max WiFi retry attempts reached. Pausing before restarting cycle.");
                _nextWifiRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; // Wait longer before restarting the retry cycle
                _wifiRetryCount = 0; // Reset to allow a new cycle of attempts later
                _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
            }
        }
    } else if (WiFi.status() != WL_CONNECTED && _wifiConnected) {
        // WiFi was previously connected, but has now dropped
        _handleWifiDisconnect();
    }

    // --- MQTT Connection Management (only if WiFi is connected) ---
    if (_wifiConnected) {
        if (!_mqttClient.connected() && _isAttemptingMqttReconnect) {
            if (currentTime >= _nextMqttRetryTime) {
                if (_mqttRetryCount < MAX_MQTT_RETRY_ATTEMPTS) {
                    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Retrying MQTT connection (Attempt %d)...", _mqttRetryCount + 1);
                    if (_connectMqtt()) {
                        // MQTT reconnected successfully, _connectMqtt handles flag updates
                    } else {
                        _mqttRetryCount++;
                        _currentMqttRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentMqttRetryIntervalMs * 2); // Apply exponential backoff
                        _nextMqttRetryTime = currentTime + _currentMqttRetryIntervalMs;
                        AppLogger.logf(LOG_LEVEL_WARNING, "NetMgr", "MQTT retry failed. Next attempt in %lus.", _currentMqttRetryIntervalMs / 1000);
                    }
                } else {
                    AppLogger.error("NetMgr", "Max MQTT retry attempts reached. Pausing before restarting cycle.");
                    _nextMqttRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; // Wait longer
                    _mqttRetryCount = 0; // Reset to allow a new cycle of attempts later
                    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                }
            }
        } else if (_mqttClient.connected()) {
            _mqttClient.loop(); // Crucial for PubSubClient: processes incoming messages and maintains keepalive
        } else if (!_mqttClient.connected() && _mqttConnected) { // MQTT was previously connected, but has now dropped
             _handleMqttDisconnect();
        }
    }

    // --- NTP Time Sync Update (periodically if connected and synced initially) ---
    // _timeClient.update() is generally non-blocking. Called periodically to maintain time accuracy.
    if(_wifiConnected && _timeSync) _timeClient.update();
}

bool NetworkManager::publish(const char* topic, const char* payload) {
    if (!isConnected()) {
        AppLogger.logf(LOG_LEVEL_WARNING, "NetMgr", "Cannot publish, network not fully connected. Topic: %s", topic);
        return false;
    }
    AppLogger.logf(LOG_LEVEL_DEBUG, "NetMgr", "Publishing to MQTT topic: %s, length: %u", topic, strlen(payload));
    return _mqttClient.publish(topic, payload);
}

// Adds a topic to the subscription list and subscribes if currently connected.
// Topics in the list will be automatically re-subscribed upon MQTT (re)connection.
bool NetworkManager::subscribe(const char* topic) {
    if (topic == nullptr || strlen(topic) == 0) return false;
    
    for (const String& existingTopic : _subscriptionTopics) {
        if (existingTopic.equals(topic)) {
            // Topic is already in the list. 
            // If connected, re-subscribing might be redundant but harmless if initial subscribe failed.
            if (_mqttClient.connected()) {
                return _mqttClient.subscribe(topic);
            }
            return true; // Already listed, will be subscribed on connect.
        }
    }
    _subscriptionTopics.push_back(String(topic));
    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Added to subscription list: %s", topic);

    if (_mqttClient.connected()) {
        AppLogger.logf(LOG_LEVEL_DEBUG, "NetMgr", "Attempting to subscribe immediately: %s", topic);
        return _mqttClient.subscribe(topic);
    }
    return true; // Added to list, will be subscribed when MQTT connects
}

void NetworkManager::setCallback(MqttCallback callback) {
    _mqttClient.setCallback(callback);
    AppLogger.info("NetMgr", "MQTT callback set");
}

// Status Getters
bool NetworkManager::isWifiConnected() const { return _wifiConnected; }
bool NetworkManager::isMqttConnected() const { return _mqttConnected; }
bool NetworkManager::isConnected() const { return _wifiConnected && _mqttConnected; }
bool NetworkManager::isAttemptingWifiReconnect() const { return _isAttemptingWifiReconnect; }
bool NetworkManager::isAttemptingMqttReconnect() const { return _isAttemptingMqttReconnect; }
int NetworkManager::getMqttState() { return _mqttClient.state(); }

bool NetworkManager::syncTime() {
    AppLogger.info("NetMgr", "Attempting NTP time synchronization...");
    int retries_per_server = 10; // Increased retries
    bool sync_success = false;

    for (const char* current_ntp_server : ntpServerList) {
        AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Attempting to sync with NTP server: %s", current_ntp_server);
        
        _timeClient.setPoolServerName(current_ntp_server); 
        _timeClient.begin();
        // Configure system for an initial attempt to get UTC (offset 0)
        configTime(0, 0, current_ntp_server);

        for (int attempt = 0; attempt < retries_per_server; ++attempt) {
            AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "NTP sync with %s (Attempt %d/%d) for UTC. Current epoch: %lu", 
                           current_ntp_server, attempt + 1, retries_per_server, (unsigned long)time(nullptr));
            
            bool packet_exchange_success = _timeClient.forceUpdate(); 
            
            AppLogger.logf(LOG_LEVEL_DEBUG, "NetMgr", "NTP forceUpdate with %s attempt done (Packet exchange: %s). Current epoch: %lu", 
                           current_ntp_server, packet_exchange_success ? "success" : "failed", (unsigned long)time(nullptr));

            if (packet_exchange_success) {
                // If forceUpdate succeeded (got a response), give a brief moment for system time to potentially update
                delay(200); // Small delay for SNTP to process
            }

            if (time(nullptr) > 1000000000L) { // System clock has a valid UTC epoch
                AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "UTC time appears synced with %s. Raw Epoch: %lu", current_ntp_server, (unsigned long)time(nullptr));
                
                // Now that UTC is set, reconfigure with the correct local timezone offset for Vietnam (GMT+7)
                // This ensures getLocalTime() used by other parts of the system (or directly) will be correct.
                configTime(7 * 3600, 0, current_ntp_server); 
                setenv("TZ", TZ_INFO, 1); // TZ_INFO is from main.cpp
                tzset(); 
                AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Successfully applied local timezone (%s) settings after UTC sync.", TZ_INFO);
                
                struct tm timeinfo;
                if(getLocalTime(&timeinfo)){ 
                    char timeBuffer[50];
                    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
                    AppLogger.logf(LOG_LEVEL_INFO, "NetMgr", "Current local time after NTP sync: %s", timeBuffer);
                } else {
                    AppLogger.error("NetMgr", "getLocalTime() failed after NTP sync.");
                }
                _timeSync = true;
                sync_success = true;
                break; // Exit retry loop for this server
            }
            delay(1000); 
        }
        _timeClient.end(); // Stop client for this server
        if (sync_success) {
            break; // Exit main server loop if successfully synced
        }
        AppLogger.logf(LOG_LEVEL_WARNING, "NetMgr", "Failed to synchronize with NTP server: %s after %d attempts.", current_ntp_server, retries_per_server);
    }

    if (!sync_success) {
        AppLogger.logf(LOG_LEVEL_ERROR, "NetMgr", "NTP time synchronization failed with all configured servers.");
    }
    _timeSync = sync_success;
    return _timeSync;
} 