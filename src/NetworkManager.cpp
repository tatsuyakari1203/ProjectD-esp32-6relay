#include "../include/NetworkManager.h"
#include "../include/Logger.h"
// SPIFFS đã được include trong .h, nhưng để rõ ràng có thể thêm ở đây nếu muốn.
// #include <SPIFFS.h> 
#include <Preferences.h> // THÊM VÀO: Thư viện Preferences cho NVS

// Định nghĩa các hằng số đã được khai báo là extern trong .h
const char* AP_SSID = "ESP32-Config";
const char* AP_PASSWORD = "password123"; // Mật khẩu cho AP (có thể để rỗng "" nếu muốn mạng mở)
const int WEB_SERVER_PORT = 80;
const char* ADMIN_LOGIN_PASSWORD = "admin123";

// THÊM VÀO: Default values for MQTT and API Key
const char* DEFAULT_MQTT_SERVER = "karis.cloud";
const int DEFAULT_MQTT_PORT = 1883;
const char* DEFAULT_API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2";

// THÊM VÀO: Enum for WiFi connection states
// enum WifiConnectionState { // <-- REMOVE THIS BLOCK
//     WIFI_STATE_DISCONNECTED,
//     WIFI_STATE_CONNECTING,
//     WIFI_STATE_CONNECTED,
//     WIFI_STATE_START_PORTAL_PENDING // Added for managing portal start
// }; // <-- REMOVE THIS BLOCK

NetworkManager::NetworkManager() : 
    _timeClient(_ntpUDP), 
    _server(WEB_SERVER_PORT), 
    _configPortalActive(false)
{
    _wifiConnected = false;
    _mqttConnected = false;
    _timeSync = false;
    _currentNtpServerIndex = 0;
    _wifiConnectionState = WIFI_STATE_DISCONNECTED; 
    _wifiConnectStartTime = 0; 

    // NTP variables
    _lastNtpSyncAttempt = 0;
    _lastSuccessfulNtpSync = 0;

    // Populate NTP server list
    _ntpServerList.push_back("vn.pool.ntp.org");
    _ntpServerList.push_back("0.vn.pool.ntp.org");
    _ntpServerList.push_back("1.ntp.vnix.vn");    
    _ntpServerList.push_back("2.ntp.vnix.vn");    
    _ntpServerList.push_back("0.asia.pool.ntp.org");
    _ntpServerList.push_back("1.asia.pool.ntp.org");
    _ntpServerList.push_back("pool.ntp.org");      
    _ntpServerList.push_back("time.nist.gov");    

    _lastWiFiReconnectAttemptTime = 0; // Still used for logging/timing, but not direct control
    _lastMqttReconnectAttemptTime = 0;
    _nextWifiRetryTime = 0;
    _nextMqttRetryTime = 0;
    _wifiRetryCount = 0;
    _mqttRetryCount = 0;
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
    _isAttemptingWifiReconnect = false; // This flag indicates the *desire* to be connected
    _isAttemptingMqttReconnect = false;

    _targetSsid[0] = '\0';
    _targetPassword[0] = '\0';
    _mqttServer[0] = '\0';
    _apiKey[0] = '\0'; 
    _mqttPort = 0;

    uint32_t random_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    uint32_t timestamp = millis();
    snprintf(_clientId, sizeof(_clientId), "ESP32Client-%06X-%u", random_id, timestamp % 1000000);
}

