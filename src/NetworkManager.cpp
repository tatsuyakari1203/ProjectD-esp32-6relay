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

NetworkManager::NetworkManager() : 
    _timeClient(_ntpUDP, "pool.ntp.org", 7 * 3600), // 7 hours offset for Vietnam
    _server(WEB_SERVER_PORT), // Khởi tạo AsyncWebServer với port đã định nghĩa
    _configPortalActive(false) // Khởi tạo trạng thái portal
{
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
    
    // AppLogger.info("NetMgr", "Generated MQTT Client ID: " + String(_clientId)); // Changed from Serial.println
    // Logging here might be too early if AppLogger is not ready, defer if issues arise.

    // KHÔNG khởi tạo _targetSsid, _targetPassword ở đây, sẽ nhận từ begin() hoặc NVS
}

bool NetworkManager::begin(const char* initial_ssid, const char* initial_password, const char* mqttServer, int mqttPort) {
    // Ensure AppLogger is available for critical logs from this point onwards.
    // The actual AppLogger.begin() is called in main.cpp setup() before networkManager.begin().
    AppLogger.info("NetMgr", "NetworkManager::begin() called.");
    AppLogger.info("NetMgr", "Generated MQTT Client ID: " + String(_clientId)); // Log Client ID here

    // Initialize Preferences here
    if (!_preferences.begin("net-config", false)) {
        AppLogger.warning("NetMgr", "NVM: Failed to initialize Preferences in NetworkManager::begin(). WiFi credentials might not be saved/loaded.");
    } else {
        AppLogger.info("NetMgr", "NVM: Preferences initialized successfully in NetworkManager::begin().");
    }

    // Lưu trữ initial credentials (có thể là rỗng nếu chưa từng cài đặt)
    if (initial_ssid) strncpy(_targetSsid, initial_ssid, sizeof(_targetSsid) -1); else _targetSsid[0] = '\0';
    _targetSsid[sizeof(_targetSsid)-1] = '\0';
    if (initial_password) strncpy(_targetPassword, initial_password, sizeof(_targetPassword) -1); else _targetPassword[0] = '\0';
    _targetPassword[sizeof(_targetPassword)-1] = '\0';
    
    strncpy(_mqttServer, mqttServer, sizeof(_mqttServer) -1); _mqttServer[sizeof(_mqttServer)-1] = '\0';
    _mqttPort = mqttPort;

    AppLogger.info("NetMgr", "Initializing NetworkManager...");

    // THAY ĐỔI: Load credentials từ NVS trước
    String nvs_ssid, nvs_pass;
    bool loaded_from_nvs = _loadCredentials(nvs_ssid, nvs_pass);

    if (loaded_from_nvs) {
        strncpy(_targetSsid, nvs_ssid.c_str(), sizeof(_targetSsid) - 1);
        _targetSsid[sizeof(_targetSsid) - 1] = '\0';
        strncpy(_targetPassword, nvs_pass.c_str(), sizeof(_targetPassword) - 1);
        _targetPassword[sizeof(_targetPassword) - 1] = '\0';
        AppLogger.info("NetMgr", "Using WiFi credentials from NVS.");
    } else if (strlen(_targetSsid) > 0) {
        // Nếu không có gì trong NVS, nhưng có targetSsid được truyền vào (ví dụ từ main.cpp như một default cuối cùng)
        AppLogger.info("NetMgr", "Using initial parameters for WiFi credentials (NVS was empty).");
    } else {
        // Không có trong NVS và không có targetSsid -> targetSsid sẽ rỗng, sẽ kích hoạt portal
        _targetSsid[0] = '\0';
        _targetPassword[0] = '\0';
        AppLogger.info("NetMgr", "No credentials in NVS or from initial parameters. Config portal will be primary.");
    }

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
        // NTP Synchronization with retries
        _timeClient.begin(); // Initialize the NTP client
        AppLogger.info("NetMgr", "Attempting NTP time synchronization...");
        int ntp_retries = 0;
        const int max_ntp_retries = 10; // Try up to 10 times
        const unsigned long ntp_retry_interval_ms = 2000; // Wait 2 seconds between retries
        bool ntp_synced = false;

        while(!ntp_synced && ntp_retries < max_ntp_retries) {
            AppLogger.debug("NetMgr", "NTP sync attempt " + String(ntp_retries + 1) + " of " + String(max_ntp_retries) + "...");
            if (syncTime()) { // syncTime() likely calls _timeClient.forceUpdate()
            AppLogger.info("NetMgr", "Time synchronized via NTP.");
            _timeSync = true;
                ntp_synced = true;
                // Set system time using the epoch from NTPClient if syncTime() was successful
                // This assumes syncTime() updates the underlying NTPClient's state correctly.
                // And that _timeClient is the NTPClient instance.
                time_t epochTime = _timeClient.getEpochTime(); 
                struct timeval tv;
                tv.tv_sec = epochTime;
                tv.tv_usec = 0;
                if (settimeofday(&tv, nullptr) == 0) {
                    AppLogger.info("NetMgr", "System time set to: " + _timeClient.getFormattedTime() + " (UTC)");
                    // Now set the timezone environment variable for C library functions
                    setenv("TZ", "Asia/Ho_Chi_Minh", 1); // TZ for Vietnam GMT+7
                    tzset(); // Apply the TZ environment variable
                    AppLogger.info("NetMgr", "Timezone set to Asia/Ho_Chi_Minh. Local time functions should now be correct.");
                    // You can verify with a localtime call if needed for debugging here
                    // struct tm timeinfo_check;
                    // getLocalTime(&timeinfo_check, 0); // Get time from RTC immediately
                    // AppLogger.debug("NetMgr", "Sample local time: " + String(asctime(&timeinfo_check)));
                } else {
                    AppLogger.error("NetMgr", "Failed to set system time.");
                }
        } else {
                AppLogger.warning("NetMgr", "NTP sync attempt " + String(ntp_retries + 1) + " failed.");
                ntp_retries++;
                if (ntp_retries < max_ntp_retries) {
                    delay(ntp_retry_interval_ms);
                }
            }
        }

        if (!ntp_synced) {
            AppLogger.error("NetMgr", "NTP time synchronization failed after " + String(max_ntp_retries) + " retries.");
            // _timeSync remains false
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

    if (request->hasParam("ssid", true)) {
        new_ssid_str = request->getParam("ssid", true)->value();
    }
    if (request->hasParam("pass", true)) {
        new_pass_str = request->getParam("pass", true)->value();
    }

    AppLogger.info("WebServer", "Save request: SSID='" + new_ssid_str + "', Password specified: " + String(new_pass_str.length() > 0 ? "Yes" : "No"));

    if (new_ssid_str.length() > 0 && new_ssid_str.length() < sizeof(_targetSsid)) {
        strncpy(_targetSsid, new_ssid_str.c_str(), sizeof(_targetSsid) - 1);
        _targetSsid[sizeof(_targetSsid) - 1] = '\0';

        strncpy(_targetPassword, new_pass_str.c_str(), sizeof(_targetPassword) - 1);
        _targetPassword[sizeof(_targetPassword) - 1] = '\0';
        
        AppLogger.info("NetMgr", "New WiFi credentials received: SSID='" + String(_targetSsid) + "'");
        
        // THÊM VÀO: Lưu credentials vào NVS
        _saveCredentials(_targetSsid, _targetPassword);

        request->send(200, "text/plain", "Đã lưu cài đặt. Đang thử kết nối với WiFi mới...");
        
        AppLogger.info("NetMgr", "Deactivating AP mode (if active) and attempting connection to: " + String(_targetSsid));
        if (_configPortalActive) {
            WiFi.softAPdisconnect(true); 
            _configPortalActive = false;
            delay(100); 
        }
        
        _wifiRetryCount = 0; 
        _currentWifiRetryIntervalMs = INITIAL_RETRY_INTERVAL_MS;
        _isAttemptingWifiReconnect = true; 
        _nextWifiRetryTime = millis(); 

    } else {
        AppLogger.warning("WebServer", "Invalid SSID received from save request.");
        request->send(400, "text/plain", "Lỗi: Tên WiFi (SSID) không hợp lệ hoặc quá dài.");
    }
}

// THÊM VÀO: Implement _loadCredentials
bool NetworkManager::_loadCredentials(String& ssid, String& password) {
    if (!_preferences.isKey("wifi_ssid")) {
        AppLogger.info("NVM", "No SSID found in NVS.");
        ssid = "";
        password = "";
        return false; // Không có key, nghĩa là chưa lưu gì
    }
    ssid = _preferences.getString("wifi_ssid", "");
    password = _preferences.getString("wifi_pass", "");
    AppLogger.info("NVM", "Loaded SSID from NVS: '" + ssid + "'");
    // Không log mật khẩu
    return ssid.length() > 0; // Trả về true nếu SSID không rỗng
}

// THÊM VÀO: Implement _saveCredentials
void NetworkManager::_saveCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || password == nullptr) {
        AppLogger.error("NVM", "Cannot save null SSID or Password.");
        return;
    }
    bool ssid_saved = _preferences.putString("wifi_ssid", ssid);
    bool pass_saved = _preferences.putString("wifi_pass", password);

    if (ssid_saved && pass_saved) {
        AppLogger.info("NVM", "WiFi credentials saved to NVS: SSID='" + String(ssid) + "'");
    } else {
        AppLogger.error("NVM", "Failed to save WiFi credentials to NVS.");
        if(!ssid_saved) AppLogger.error("NVM", "SSID save failed.");
        if(!pass_saved) AppLogger.error("NVM", "Password save failed.");
    }
    // preferences.end(); // Không cần end() nếu muốn giữ nó mở, nhưng nếu chỉ dùng ở đây thì có thể end()
    // Tốt hơn là giữ Preferences mở trong suốt thời gian chạy của NetworkManager.
}

// ... (rest of NetworkManager.cpp) ... 