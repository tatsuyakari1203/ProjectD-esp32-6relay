#include "../include/NetworkManager.h"
#include "../include/Logger.h"

NetworkManager::NetworkManager() : _timeClient(_ntpUDP, "pool.ntp.org", 7 * 3600) { // 7 hours offset for Vietnam
    _wifiConnected = false;
    _mqttConnected = false;
    _timeSync = false;
    _lastReconnectAttempt = 0;
    
    // Create random clientId with timestamp for uniqueness
    uint32_t random_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    uint32_t timestamp = millis();
    snprintf(_clientId, sizeof(_clientId), "ESP32Client-%06X-%u", random_id, timestamp % 1000000);
    
    // This will use Serial directly since AppLogger is not yet initialized
    Serial.println("Generated MQTT Client ID: " + String(_clientId));
}

bool NetworkManager::begin(const char* ssid, const char* password, const char* mqttServer, int mqttPort, const char* mqttUser, const char* mqttPass) {
    // Save MQTT information for later reconnection
    strncpy(_mqttServer, mqttServer, sizeof(_mqttServer));
    _mqttPort = mqttPort;
    
    if (mqttUser) {
        strncpy(_mqttUser, mqttUser, sizeof(_mqttUser));
    } else {
        _mqttUser[0] = '\0';
    }
    
    if (mqttPass) {
        strncpy(_mqttPass, mqttPass, sizeof(_mqttPass));
    } else {
        _mqttPass[0] = '\0';
    }
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        _wifiConnected = true;
        
        // Initialize NTP client and synchronize time
        _timeClient.begin();
        if (syncTime()) {
            Serial.println("Time synchronized");
            _timeSync = true;
        } else {
            Serial.println("Time sync failed");
        }
        
        // Initialize MQTT client
        _mqttClient.setClient(_wifiClient);
        _mqttClient.setServer(_mqttServer, _mqttPort);
        
        // Configure MQTT client options
        _mqttClient.setKeepAlive(60); // Keep alive time in seconds
        _mqttClient.setSocketTimeout(15); // Socket timeout in seconds
        _mqttClient.setBufferSize(1024); // Set buffer size to handle larger payloads
        
        // Try to connect to MQTT
        if (reconnect()) {
            Serial.println("MQTT connected");
            return true;
        } else {
            Serial.println("MQTT connection failed");
            return false;
        }
    } else {
        Serial.println();
        Serial.println("WiFi connection failed");
        _wifiConnected = false;
        return false;
    }
}

bool NetworkManager::reconnect() {
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        _wifiConnected = false;
        _mqttConnected = false;
        return false;
    }
    
    _wifiConnected = true;
    
    // If already connected to MQTT, no need to reconnect
    if (_mqttClient.connected()) {
        _mqttConnected = true;
        return true;
    }
    
    // Try to connect to MQTT
    AppLogger.debug("NetMgr", "Attempting MQTT connection...");
    
    bool connected = false;
    
    if (_mqttUser[0] != '\0') {
        // Connect with username and password
        connected = _mqttClient.connect(_clientId, _mqttUser, _mqttPass);
    } else {
        // Connect without authentication
        connected = _mqttClient.connect(_clientId);
    }
    
    if (connected) {
        AppLogger.info("NetMgr", "MQTT connected to " + String(_mqttServer) + ":" + String(_mqttPort));
        _mqttConnected = true;
        return true;
    } else {
        AppLogger.error("NetMgr", "MQTT connection failed, rc=" + String(_mqttClient.state()));
        _mqttConnected = false;
        return false;
    }
}

bool NetworkManager::publish(const char* topic, const char* payload) {
    if (!_mqttConnected && !reconnect()) {
        AppLogger.error("NetMgr", "MQTT not connected and reconnect failed");
        return false;
    }

    // Log the publish attempt with details
    AppLogger.debug("NetMgr", "Publishing to MQTT topic: " + String(topic) + 
                   ", length: " + String(strlen(payload)));
    
    // Set the maximum packet size for MQTT (default is often too small)
    _mqttClient.setBufferSize(1024);
    
    // Try to publish with some retries
    bool success = false;
    int retries = 0;
    const int maxRetries = 3;
    
    while (!success && retries < maxRetries) {
        AppLogger.debug("NetMgr", "Publish attempt " + String(retries+1) + "/" + String(maxRetries));
        
        success = _mqttClient.publish(topic, payload);
        
        if (success) {
            AppLogger.debug("NetMgr", "Publish successful");
        } else {
            AppLogger.warning("NetMgr", "Publish failed. MQTT state: " + String(_mqttClient.state()));
            retries++;
            delay(500); // Wait before retry
        }
    }
    
    return success;
}

bool NetworkManager::subscribe(const char* topic) {
    if (!_mqttConnected && !reconnect()) {
        AppLogger.error("NetMgr", "MQTT not connected and reconnect failed");
        return false;
    }
    
    // Log the subscribe attempt
    AppLogger.debug("NetMgr", "Subscribing to MQTT topic: " + String(topic));
    
    // Subscribe to the topic
    bool success = _mqttClient.subscribe(topic);
    
    if (success) {
        AppLogger.info("NetMgr", "Successfully subscribed to topic: " + String(topic));
    } else {
        AppLogger.error("NetMgr", "Failed to subscribe to topic. MQTT state: " + String(_mqttClient.state()));
    }
    
    return success;
}

void NetworkManager::setCallback(MqttCallback callback) {
    _mqttClient.setCallback(callback);
    AppLogger.info("NetMgr", "MQTT callback set");
}

bool NetworkManager::isConnected() {
    return _wifiConnected && _mqttConnected;
}

void NetworkManager::loop() {
    // Check and maintain MQTT connection
    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            
            // Try to reconnect
            if (reconnect()) {
                _lastReconnectAttempt = 0;
            }
        }
    } else {
        // Process MQTT events
        _mqttClient.loop();
    }
    
    // Update NTP time
    _timeClient.update();
}

bool NetworkManager::syncTime() {
    int retries = 0;
    while (!_timeClient.update() && retries < 3) {
        _timeClient.forceUpdate();
        retries++;
        delay(500);
    }
    
    if (retries < 3) {
        // Synchronize time with system for Vietnam timezone
        configTime(7 * 3600, 0, "pool.ntp.org");
        setenv("TZ", "Asia/Ho_Chi_Minh", 1);
        tzset();
        return true;
    }
    
    return false;
}

int NetworkManager::getMqttState() {
    return _mqttClient.state();
} 