bool NetworkManager::begin(const char* initial_ssid, const char* initial_password) {
    AppLogger.info("NetMgr", "NetworkManager::begin() called.");
    AppLogger.info("NetMgr", "Generated MQTT Client ID: " + String(_clientId));

    if (!_preferences.begin("net-config", false)) {
        AppLogger.warning("NetMgr", "NVM: Failed to initialize Preferences.");
    } else {
        AppLogger.info("NetMgr", "NVM: Preferences initialized successfully.");
        _loadNetworkConfig(); 
    }

    if (strlen(_targetSsid) == 0) {
        if (initial_ssid && strlen(initial_ssid) > 0) {
            strncpy(_targetSsid, initial_ssid, sizeof(_targetSsid) - 1);
            _targetSsid[sizeof(_targetSsid) - 1] = '\0';
            if (initial_password) {
                strncpy(_targetPassword, initial_password, sizeof(_targetPassword) - 1);
                _targetPassword[sizeof(_targetPassword) - 1] = '\0';
            } else {
                _targetPassword[0] = '\0';
            }
            AppLogger.info("NetMgr", "Using initial parameters for WiFi credentials as NVS was empty.");
        } else {
            AppLogger.info("NetMgr", "No WiFi credentials in NVS or from initial parameters.");
        }
    }
    
    AppLogger.info("NetMgr", "Effective Config: SSID='" + String(_targetSsid) + "', MQTT Server='" + String(_mqttServer) + ":" + String(_mqttPort) + "', APIKey='" + String(_apiKey) + "'");

    AppLogger.info("NetMgr", "Initializing SPIFFS...");
    if (!SPIFFS.begin(true)) {
        AppLogger.error("NetMgr", "SPIFFS Mount Failed.");
    } else {
        AppLogger.info("NetMgr", "SPIFFS mounted successfully.");
    }

    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){ this->_handleRoot(request); });
    _server.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest *request){ this->_serveStaticFile(request, "/style.css", "text/css"); });
    _server.on("/script.js", HTTP_GET, [this](AsyncWebServerRequest *request){ this->_serveStaticFile(request, "/script.js", "text/javascript"); });
    _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){ this->_handleSave(request); });
    _server.on("/getconfig", HTTP_GET, [this](AsyncWebServerRequest *request){ this->_handleGetConfig(request); });
    _server.on("/getsysteminfo", HTTP_GET, [this](AsyncWebServerRequest *request){ this->_handleGetSystemInfo(request); });
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(204); });
    _server.onNotFound([this](AsyncWebServerRequest *request){ this->_handleNotFound(request); });

    WiFi.mode(WIFI_STA); 
    delay(100); 

    _server.begin();
    AppLogger.info("NetMgr", "AsyncWebServer started on port " + String(WEB_SERVER_PORT) + ".");

    if (strlen(_targetSsid) > 0) {
        AppLogger.info("NetMgr", "Attempting initial WiFi connection to '" + String(_targetSsid) + "'...");
        _isAttemptingWifiReconnect = true; 
        _initiateWifiConnection(_targetSsid, _targetPassword); 

        unsigned long connectionAttemptStartTime = millis();
        AppLogger.info("NetMgr", "Waiting for initial WiFi connection (max " + String(WIFI_CONNECT_TIMEOUT_MS / 1000) + "s). This involves internal loop calls.");
        
        while (_wifiConnectionState == WIFI_STATE_CONNECTING && (millis() - connectionAttemptStartTime < WIFI_CONNECT_TIMEOUT_MS)) {
            this->loop(); // Process WiFi state, does not process MQTT/NTP yet in this specific context
            delay(50);  
        }

        if (_wifiConnectionState == WIFI_STATE_CONNECTED) {
            AppLogger.info("NetMgr", "Initial WiFi connection successful.");
            
            // Configure NTP Client (will attempt sync in loop())
            _timeClient.setTimeOffset(7 * 3600); // GMT+7
            if (!_ntpServerList.empty()) {
                _timeClient.setPoolServerName(_ntpServerList[_currentNtpServerIndex].c_str());
                _timeClient.begin(); // Initialize UDP for NTP, first time
            }
            _lastNtpSyncAttempt = 0; // Trigger immediate sync attempt in loop()
            AppLogger.info("NetMgr", "NTP client configured. Sync will be attempted in main loop.");

            // Configure MQTT Client
            _mqttClient.setClient(_wifiClient);
            _mqttClient.setServer(_mqttServer, _mqttPort);
            _mqttClient.setKeepAlive(60);
            _mqttClient.setSocketTimeout(10); 
            _mqttClient.setBufferSize(1024); // Default is 256, ensure this matches PubSubClient.h MQTT_MAX_PACKET_SIZE if changed

            AppLogger.info("NetMgr", "Attempting initial MQTT connection...");
            if (_connectMqtt()) { 
                AppLogger.info("NetMgr", "Initial MQTT connection successful.");
            } else {
                AppLogger.error("NetMgr", "Initial MQTT connection failed. Will retry in main loop.");
            }
        } else {
            AppLogger.warning("NetMgr", "Initial WiFi connection failed within timeout. Final State: " + String(_wifiConnectionState) + ". Retries/Portal will be handled by main loop.");
            _isAttemptingWifiReconnect = true; 
            _nextWifiRetryTime = millis(); 
            _wifiConnectionState = WIFI_STATE_DISCONNECTED; 
             _wifiRetryCount = 0; // Reset for new series of attempts by loop()
            _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
        }
    } else {
        AppLogger.info("NetMgr", "No SSID configured. Requesting Config Portal start via loop.");
        _wifiConnectionState = WIFI_STATE_START_PORTAL_PENDING; 
        _isAttemptingWifiReconnect = false; 
    }
    return _wifiConnected && _mqttConnected; 
}

void NetworkManager::_initiateWifiConnection(const char* ssid_to_connect, const char* password_to_connect) {
    if (_wifiConnectionState == WIFI_STATE_CONNECTING && (millis() - _wifiConnectStartTime < WIFI_CONNECT_TIMEOUT_MS)) {
        AppLogger.debug("NetMgr", "WiFi connection attempt already in progress.");
        return; 
    }

    if (!ssid_to_connect || strlen(ssid_to_connect) == 0) {
        AppLogger.warning("NetMgr", "Cannot initiate WiFi connection: SSID is empty.");
        _wifiConnectionState = WIFI_STATE_DISCONNECTED; 
        if(!_configPortalActive) _wifiConnectionState = WIFI_STATE_START_PORTAL_PENDING;
        return;
    }

    AppLogger.info("NetMgr", "Initiating WiFi connection to: " + String(ssid_to_connect));
    
    if (_configPortalActive) {
         AppLogger.info("NetMgr", "Config portal is active, stopping it to attempt STA connection.");
         WiFi.softAPdisconnect(true);
         _configPortalActive = false; 
    }
    
    WiFi.mode(WIFI_STA); 
    
    // It's good practice to disconnect before trying to connect to a new/same AP,
    // but WiFi.disconnect(true) can also be blocking for a short period.
    // WiFi.begin() should handle this, but explicit can sometimes help.
    // For now, let WiFi.begin() manage the disconnection of any previous state.
    // if (WiFi.isConnected()) { 
    //     WiFi.disconnect(true); 
    //     delay(100); 
    // }

    WiFi.begin(ssid_to_connect, password_to_connect);
    _wifiConnectionState = WIFI_STATE_CONNECTING;
    _wifiConnectStartTime = millis();
    _isAttemptingWifiReconnect = true; 
    _lastWiFiReconnectAttemptTime = millis(); 
}

