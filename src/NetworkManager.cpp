#include "../include/NetworkManager.h"
#include "../include/Logger.h"
// SPIFFS đã được include trong .h, nhưng để rõ ràng có thể thêm ở đây nếu muốn.
// #include <SPIFFS.h> 
// ArduinoJson cũng đã có trong .h
// #include <ArduinoJson.h>

// Định nghĩa các hằng số đã được khai báo là extern trong .h
const char* AP_SSID = "ESP32-Config";
const char* AP_PASSWORD = "password123"; // Mật khẩu cho AP (có thể để rỗng "" nếu muốn mạng mở)
const int WEB_SERVER_PORT = 80;
const char* ADMIN_LOGIN_PASSWORD = "admin123";

// THÊM VÀO: Default values for MQTT and API Key
const char* DEFAULT_MQTT_SERVER = "karis.cloud";
const int DEFAULT_MQTT_PORT = 1883;
const char* DEFAULT_API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2";

NetworkManager::NetworkManager() : 
    _timeClient(_ntpUDP), // Initialize _timeClient without server first
    _server(WEB_SERVER_PORT), // Khởi tạo AsyncWebServer với port đã định nghĩa
    _configPortalActive(false) // Khởi tạo trạng thái portal
{
    _wifiConnected = false;
    _mqttConnected = false;
    _timeSync = false;
    _currentNtpServerIndex = 0;

    // Populate NTP server list (prioritize Vietnam, then Asia, then global)
    _ntpServerList.push_back("vn.pool.ntp.org");
    _ntpServerList.push_back("0.vn.pool.ntp.org");
    _ntpServerList.push_back("1.ntp.vnix.vn");    // VNNIC Server 1
    _ntpServerList.push_back("2.ntp.vnix.vn");    // VNNIC Server 2
    _ntpServerList.push_back("0.asia.pool.ntp.org");
    _ntpServerList.push_back("1.asia.pool.ntp.org");
    _ntpServerList.push_back("pool.ntp.org");      // Global fallback
    _ntpServerList.push_back("time.nist.gov");    // NIST as another reliable global option

    // Now configure the timeClient with the first server and offset
    // Note: NTPClient might not have a separate method to set offset after construction without also setting server.
    // So, we re-construct/re-assign if necessary, or ensure its internal setters allow this.
    // For now, assuming we can set pool server name and time offset independently if needed or handle it in begin.
    // The constructor NTPClient(udp, server, offset) is common. Let's re-init with first server.
    // The default constructor NTPClient(udp) is used above, setServerName and setTimeOffset will be used in begin().

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

    // Initialize char arrays to be safe
    _targetSsid[0] = '\0';
    _targetPassword[0] = '\0';
    _mqttServer[0] = '\0';
    _apiKey[0] = '\0'; // Initialize new apiKey
    _mqttPort = 0;

    // Create random clientId with timestamp for uniqueness
    uint32_t random_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    uint32_t timestamp = millis();
    snprintf(_clientId, sizeof(_clientId), "ESP32Client-%06X-%u", random_id, timestamp % 1000000);
    
    // AppLogger.info("NetMgr", "Generated MQTT Client ID: " + String(_clientId)); // Changed from Serial.println
    // Logging here might be too early if AppLogger is not ready, defer if issues arise.

    // KHÔNG khởi tạo _targetSsid, _targetPassword ở đây, sẽ nhận từ begin() hoặc NVS
}

