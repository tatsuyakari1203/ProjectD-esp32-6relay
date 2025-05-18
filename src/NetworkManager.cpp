#include "../include/NetworkManager.h"
#include "../include/Logger.h"

NetworkManager::NetworkManager() : _timeClient(_ntpUDP, "pool.ntp.org", 7 * 3600) { // 7 hours offset for Vietnam
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

    // Create random clientId with timestamp for uniqueness
    uint32_t random_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    uint32_t timestamp = millis();
    snprintf(_clientId, sizeof(_clientId), "ESP32Client-%06X-%u", random_id, timestamp % 1000000);
    
    // This will use Serial directly since AppLogger is not yet initialized
    Serial.println("Generated MQTT Client ID: " + String(_clientId));
}

bool NetworkManager::begin(const char* ssid, const char* password, const char* mqttServer, int mqttPort) {
    strncpy(_ssid, ssid, sizeof(_ssid) -1); _ssid[sizeof(_ssid)-1] = '\0';
    strncpy(_password, password, sizeof(_password) -1); _password[sizeof(_password)-1] = '\0';
    strncpy(_mqttServer, mqttServer, sizeof(_mqttServer) -1); _mqttServer[sizeof(_mqttServer)-1] = '\0';
    _mqttPort = mqttPort;

    AppLogger.info("NetMgr", "Initializing network connection...");
    if (_connectWifi()) {
        // Initialize NTP client and synchronize time after WiFi connected
        _timeClient.begin();
        if (syncTime()) {
            AppLogger.info("NetMgr", "Time synchronized via NTP.");
            _timeSync = true;
        } else {
            AppLogger.warning("NetMgr", "Time sync failed via NTP.");
        }
        
        // Initialize MQTT client
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
        _handleMqttDisconnect(); // Start MQTT reconnection process
        return false; // MQTT failed initially, but WiFi is up
    }
    AppLogger.error("NetMgr", "Initial WiFi connection failed.");
    _handleWifiDisconnect(); // Start WiFi reconnection process
    return false;
}

bool NetworkManager::_connectWifi() {
    AppLogger.info("NetMgr", "Connecting to WiFi SSID: " + String(_ssid));
    _isAttemptingWifiReconnect = true;
    WiFi.begin(_ssid, _password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
            AppLogger.error("NetMgr", "WiFi connection timed out.");
            WiFi.disconnect(true); // Ensure it's fully disconnected
            _isAttemptingWifiReconnect = false;
            _wifiConnected = false;
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.println();
    AppLogger.info("NetMgr", "WiFi connected. IP: " + WiFi.localIP().toString());
    _wifiConnected = true;
    _isAttemptingWifiReconnect = false;
    _wifiRetryCount = 0; // Reset retry count on successful connection
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; // Reset interval
    return true;
}

bool NetworkManager::_connectMqtt() {
    if (!_wifiConnected) {
        AppLogger.warning("NetMgr", "Cannot connect MQTT, WiFi is not connected.");
        return false;
    }
    AppLogger.info("NetMgr", "Attempting MQTT connection to " + String(_mqttServer) + ":" + String(_mqttPort));
    _isAttemptingMqttReconnect = true;
    bool connected = _mqttClient.connect(_clientId);
    
    if (connected) {
        AppLogger.info("NetMgr", "MQTT connected.");
        _mqttConnected = true;
        _isAttemptingMqttReconnect = false;
        _mqttRetryCount = 0; // Reset retry count
        _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; // Reset interval
        _executeMqttSubscriptions();
    } else {
        AppLogger.warning("NetMgr", "MQTT connection failed, rc=" + String(_mqttClient.state()));
        _mqttConnected = false; // Ensure this is set if connect fails
        // _isAttemptingMqttReconnect remains true if it failed, loop will handle next attempt
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
            AppLogger.info("NetMgr", "Successfully subscribed to: " + topic);
        } else {
            AppLogger.error("NetMgr", "Failed to subscribe to: " + topic);
        }
    }
}

void NetworkManager::_handleWifiDisconnect() {
    AppLogger.warning("NetMgr", "WiFi disconnected. Initiating reconnection process.");
    _wifiConnected = false;
    _mqttConnected = false; // MQTT can't be connected if WiFi is down
    _isAttemptingWifiReconnect = true;
    _isAttemptingMqttReconnect = false; // Stop MQTT attempts if WiFi is down
    _nextWifiRetryTime = millis() + _currentWifiRetryIntervalMs; // Schedule next attempt
}

void NetworkManager::_handleMqttDisconnect() {
    if (!_wifiConnected) { // If WiFi is also down, WiFi handler takes precedence
        _isAttemptingMqttReconnect = false;
        return;
    }
    AppLogger.warning("NetMgr", "MQTT disconnected. Initiating reconnection process.");
    _mqttConnected = false;
    _isAttemptingMqttReconnect = true;
    _nextMqttRetryTime = millis() + _currentMqttRetryIntervalMs; // Schedule next attempt
}