bool NetworkManager::_connectMqtt() {
    if (!_wifiConnected) {
        AppLogger.warning("NetMgr", "MQTT: Cannot connect, WiFi is not available.");
        _mqttConnected = false;
        return false;
    }

    // Ensure _wifiClient is explicitly stopped if it's in a lingering connected state
    // before PubSubClient attempts to use it for a new connection.
    if (_wifiClient.connected()) {
        AppLogger.warning("NetMgr", "MQTT: _wifiClient was found connected before new MQTT connect attempt. Stopping it first.");
        _wifiClient.stop();
        delay(100); // Short delay to allow the stop to process
    }

    AppLogger.info("NetMgr", "MQTT: Attempting connection to " + String(_mqttServer) + ":" + String(_mqttPort) + " as " + String(_clientId));
    _isAttemptingMqttReconnect = true; // Mark that we are trying

    // PubSubClient connect() is blocking
    bool connected = _mqttClient.connect(_clientId); 
    
    if (connected) {
        AppLogger.info("NetMgr", "MQTT: Connected successfully.");
        _mqttConnected = true;
        _isAttemptingMqttReconnect = false; // Clear attempt flag
        _mqttRetryCount = 0; 
        _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; 
        _executeMqttSubscriptions();
    } else {
        AppLogger.warning("NetMgr", "MQTT: Connection failed, rc=" + String(_mqttClient.state()) + ". Will retry.");
        _mqttConnected = false; 
        // _isAttemptingMqttReconnect remains true so loop() can retry
    }
    return _mqttConnected;
}

void NetworkManager::_executeMqttSubscriptions() {
    if (!_mqttClient.connected()) {
        AppLogger.warning("NetMgr", "MQTT: Cannot subscribe, client not connected.");
        return;
    }
    AppLogger.info("NetMgr", "MQTT: Executing subscriptions...");
    for (const String& topic : _subscriptionTopics) {
        if (_mqttClient.subscribe(topic.c_str())) {
            AppLogger.info("NetMgr", "MQTT: Subscribed to: " + topic);
        } else {
            AppLogger.error("NetMgr", "MQTT: Failed to subscribe to: " + topic);
        }
    }
}

void NetworkManager::_handleWifiDisconnect() {
    AppLogger.warning("NetMgr", "WiFi: Detected disconnection.");
    _wifiConnected = false;
    _mqttConnected = false; 
    _wifiConnectionState = WIFI_STATE_DISCONNECTED;
    _timeSync = false; // Assume NTP sync is lost

    if (_configPortalActive) {
        AppLogger.info("NetMgr", "WiFi: Disconnected, but Config Portal is active. No STA reconnect attempt now.");
        _isAttemptingWifiReconnect = false; 
        return;
    }

    AppLogger.info("NetMgr", "WiFi: Will attempt reconnection via loop().");
    _isAttemptingWifiReconnect = true; 
    _nextWifiRetryTime = millis(); // Schedule an immediate retry attempt check by loop()
    _wifiRetryCount = 0; 
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; 
}

void NetworkManager::_handleMqttDisconnect() {
    if (!_wifiConnected) { 
        AppLogger.debug("NetMgr", "MQTT: Disconnected, but WiFi is also down. WiFi handler will manage MQTT state.");
        _isAttemptingMqttReconnect = false; 
        _mqttConnected = false;
        return;
    }
    AppLogger.warning("NetMgr", "MQTT: Detected disconnection.");
    _mqttConnected = false;
    _isAttemptingMqttReconnect = true; 
    _nextMqttRetryTime = millis(); 
    _mqttRetryCount = 0; 
    _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS; 
}