bool NetworkManager::begin(const char* initial_ssid, const char* initial_password) {
    // Ensure AppLogger is available for critical logs from this point onwards.
    // The actual AppLogger.begin() is called in main.cpp setup() before networkManager.begin().
    AppLogger.info("NetMgr", "NetworkManager::begin() called.");
    AppLogger.info("NetMgr", "Generated MQTT Client ID: " + String(_clientId)); // Log Client ID here

    // Initialize Preferences here
    if (!_preferences.begin("net-config", false)) {
        AppLogger.warning("NetMgr", "NVM: Failed to initialize Preferences in NetworkManager::begin(). Configs might not be saved/loaded.");
    } else {
        AppLogger.info("NetMgr", "NVM: Preferences initialized successfully in NetworkManager::begin().");
    }

    // Load all network configurations
    bool loaded_from_nvs = _loadNetworkConfig();

    // If NVS is empty for SSID, and initial_ssid is provided, use it.
    // This handles the case where main.cpp might pass a default if nothing is in NVS.
    // However, for MQTT/API Key, _loadNetworkConfig will set defaults if NVS is empty.
    if (!loaded_from_nvs || strlen(_targetSsid) == 0) {
        if (initial_ssid && strlen(initial_ssid) > 0) {
            strncpy(_targetSsid, initial_ssid, sizeof(_targetSsid) - 1);
            _targetSsid[sizeof(_targetSsid) - 1] = '\0';
            if (initial_password) { // Password can be empty
                strncpy(_targetPassword, initial_password, sizeof(_targetPassword) - 1);
                _targetPassword[sizeof(_targetPassword) - 1] = '\0';
            } else {
                _targetPassword[0] = '\0';
            }
            AppLogger.info("NetMgr", "Using initial parameters for WiFi credentials (NVS was empty or no SSID).");
        } else {
            AppLogger.info("NetMgr", "No credentials in NVS or from initial parameters. Config portal will be primary for WiFi.");
            // _targetSsid will be empty, portal will be triggered
        }
    }
    
    AppLogger.info("NetMgr", "Current Config: SSID='" + String(_targetSsid) + "', MQTT Server='" + String(_mqttServer) + ":" + String(_mqttPort) + "', APIKey='" + String(_apiKey) + "'");

    // Initialize SPIFFS
    AppLogger.info("NetMgr", "Initializing SPIFFS...");
    if (!SPIFFS.begin(true)) { // true = format SPIFFS if mount failed
        AppLogger.error("NetMgr", "SPIFFS Mount Failed. Configuration portal might not work.");
        // Có thể quyết định dừng ở đây hoặc tiếp tục mà không có web files
    } else {
        AppLogger.info("NetMgr", "SPIFFS mounted successfully.");
        // Liệt kê files trong SPIFFS (debug)
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while(file){
            AppLogger.debug("NetMgr", "SPIFFS File: " + String(file.name()) + ", Size: " + String(file.size()));
            file = root.openNextFile();
        }
        root.close();
    }

    // THAY ĐỔI: Đăng ký các route cho web server và khởi động server MỘT LẦN ở đây
    AppLogger.info("NetMgr", "Setting up AsyncWebServer routes...");
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET / - Serving index.html");
        this->_handleRoot(request);
    });
    _server.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET /style.css");
        this->_serveStaticFile(request, "/style.css", "text/css");
    });
    _server.on("/script.js", HTTP_GET, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET /script.js");
        this->_serveStaticFile(request, "/script.js", "text/javascript");
    });
    _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "POST /save");
        this->_handleSave(request);
    });
    _server.on("/getconfig", HTTP_GET, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET /getconfig");
        this->_handleGetConfig(request);
    });
    _server.on("/getsysteminfo", HTTP_GET, [this](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET /getsysteminfo");
        this->_handleGetSystemInfo(request);
    });
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        AppLogger.debug("WebServer", "GET /favicon.ico - Sending 204 No Content");
        request->send(204);
    });
    _server.onNotFound([this](AsyncWebServerRequest *request){
        AppLogger.warning("WebServer", String("Not Found URL (from onNotFound handler): ") + request->url());
        this->_handleNotFound(request); // _handleNotFound sẽ log chi tiết hơn
    });

    // --- NEW: Ensure WiFi/LwIP stack is initialized before starting server ---
    AppLogger.debug("NetMgr", "Setting WiFi mode to STA to ensure LwIP is initialized.");
    WiFi.mode(WIFI_STA); // This can help ensure TCP/IP stack is ready
    delay(100); // Small delay for mode change to settle
    // --- END NEW ---

    _server.begin();
    AppLogger.info("NetMgr", "AsyncWebServer started on port " + String(WEB_SERVER_PORT) + ". It will remain active.");

    // Thử kết nối với credentials hiện có (nếu có)
    bool connectedViaStoredCredentials = false;
    if (strlen(_targetSsid) > 0) {
        AppLogger.info("NetMgr", "Attempting to connect with stored/initial credentials...");
        if (_connectWifi(_targetSsid, _targetPassword)) { // Sử dụng _targetSsid, _targetPassword
            connectedViaStoredCredentials = true;
        }
    }

    if (connectedViaStoredCredentials) {
        // NTP Synchronization with retries and server failover
        // _timeClient.begin(); // Initialize the NTP client - NO, will be called per server
        _timeClient.setTimeOffset(7 * 3600); // Set Vietnam offset (GMT+7) once

        AppLogger.info("NetMgr", "Attempting NTP time synchronization with server failover...");
        int ntp_global_retries = 0;
        const int max_ntp_global_retries = _ntpServerList.size() * NTP_ATTEMPTS_PER_SERVER; // Max total attempts across all servers
        bool ntp_synced = false;

        while(!ntp_synced && ntp_global_retries < max_ntp_global_retries) {
            const char* current_server_name = _ntpServerList[_currentNtpServerIndex].c_str();
            AppLogger.info("NetMgr", "NTP: Using server: " + String(current_server_name) + " (Attempt " + String(ntp_global_retries + 1) + "/" + String(max_ntp_global_retries) + ")");
            
            _timeClient.setPoolServerName(current_server_name); // Set server for NTPClient
            _timeClient.begin(); // Initialize UDP for the current server. Important for server changes.

            int attempts_on_this_server = 0;
            while(attempts_on_this_server < NTP_ATTEMPTS_PER_SERVER && !ntp_synced) {
                AppLogger.debug("NetMgr", "NTP: Attempt " + String(attempts_on_this_server + 1) + "/" + String(NTP_ATTEMPTS_PER_SERVER) + " with " + String(current_server_name));
                if (_timeClient.forceUpdate()) {
                    AppLogger.info("NetMgr", "NTP: Time synchronized with " + String(current_server_name));
                    _timeSync = true;
                    ntp_synced = true;
                    time_t epochTime = _timeClient.getEpochTime(); 
                    struct timeval tv;
                    tv.tv_sec = epochTime;
                    tv.tv_usec = 0;
                    if (settimeofday(&tv, nullptr) == 0) {
                        AppLogger.info("NetMgr", "NTP: System time set to: " + _timeClient.getFormattedTime() + " (UTC)");
                        setenv("TZ", "Asia/Ho_Chi_Minh", 1);
                        tzset();
                        AppLogger.info("NetMgr", "NTP: Timezone set to Asia/Ho_Chi_Minh.");
                    } else {
                        AppLogger.error("NetMgr", "NTP: Failed to set system time.");
                    }
                    break; // Break from inner loop (attempts_on_this_server)
                } else {
                    AppLogger.warning("NetMgr", "NTP: Sync attempt " + String(attempts_on_this_server + 1) + " failed with " + String(current_server_name));
                    attempts_on_this_server++;
                    ntp_global_retries++; // Also increment global counter here
                    if (attempts_on_this_server < NTP_ATTEMPTS_PER_SERVER && ntp_global_retries < max_ntp_global_retries) {
                        delay(1000); // Wait 1 sec before quick retry on same server
                    }
                }
            } // End while attempts_on_this_server

            if (!ntp_synced) {
                _currentNtpServerIndex = (_currentNtpServerIndex + 1) % _ntpServerList.size();
                if (ntp_global_retries < max_ntp_global_retries) {
                     AppLogger.warning("NetMgr", "NTP: Failed to sync with " + String(current_server_name) + " after " + String(NTP_ATTEMPTS_PER_SERVER) + " attempts. Trying next server.");
                     delay(2000); // Wait 2 secs before trying next server
                }
            } else {
                // NTP Synced, break the main while loop
                break;
            }
        } // End while !ntp_synced && ntp_global_retries < max_ntp_global_retries

        if (!ntp_synced) {
            AppLogger.error("NetMgr", "NTP: Time synchronization failed after all attempts with all servers.");
        }
        
        _mqttClient.setClient(_wifiClient);
        _mqttClient.setServer(_mqttServer, _mqttPort);
        _mqttClient.setKeepAlive(60);
        _mqttClient.setSocketTimeout(15);
        _mqttClient.setBufferSize(1024);

        if (_connectMqtt()) {
            AppLogger.info("NetMgr", "Initial MQTT connection successful.");
            return true;
        }
        AppLogger.error("NetMgr", "Initial MQTT connection failed after connecting with stored credentials.");
        _handleMqttDisconnect(); 
        return false; // MQTT failed, but WiFi is up
    } else {
        AppLogger.info("NetMgr", "No valid WiFi credentials or connection failed. Activating Config Portal (AP Mode).");
        startConfigPortal(); // Server đã chạy, chỉ cần kích hoạt AP
        return false; 
    }
}