void NetworkManager::loop() {
    unsigned long currentTime = millis();

    // 1. WiFi Connection Management
    if (!_wifiConnected && _isAttemptingWifiReconnect) {
        if (currentTime >= _nextWifiRetryTime) {
            if (_wifiRetryCount < MAX_WIFI_RETRY_ATTEMPTS) {
                AppLogger.info("NetMgr", "Retrying WiFi connection (Attempt " + String(_wifiRetryCount + 1) + ")...");
                if (_connectWifi()) {
                    // WiFi reconnected, now try to connect MQTT
                    _isAttemptingMqttReconnect = true; // Signal to attempt MQTT connection
                    _nextMqttRetryTime = millis(); // Attempt MQTT immediately
                    _mqttRetryCount = 0; // Reset MQTT retries as WiFi is now up
                    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                } else {
                    _wifiRetryCount++;
                    _currentWifiRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentWifiRetryIntervalMs * 2); // Exponential backoff
                    _nextWifiRetryTime = currentTime + _currentWifiRetryIntervalMs;
                    AppLogger.warning("NetMgr", "WiFi retry failed. Next attempt in " + String(_currentWifiRetryIntervalMs / 1000) + "s.");
                }
            } else {
                AppLogger.error("NetMgr", "Max WiFi retry attempts reached. Will try again later.");
                _nextWifiRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; // Wait longer before resetting cycle
                _wifiRetryCount = 0; // Reset to try the cycle again later
                _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
            }
        }
    } else if (WiFi.status() != WL_CONNECTED && _wifiConnected) {
        // WiFi was connected, but now it's not (e.g. router rebooted)
        _handleWifiDisconnect();
    }

    // 2. MQTT Connection Management (only if WiFi is connected)
    if (_wifiConnected) {
        if (!_mqttClient.connected() && _isAttemptingMqttReconnect) {
            if (currentTime >= _nextMqttRetryTime) {
                if (_mqttRetryCount < MAX_MQTT_RETRY_ATTEMPTS) {
                    AppLogger.info("NetMgr", "Retrying MQTT connection (Attempt " + String(_mqttRetryCount + 1) + ")...");
                    if (_connectMqtt()) {
                        // MQTT reconnected successfully
                    } else {
                        _mqttRetryCount++;
                        _currentMqttRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentMqttRetryIntervalMs * 2); // Exponential backoff
                        _nextMqttRetryTime = currentTime + _currentMqttRetryIntervalMs;
                        AppLogger.warning("NetMgr", "MQTT retry failed. Next attempt in " + String(_currentMqttRetryIntervalMs / 1000) + "s.");
                    }
                } else {
                    AppLogger.error("NetMgr", "Max MQTT retry attempts reached. Will try again later if WiFi is up.");
                    _nextMqttRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; // Wait longer
                    _mqttRetryCount = 0; // Reset to try cycle again
                    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                }
            }
        } else if (_mqttClient.connected()) {
            _mqttClient.loop(); // Essential for PubSubClient to process messages and keepalive
        } else if (!_mqttClient.connected() && _mqttConnected) { // MQTT was connected, but now it's not
             _handleMqttDisconnect();
        }
    }

    // 3. NTP Time Sync Update (periodically)
    // _timeClient.update(); // This is typically non-blocking
    // syncTime() might be better called less frequently if it's blocking or after connections are stable.
    // For now, assuming periodic update is fine for NTPClient.
    if(_wifiConnected && _timeSync) _timeClient.update();
}

bool NetworkManager::publish(const char* topic, const char* payload) {
    if (!isConnected()) {
        AppLogger.warning("NetMgr", "Cannot publish, network not fully connected. Topic: " + String(topic));
        return false;
    }
    AppLogger.debug("NetMgr", "Publishing to MQTT topic: " + String(topic) + ", length: " + String(strlen(payload)));
    return _mqttClient.publish(topic, payload);
}

bool NetworkManager::subscribe(const char* topic) {
    if (topic == nullptr || strlen(topic) == 0) return false;
    // Add to list for resubscription on reconnect
    for (const String& existingTopic : _subscriptionTopics) {
        if (existingTopic.equals(topic)) {
            // AppLogger.debug("NetMgr", "Topic already in subscription list: " + String(topic));
            // Still attempt to subscribe if MQTT is connected, in case initial subscribe failed
            if (_mqttClient.connected()) {
                return _mqttClient.subscribe(topic);
            }
            return false; // Not connected, will be subscribed on connect
        }
    }
    _subscriptionTopics.push_back(String(topic));
    AppLogger.info("NetMgr", "Added to subscription list: " + String(topic));

    if (_mqttClient.connected()) {
        AppLogger.debug("NetMgr", "Attempting to subscribe immediately: " + String(topic));
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
    if(!_wifiConnected) {
        AppLogger.warning("NetMgr", "Cannot sync time, WiFi not connected.");
        return false;
    }
    AppLogger.info("NetMgr", "Attempting NTP time synchronization...");
    int retries = 0;
    // _timeClient.update() can be true even if time is not synced yet after boot
    // _timeClient.forceUpdate() is blocking
    // Using getEpochTime to check if time is reasonable
    _timeClient.begin(); // Ensure it's started

    while(time(nullptr) < 1000000000L && retries < 5) { // Check if time looks like a Unix timestamp
        _timeClient.forceUpdate();
        AppLogger.debug("NetMgr", "NTP forceUpdate, current epoch: " + String(time(nullptr)));
        delay(1000); 
        retries++;
    }

    if (time(nullptr) > 1000000000L) {
        // configTime and tzset are important for Arduino time functions like localtime()
        // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3)
        // For Vietnam GMT+7, gmtOffset_sec = 7 * 3600 = 25200. daylightOffset_sec = 0.
        configTime(7 * 3600, 0, "pool.ntp.org"); 
        setenv("TZ", "Asia/Ho_Chi_Minh", 1); // Set timezone environment variable
        tzset(); // Apply an C library timezone change
        AppLogger.info("NetMgr", "NTP time synchronized and timezone set.");
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        AppLogger.info("NetMgr", "Current time: " + String(asctime(&timeinfo)));
        _timeSync = true;
        return true;
    }
    
    AppLogger.error("NetMgr", "NTP time synchronization failed after retries.");
    _timeSync = false;
    return false;
} 