void NetworkManager::loop() {
    unsigned long currentTime = millis();

    // --- Config Portal Activation ---
    if (_wifiConnectionState == WIFI_STATE_START_PORTAL_PENDING && !_configPortalActive) {
        AppLogger.info("NetMgr", "Loop: Activating Config Portal as requested.");
        startConfigPortal(); 
    }

    // --- WiFi Connection State Machine ---
    if (!_configPortalActive) { // Only manage STA connection if portal is not active
        if (_wifiConnectionState == WIFI_STATE_CONNECTING) {
            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                AppLogger.info("NetMgr", "Loop: WiFi successfully connected. IP: " + WiFi.localIP().toString());
                _wifiConnected = true;
                _wifiConnectionState = WIFI_STATE_CONNECTED;
                _isAttemptingWifiReconnect = false; 
                _wifiRetryCount = 0; 
                _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;

                _isAttemptingMqttReconnect = true; 
                _nextMqttRetryTime = millis(); 
                _mqttRetryCount = 0; 
                _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                
                // Reset NTP server index and reconfigure NTP client on new WiFi connection.
                if (!_ntpServerList.empty()) {
                    AppLogger.info("NetMgr", "NTP: Reconfiguring on WiFi connect. Server: " + String(_ntpServerList[0].c_str()));
                    _timeClient.end(); // Stop UDP client before changing server
                    _currentNtpServerIndex = 0; 
                    _timeClient.setPoolServerName(_ntpServerList[_currentNtpServerIndex].c_str());
                    _timeClient.begin(); // Re-initialize UDP client
                }
                _lastNtpSyncAttempt = 0; // Trigger immediate sync attempt

            } else if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED || 
                       status == WL_IDLE_STATUS && (currentTime - _wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS / 2) || // stuck in idle
                       (currentTime - _wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS)) {
                
                if (status == WL_NO_SSID_AVAIL) AppLogger.error("NetMgr", "Loop: WiFi connection failed - SSID not available.");
                else if (currentTime - _wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS) AppLogger.error("NetMgr", "Loop: WiFi connection timed out.");
                else AppLogger.warning("NetMgr", "Loop: WiFi connection failed, status: " + String(status));
                
                WiFi.disconnect(false); 
                _wifiConnected = false;
                _wifiConnectionState = WIFI_STATE_DISCONNECTED;
                _nextWifiRetryTime = currentTime; 
            }
        
        } else if (_wifiConnectionState == WIFI_STATE_DISCONNECTED && _isAttemptingWifiReconnect) {
            if (currentTime >= _nextWifiRetryTime) {
                if (_wifiRetryCount < MAX_WIFI_RETRY_ATTEMPTS) {
                    AppLogger.info("NetMgr", "Loop: Retrying WiFi connection (Attempt " + String(_wifiRetryCount + 1) + "/" + String(MAX_WIFI_RETRY_ATTEMPTS) + ")");
                    _initiateWifiConnection(_targetSsid, _targetPassword); 
                    _wifiRetryCount++;
                     _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                    for(int i = 1; i < _wifiRetryCount; ++i) { 
                       _currentWifiRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentWifiRetryIntervalMs * 2);
                    }
                    _nextWifiRetryTime = currentTime + _currentWifiRetryIntervalMs;

                } else {
                    AppLogger.error("NetMgr", "Loop: Max WiFi retry attempts (" + String(MAX_WIFI_RETRY_ATTEMPTS) + ") reached for current credentials.");
                    AppLogger.info("NetMgr", "Loop: Requesting Config Portal activation.");
                    _wifiConnectionState = WIFI_STATE_START_PORTAL_PENDING; 
                    _isAttemptingWifiReconnect = false; 
                    _wifiRetryCount = 0; 
                    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                }
            }
        } else if (WiFi.status() != WL_CONNECTED && _wifiConnected) {
            AppLogger.warning("NetMgr", "Loop: WiFi connection unexpectedly lost (status: " + String(WiFi.status()) + ").");
            _handleWifiDisconnect(); 
        }
    } // end if (!_configPortalActive)

    // --- MQTT Connection Management ---
    if (_wifiConnected && !_configPortalActive) {
        if (!_mqttClient.connected()) {
            if (_isAttemptingMqttReconnect && currentTime >= _nextMqttRetryTime) { 
                 if (_mqttRetryCount < MAX_MQTT_RETRY_ATTEMPTS) {
                    AppLogger.info("NetMgr", "Loop: Retrying MQTT connection (Attempt " + String(_mqttRetryCount + 1) + ")");
                    _lastMqttReconnectAttemptTime = currentTime;
                    if (_connectMqtt()) {
                        // Success, _connectMqtt resets retry counters
                    } else {
                        _mqttRetryCount++; 
                        _currentMqttRetryIntervalMs = min(MAX_RETRY_INTERVAL_MS, _currentMqttRetryIntervalMs * 2);
                        _nextMqttRetryTime = currentTime + _currentMqttRetryIntervalMs;
                         AppLogger.warning("NetMgr", "Loop: MQTT retry failed. Next attempt in " + String(_currentMqttRetryIntervalMs / 1000) + "s.");
                    }
                } else {
                    AppLogger.error("NetMgr", "Loop: Max MQTT retry attempts reached. Pausing for a while.");
                    _isAttemptingMqttReconnect = false; 
                    _nextMqttRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; 
                }
            } else if (!_isAttemptingMqttReconnect && currentTime >= _nextMqttRetryTime && _mqttRetryCount >= MAX_MQTT_RETRY_ATTEMPTS) {
                 AppLogger.info("NetMgr", "Loop: Re-enabling MQTT connection attempts.");
                 _isAttemptingMqttReconnect = true;
                 _nextMqttRetryTime = currentTime; 
                 _mqttRetryCount = 0;
                 _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
            }
        } else { 
            _mqttClient.loop(); 
        }
    } else if (!_wifiConnected && _mqttConnected) { 
        AppLogger.warning("NetMgr", "Loop: MQTT is connected but WiFi is not. Correcting MQTT state.");
        _mqttConnected = false;
        _mqttClient.disconnect();
        _isAttemptingMqttReconnect = false; 
    }

    // --- NTP Time Sync Update ---
    if(_wifiConnected && !_configPortalActive && !_ntpServerList.empty()) {
        bool tryForceSync = !_timeSync && (currentTime - _lastNtpSyncAttempt > NTP_FORCE_SYNC_RETRY_INTERVAL_MS || _lastNtpSyncAttempt == 0);
        bool tryPeriodicRefresh = _timeSync && (currentTime - _lastSuccessfulNtpSync > NTP_SYNC_INTERVAL_MS);

        if (tryForceSync || tryPeriodicRefresh) {
            if (tryForceSync) AppLogger.info("NetMgr", "NTP: Attempting time sync with server: " + String(_ntpServerList[_currentNtpServerIndex].c_str()));
            if (tryPeriodicRefresh) AppLogger.info("NetMgr", "NTP: Attempting periodic time refresh with server: " + String(_ntpServerList[_currentNtpServerIndex].c_str()));
            
            if (_timeClient.forceUpdate()) { 
                _timeSync = true;
                _lastSuccessfulNtpSync = currentTime;
                time_t epochTime = _timeClient.getEpochTime();
                struct timeval tv = { .tv_sec = epochTime, .tv_usec = 0 };
                if (settimeofday(&tv, nullptr) == 0) {
                    char timeStr[30];
                    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&epochTime));
                    AppLogger.info("NetMgr", "NTP: System time set to: " + String(timeStr) + " (Server: " + String(_ntpServerList[_currentNtpServerIndex].c_str()) + ")");
                } else {
                    AppLogger.error("NetMgr", "NTP: Failed to set system time via settimeofday.");
                }
                _lastNtpSyncAttempt = currentTime; // Record attempt time, successful or not for settimeofday
            } else {
                AppLogger.warning("NetMgr", "NTP: forceUpdate() failed with server: " + String(_ntpServerList[_currentNtpServerIndex].c_str()));
                _currentNtpServerIndex = (_currentNtpServerIndex + 1) % _ntpServerList.size();
                AppLogger.info("NetMgr", "NTP: Switching to next server: " + String(_ntpServerList[_currentNtpServerIndex].c_str()));
                _timeClient.end(); // Stop UDP client before changing server
                _timeClient.setPoolServerName(_ntpServerList[_currentNtpServerIndex].c_str());
                _timeClient.begin(); // Re-initialize UDP client
                _lastNtpSyncAttempt = currentTime; 
            }
        } else if (_timeSync) {
            // If already synced and not time for a forced refresh, 
            // _timeClient.update() can make minor adjustments if the library supports it non-blockingly.
            // However, typical NTPClient::update() is often the same as forceUpdate() or does little without it.
            // For now, we rely on periodic forceUpdate(). This line can be removed if update() is not useful.
            // _timeClient.update(); 
        }
    }
}