bool NetworkManager::_connectWifi(const char* ssid_to_connect, const char* password_to_connect) {
    if (!ssid_to_connect || strlen(ssid_to_connect) == 0) {
        AppLogger.warning("NetMgr", "WiFi connection attempt with empty SSID.");
    return false;
}
    AppLogger.info("NetMgr", "Connecting to WiFi SSID: " + String(ssid_to_connect));
    _isAttemptingWifiReconnect = true; // Giữ cờ này cho logic retry tổng thể
    
    // Ngắt kết nối AP mode nếu đang chạy
    if (_configPortalActive) {
         AppLogger.info("NetMgr", "Config portal was active, stopping it to attempt STA connection.");
         // Không gọi stopConfigPortal() trực tiếp ở đây để tránh gọi lại _connectWifi() không cần thiết
         // Chỉ cần đảm bảo AP tắt và mode là STA
         WiFi.softAPdisconnect(true);
         WiFi.mode(WIFI_STA);
         _configPortalActive = false; // Cập nhật trạng thái
    } else {
        // Nếu không phải từ config portal, đảm bảo mode là STA
        if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) { // WIFI_AP_STA cũng ok
             WiFi.mode(WIFI_STA);
        }
    }
    WiFi.disconnect(true); // Đảm bảo ngắt kết nối cũ trước khi thử mới

    WiFi.begin(ssid_to_connect, password_to_connect);
    
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
    }
    
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
    
    if (_configPortalActive) {
        AppLogger.info("NetMgr", "WiFi disconnected but Config Portal is active. Not attempting STA reconnect now.");
        _isAttemptingWifiReconnect = false; // Don't try to reconnect STA while AP is up
        return;
    }

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
                if (_connectWifi(_targetSsid, _targetPassword)) { // Sử dụng _targetSsid/_targetPassword cho retry
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
                AppLogger.error("NetMgr", "Max WiFi retry attempts reached. Stopping STA attempts.");
                // THAY ĐỔI: Thay vì chỉ đợi lâu, có thể quyết định mở Config Portal ở đây
                // _nextWifiRetryTime = currentTime + MAX_RETRY_INTERVAL_MS * 2; 
                // _wifiRetryCount = 0; 
                // _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
                AppLogger.info("NetMgr", "Starting Config Portal due to repeated WiFi failures.");
                startConfigPortal(); // Kích hoạt Config Portal
                _isAttemptingWifiReconnect = false; // Dừng các nỗ lực kết nối STA
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
    if(_wifiConnected && _timeSync && !_configPortalActive) _timeClient.update();
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
        AppLogger.debug("NetMgr", "Config portal (AP mode) already active.");
        return;
    }

    AppLogger.info("NetMgr", "Activating WiFi Configuration Portal (AP Mode)...");
    
    _isAttemptingWifiReconnect = false;
    _isAttemptingMqttReconnect = false;
    if (_wifiConnected) { // Nếu đang kết nối STA, ngắt đi
        WiFi.disconnect(true);
        _wifiConnected = false;
        _mqttConnected = false; 
    }
    
    WiFi.mode(WIFI_AP);
    if (strlen(AP_PASSWORD) > 0) {
        WiFi.softAP(AP_SSID, AP_PASSWORD);
    } else {
        WiFi.softAP(AP_SSID);
    }

    delay(500); 
    AppLogger.info("NetMgr", "AP Mode Started. SSID: " + String(AP_SSID) + ", IP: " + WiFi.softAPIP().toString());
    AppLogger.info("NetMgr", "Web server is already running. Access configuration at http://" + WiFi.softAPIP().toString());

    // KHÔNG đăng ký route hay _server.begin() ở đây nữa.
    _configPortalActive = true;
}