bool NetworkManager::publish(const char* topic, const char* payload) {
    if (!isConnected()) {
        AppLogger.warning("NetMgr", "MQTT: Cannot publish, network not fully connected. Topic: " + String(topic));
        return false;
    }

    int currentState = _mqttClient.state();
    AppLogger.debug("NetMgr", "MQTT: Attempting to publish. Topic: '" + String(topic) + "', Payload len: " + String(strlen(payload)) + ", Current MQTT State: " + String(currentState));

    // Check payload size against the buffer size (1024 in this case)
    // PubSubClient's default MQTT_MAX_PACKET_SIZE is 256. If setBufferSize(1024) was used, this check is against that.
    const int mqttOverheadEstimate = 50; // Estimate for topic name, QoS, etc.
    if (strlen(payload) > (1024 - mqttOverheadEstimate)) { 
         AppLogger.warning("NetMgr", "MQTT: Payload for topic '" + String(topic) + "' might be too large for buffer (1024 bytes). Length: " + String(strlen(payload)));
    }
    
    bool success = _mqttClient.publish(topic, payload);

    if (!success) {
        int stateAfterFail = _mqttClient.state();
        AppLogger.error("NetMgr", "MQTT: Publish failed! Topic: '" + String(topic) + "', MQTT State after fail: " + String(stateAfterFail));

        // If publish fails, assume connection is compromised and disconnect to allow robust reconnection.
        if (_mqttConnected) { // Only if we previously thought we were connected
            AppLogger.warning("NetMgr", "MQTT: Publish failure detected. Forcing MQTT disconnect and scheduling reconnect.");
            _mqttClient.disconnect(); // This sets PubSubClient state to MQTT_DISCONNECTED (-1) and closes socket.
            _mqttConnected = false; // Update our manager's state
            
            // Schedule reconnection attempt
            _isAttemptingMqttReconnect = true;
            _nextMqttRetryTime = millis(); // Try to reconnect in the next loop iteration
            _mqttRetryCount = 0;           // Reset retry count for a fresh series of attempts
            _currentMqttRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
        }
    }
    // AppLogger.debug("NetMgr", "MQTT: Publishing to: " + String(topic)); 
    return success;
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
bool NetworkManager::isAttemptingWifiReconnect() const { 
    // Considered attempting reconnect if in CONNECTING state OR if DISCONNECTED but _isAttemptingWifiReconnect flag is true
    return _wifiConnectionState == WIFI_STATE_CONNECTING || (_wifiConnectionState == WIFI_STATE_DISCONNECTED && _isAttemptingWifiReconnect);
}

bool NetworkManager::isAttemptingMqttReconnect() const {
    return _isAttemptingMqttReconnect;
}

int NetworkManager::getMqttState() { return _mqttClient.state(); }

// Add getter for the new state, if needed for external logic (e.g. LED status)
WifiConnectionState NetworkManager::getWifiConnectionState() const { return _wifiConnectionState; }

// Refined syncTime: Tries to update time once and returns status.
// The main retry logic and system time setting is in NetworkManager::begin().
bool NetworkManager::syncTime() {
    if(!_wifiConnected) {
        // AppLogger.warning("NetMgr", "Cannot sync time, WiFi not connected."); // Already logged by caller
        return false;
    }
    // _timeClient.begin() should have been called once before in NetworkManager::begin()
    // AppLogger.debug("NetMgr", "syncTime() calling _timeClient.forceUpdate().");
    bool success = _timeClient.forceUpdate();
    if (success) {
        // AppLogger.debug("NetMgr", "_timeClient.forceUpdate() successful in syncTime(). Epoch: " + String(_timeClient.getEpochTime()));
        _timeSync = true; // Mark that at least one sync attempt was good
    } else {
        // AppLogger.warning("NetMgr", "_timeClient.forceUpdate() failed in syncTime().");
    }
    return success;
}

// THÊM VÀO: Implement các hàm tiện ích
bool NetworkManager::isConfigPortalActive() const {
    return _configPortalActive;
}

IPAddress NetworkManager::getLocalIP() const {
    if (_wifiConnected && !_configPortalActive) {
        return WiFi.localIP();
    }
    return IPAddress(0,0,0,0);
}

IPAddress NetworkManager::getSoftAPIP() const {
    if (_configPortalActive) {
        return WiFi.softAPIP();
    }
    return IPAddress(0,0,0,0);
}

// PHẦN 2: startConfigPortal / stopConfigPortal
void NetworkManager::startConfigPortal() {
    if (_configPortalActive) {
        AppLogger.info("NetMgr", "Config portal is already active.");
        return;
    }
    AppLogger.info("NetMgr", "Starting Config Portal (AP Mode). SSID: " + String(AP_SSID));

    WiFi.disconnect(true); // Disconnect STA if connected
    delay(100); // allow disconnect to settle
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD); 
    delay(100); // allow AP to start
    
    IPAddress apIP = WiFi.softAPIP();
    AppLogger.info("NetMgr", "AP IP address: " + apIP.toString());

    _configPortalActive = true;
    _wifiConnected = false; 
    _mqttConnected = false;
    _wifiConnectionState = WIFI_STATE_DISCONNECTED; // AP mode is not STA "connected"
    _isAttemptingWifiReconnect = false; // Stop STA attempts while portal is active
    AppLogger.info("NetMgr", "Config Portal (AP Mode) started. Web server available.");
}

void NetworkManager::stopConfigPortal() {
    if (!_configPortalActive) {
        return;
    }
    AppLogger.info("NetMgr", "Stopping Config Portal (AP Mode)...");
    WiFi.softAPdisconnect(true);
    delay(100);
    _configPortalActive = false;
    
    AppLogger.info("NetMgr", "Config Portal stopped. Switching to STA mode.");
    WiFi.mode(WIFI_STA); 
    delay(100);
    
    _wifiConnectionState = WIFI_STATE_DISCONNECTED; 
    _isAttemptingWifiReconnect = true; // Signal that we want to connect to WiFi STA
    _nextWifiRetryTime = millis();     // Try to connect immediately
    _wifiRetryCount = 0;               
    _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
    // The main loop() will now pick up _isAttemptingWifiReconnect and call _initiateWifiConnection
}


// PHẦN 3: Web Server Request Handlers
void NetworkManager::_serveStaticFile(AsyncWebServerRequest *request, const char* path, const char* contentType) {
    if (!SPIFFS.exists(path)) {
        AppLogger.error("WebServer", "Static file not found: " + String(path));
        request->send(404, "text/plain", "File Not Found");
        return;
    }
    
    File file = SPIFFS.open(path, "r");
    if (!file) {
        AppLogger.error("WebServer", "Failed to open static file: " + String(path));
        request->send(500, "text/plain", "Server Error: Could not open file");
        return;
    }
    
    // AsyncWebServer hỗ trợ gửi trực tiếp từ SPIFFS, cách này hiệu quả hơn
    request->send(SPIFFS, path, contentType);
    // AppLogger.debug("WebServer", "Served static file: " + String(path));
    // file.close(); // request->send(SPIFFS,...) sẽ tự đóng file
}

void NetworkManager::_handleRoot(AsyncWebServerRequest *request) {
    // Kiểm tra xem người dùng đã "đăng nhập" bằng cách cung cấp đúng mật khẩu qua query param chưa
    // Hoặc có thể dùng một cơ chế session đơn giản nếu muốn phức tạp hơn.
    // Hiện tại, sẽ phục vụ index.html trực tiếp, JS sẽ xử lý logic login.
    _serveStaticFile(request, "/index.html", "text/html");
}

void NetworkManager::_handleNotFound(AsyncWebServerRequest *request) {
    if (!request->url().endsWith(".ico") && !request->url().endsWith(".map")) {
         // AppLogger.warning("WebServer", String("Not found handler (detailed): Method=") + request->methodToString() + ", URL=" + request->url());
         String logMsg = "Not found handler (detailed): Method=";
         logMsg += request->methodToString(); 
         logMsg += ", URL=";                  
         logMsg += request->url();            
         AppLogger.warning("WebServer", logMsg);
    }
    request->send(404, "text/plain", "Not found.");
}