void NetworkManager::stopConfigPortal() {
    if (!_configPortalActive) {
        AppLogger.debug("NetMgr", "Config portal (AP mode) not active, nothing to stop.");
        return;
    }

    AppLogger.info("NetMgr", "Deactivating WiFi Configuration Portal (AP Mode)...");
    // KHÔNG _server.end() ở đây. Server tiếp tục chạy.
    WiFi.softAPdisconnect(true); 
    delay(100); 

    _configPortalActive = false;
    AppLogger.info("NetMgr", "AP Mode stopped. Web server remains active on STA IP if connected.");

    if (strlen(_targetSsid) > 0) {
        AppLogger.info("NetMgr", "Attempting to connect with current target credentials after AP mode stop.");
        _wifiRetryCount = 0; 
        _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
        _isAttemptingWifiReconnect = true;
        _nextWifiRetryTime = millis(); 
    } else {
        AppLogger.warning("NetMgr", "No target SSID to connect to after AP mode stop.");
        // Ở đây có thể cân nhắc: Nếu không có target SSID, có nên quay lại AP mode không?
        // Hiện tại: không, sẽ chờ loop() xử lý nếu cần.
    }
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
            _mqttServer[sizeof(_mqttServer) - 1] = '\0';
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
            _apiKey[sizeof(_apiKey) - 1] = '\0';
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