void NetworkManager::_handleSave(AsyncWebServerRequest *request) {
    String new_ssid_str = "";
    String new_pass_str = "";
    String new_mqtt_server_str = "";
    String new_mqtt_port_str = "";
    String new_api_key_str = "";

    if (request->hasParam("ssid", true)) {
        new_ssid_str = request->getParam("ssid", true)->value();
    }
    if (request->hasParam("pass", true)) {
        new_pass_str = request->getParam("pass", true)->value();
    }
    if (request->hasParam("mqtt_server", true)) {
        new_mqtt_server_str = request->getParam("mqtt_server", true)->value();
    }
    if (request->hasParam("mqtt_port", true)) {
        new_mqtt_port_str = request->getParam("mqtt_port", true)->value();
    }
    if (request->hasParam("api_key", true)) {
        new_api_key_str = request->getParam("api_key", true)->value();
    }

    AppLogger.info("WebServer", "Save request: SSID='" + new_ssid_str + 
                                "', MQTT_Server='" + new_mqtt_server_str + 
                                "', MQTT_Port='" + new_mqtt_port_str + 
                                "', API_Key specified: " + String(new_api_key_str.length() > 0 ? "Yes" : "No") +
                                ", Password specified: " + String(new_pass_str.length() > 0 ? "Yes" : "No"));

    if (new_ssid_str.length() > 0 && new_ssid_str.length() < sizeof(_targetSsid)) {
        strncpy(_targetSsid, new_ssid_str.c_str(), sizeof(_targetSsid) - 1);
        _targetSsid[sizeof(_targetSsid) - 1] = '\0';

        strncpy(_targetPassword, new_pass_str.c_str(), sizeof(_targetPassword) - 1);
        _targetPassword[sizeof(_targetPassword) - 1] = '\0';
        
        // Update MQTT Server if provided and valid
        if (new_mqtt_server_str.length() > 0 && new_mqtt_server_str.length() < sizeof(_mqttServer)) {
            strncpy(_mqttServer, new_mqtt_server_str.c_str(), sizeof(_mqttServer) - 1);
            _mqttServer[sizeof(_mqttServer)-1] = '\0';
        } else if (new_mqtt_server_str.length() >= sizeof(_mqttServer)) {
            AppLogger.warning("WebServer", "MQTT Server string too long, not updated.");
        } // If empty, retain current _mqttServer (loaded from NVS or default)

        // Update MQTT Port if provided and valid
        if (new_mqtt_port_str.length() > 0) {
            int port_val = new_mqtt_port_str.toInt();
            if (port_val > 0 && port_val <= 65535) {
                _mqttPort = port_val;
            } else {
                AppLogger.warning("WebServer", "Invalid MQTT Port received, not updated.");
            }
        } // If empty, retain current _mqttPort

        // Update API Key if provided and valid
        if (new_api_key_str.length() > 0 && new_api_key_str.length() < sizeof(_apiKey)) {
            strncpy(_apiKey, new_api_key_str.c_str(), sizeof(_apiKey) - 1);
            _apiKey[sizeof(_apiKey)-1] = '\0';
        } else if (new_api_key_str.length() >= sizeof(_apiKey)) {
            AppLogger.warning("WebServer", "API Key string too long, not updated.");
        } // If empty, retain current _apiKey

        AppLogger.info("NetMgr", "New Config: SSID='" + String(_targetSsid) + 
                                   "', MQTT Server='" + String(_mqttServer) + ":" + String(_mqttPort) + 
                                   "', APIKey='" + String(_apiKey) + "'");
        
        _saveNetworkConfig(); // Save all current settings

        request->send(200, "text/plain", "Đã lưu cài đặt. Đang thử kết nối với WiFi mới...");
        
        AppLogger.info("NetMgr", "Deactivating AP mode (if active) and attempting connection to: " + String(_targetSsid));

        if (_configPortalActive) {
            WiFi.softAPdisconnect(true); // Stop AP
            delay(100); // Allow time for AP to shut down
            _configPortalActive = false;
        }
        
        // Ensure STA mode is explicitly set before attempting connection
        WiFi.mode(WIFI_STA);
        delay(100); // Allow time for mode switch

        // Reset WiFi connection state and attempt connection
        _wifiConnected = false; 
        _mqttConnected = false; // MQTT will need to reconnect after WiFi
        _wifiRetryCount = 0; 
        _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
        _isAttemptingWifiReconnect = true; 
        _nextWifiRetryTime = millis(); // Try immediately

    } else {
        AppLogger.warning("WebServer", "Invalid SSID received from save request.");
        request->send(400, "text/plain", "Lỗi: Tên WiFi (SSID) không hợp lệ hoặc quá dài.");
    }
}

// NEW: Handler for /getconfig
void NetworkManager::_handleGetConfig(AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc; // Adjust size as needed
    doc["ssid"] = String(_targetSsid);
    // Do not send back the password for security reasons, even if stored.
    // The client should always re-enter it if they want to change it.
    // doc["pass"] = String(_targetPassword); // OMITTED FOR SECURITY
    doc["mqtt_server"] = String(_mqttServer);
    doc["mqtt_port"] = _mqttPort;
    doc["api_key"] = String(_apiKey);

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
    AppLogger.info("WebServer", "Sent current config (excluding password).");
}

// NEW: Handler for /getsysteminfo
void NetworkManager::_handleGetSystemInfo(AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc; // Adjust size as needed

    uint64_t chipId = ESP.getEfuseMac();
    char deviceIdStr[18]; // 17 chars for MAC + null terminator
    snprintf(deviceIdStr, sizeof(deviceIdStr), "%04X%08X", 
             (uint16_t)(chipId >> 32),    // High 2 bytes
             (uint32_t)chipId);            // Low 4 bytes
    doc["deviceId"] = String(deviceIdStr);

    doc["firmwareVersion"] = "1.0.1"; // Example version, make this dynamic if needed

    if (isConfigPortalActive()) {
        doc["wifiStatus"] = "AP Mode Active";
        doc["ipAddress"] = getSoftAPIP().toString();
    } else if (isWifiConnected()) {
        doc["wifiStatus"] = "Connected to " + String(_targetSsid);
        doc["ipAddress"] = getLocalIP().toString();
    } else {
        doc["wifiStatus"] = "Disconnected";
        doc["ipAddress"] = "N/A";
    }

    doc["mqttStatus"] = isMqttConnected() ? "Connected" : "Disconnected";
    
    if (_timeSync) {
        doc["systemTime"] = _timeClient.getFormattedTime(); // Assumes NTPClient provides formatted time
    } else {
        doc["systemTime"] = "NTP not synced";
    }

    doc["freeHeap"] = ESP.getFreeHeap();
    doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    // Add uptime if desired
    // unsigned long uptimeMillis = millis();
    // unsigned long uptimeSeconds = uptimeMillis / 1000;
    // unsigned long uptimeMinutes = uptimeSeconds / 60;
    // unsigned long uptimeHours = uptimeMinutes / 60;
    // doc["uptime"] = String(uptimeHours) + "h " + String(uptimeMinutes % 60) + "m " + String(uptimeSeconds % 60) + "s";

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
    AppLogger.info("WebServer", "Sent system info.");
}

// Renamed from _loadCredentials and expanded
bool NetworkManager::_loadNetworkConfig() {
    // Load WiFi SSID
    String nvs_ssid = _preferences.getString("wifi_ssid", "");
    if (nvs_ssid.length() > 0) {
        strncpy(_targetSsid, nvs_ssid.c_str(), sizeof(_targetSsid) -1);
        _targetSsid[sizeof(_targetSsid)-1] = '\0';
        AppLogger.info("NVM", "Loaded SSID from NVS: '" + String(_targetSsid) + "'");
    } else {
        _targetSsid[0] = '\0'; // Ensure it's empty if not in NVS
        AppLogger.info("NVM", "No SSID found in NVS.");
    }

    // Load WiFi Password
    String nvs_pass = _preferences.getString("wifi_pass", "");
    // Password can be empty, so we always copy what's there (or empty string if key doesn't exist)
    strncpy(_targetPassword, nvs_pass.c_str(), sizeof(_targetPassword) -1);
    _targetPassword[sizeof(_targetPassword)-1] = '\0';
    // AppLogger.info("NVM", "Loaded Password from NVS (length: " + String(nvs_pass.length()) + ")");


    // Load MQTT Server
    String nvs_mqtt_server = _preferences.getString("mqtt_server", DEFAULT_MQTT_SERVER);
    strncpy(_mqttServer, nvs_mqtt_server.c_str(), sizeof(_mqttServer) -1);
    _mqttServer[sizeof(_mqttServer)-1] = '\0';
    AppLogger.info("NVM", "MQTT Server: '" + String(_mqttServer) + (nvs_mqtt_server == DEFAULT_MQTT_SERVER && !_preferences.isKey("mqtt_server") ? "' (Default)" : "' (From NVS)"));
    
    // Load MQTT Port
    _mqttPort = _preferences.getInt("mqtt_port", DEFAULT_MQTT_PORT);
    AppLogger.info("NVM", "MQTT Port: " + String(_mqttPort) + (!_preferences.isKey("mqtt_port") ? " (Default)" : " (From NVS)"));

    // Load API Key
    String nvs_api_key = _preferences.getString("api_key", DEFAULT_API_KEY);
    strncpy(_apiKey, nvs_api_key.c_str(), sizeof(_apiKey) -1);
    _apiKey[sizeof(_apiKey)-1] = '\0';
    AppLogger.info("NVM", "API Key: '" + String(_apiKey) + (nvs_api_key == DEFAULT_API_KEY && !_preferences.isKey("api_key") ? "' (Default)" : "' (From NVS)"));

    return nvs_ssid.length() > 0; // Return true if an SSID was actually loaded from NVS
}

// Renamed from _saveCredentials and expanded
void NetworkManager::_saveNetworkConfig() {
    AppLogger.info("NVM", "Attempting to save network configuration to NVS...");
    bool success = true;

    if (!_preferences.putString("wifi_ssid", _targetSsid)) {
        AppLogger.error("NVM", "Failed to save SSID."); success = false;
    }

    // Attempt to remove the key first, in case it's corrupted
    _preferences.remove("wifi_pass"); 
    // We don't strictly need to check the return of remove() here for this attempt;
    // if putString still fails, it will be logged.

    if (!_preferences.putString("wifi_pass", _targetPassword)) {
        AppLogger.error("NVM", "Failed to save WiFi Password."); success = false;
    }
    if (!_preferences.putString("mqtt_server", _mqttServer)) {
        AppLogger.error("NVM", "Failed to save MQTT Server."); success = false;
    }
    if (!_preferences.putInt("mqtt_port", _mqttPort)) {
        AppLogger.error("NVM", "Failed to save MQTT Port."); success = false;
    }
    if (!_preferences.putString("api_key", _apiKey)) {
        AppLogger.error("NVM", "Failed to save API Key."); success = false;
    }

    if (success) {
        AppLogger.info("NVM", "Network configuration saved to NVS successfully.");
    } else {
        AppLogger.error("NVM", "One or more network configuration items failed to save to NVS.");
    }
}

String NetworkManager::getMqttServer() const {
    return String(_mqttServer);
}

int NetworkManager::getMqttPort() const {
    return _mqttPort;
}

String NetworkManager::getApiKey() const {
    return String(_apiKey);
}

// ... (rest of NetworkManager.cpp) ... 