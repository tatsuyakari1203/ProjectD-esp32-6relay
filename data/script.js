document.addEventListener('DOMContentLoaded', function() {
    const loginSection = document.getElementById('login-section');
    const configSection = document.getElementById('config-section');
    const adminPasswordInput = document.getElementById('adminPassword');
    const loginButton = document.getElementById('loginButton');
    const loginStatus = document.getElementById('login-status');
    const logoutButton = document.getElementById('logoutButton'); // Added for logout

    // New: System Info Section
    const systemInfoSection = document.getElementById('system-info-section');

    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('pass');
    const saveButton = document.getElementById('saveButton');
    const statusDisplay = document.getElementById('status');

    // New input fields for MQTT and API Key
    const mqttServerInput = document.getElementById('mqtt_server');
    const mqttPortInput = document.getElementById('mqtt_port');
    const apiKeyInput = document.getElementById('api_key');

    // System Info Elements
    const infoDeviceId = document.getElementById('info-device-id');
    const infoFirmwareVersion = document.getElementById('info-firmware-version');
    const infoWifiStatus = document.getElementById('info-wifi-status');
    const infoIpAddress = document.getElementById('info-ip-address');
    const infoMqttStatus = document.getElementById('info-mqtt-status');
    const infoSystemTime = document.getElementById('info-system-time');
    const infoFreeHeap = document.getElementById('info-free-heap');
    // Add more system info element IDs here if you added them to HTML and ESP32 response
    // e.g. const infoMaxAllocHeap = document.getElementById('info-max-alloc-heap');

    // Hardcoded admin password (as per initial requirement)
    const ADMIN_PASSWORD = "admin123";
    let systemInfoIntervalId = null; // To store the interval ID for system info refresh
    const SESSION_LOGIN_KEY = 'esp32AdminLoggedIn';
    const SESSION_LOGIN_TIMESTAMP_KEY = 'esp32LoginTimestamp';
    const ONE_DAY_MS = 24 * 60 * 60 * 1000;

    function showLoginScreen() {
        loginSection.style.display = 'block';
        configSection.style.display = 'none';
        if (systemInfoSection) systemInfoSection.style.display = 'none';
        if (logoutButton) logoutButton.style.display = 'none'; // Hide logout button
        if (systemInfoIntervalId) clearInterval(systemInfoIntervalId);
        adminPasswordInput.value = ''; // Clear password field
    }

    function showMainContent() {
        loginSection.style.display = 'none';
        configSection.style.display = 'block';
        if (systemInfoSection) systemInfoSection.style.display = 'block';
        if (logoutButton) logoutButton.style.display = 'block'; // Show logout button
        loginStatus.textContent = '';
        loadCurrentConfig();
        loadSystemInfo();
        if (systemInfoIntervalId) clearInterval(systemInfoIntervalId);
        systemInfoIntervalId = setInterval(loadSystemInfo, 5000);
    }

    function loadCurrentConfig() {
        fetch("/getconfig")
            .then(response => {
                if (!response.ok) {
                    throw new Error("Failed to fetch config, status: " + response.status);
                }
                return response.json();
            })
            .then(config => {
                if (config.ssid) ssidInput.value = config.ssid;
                // Password is not sent from /getconfig for security
                if (config.mqtt_server) mqttServerInput.value = config.mqtt_server;
                if (config.mqtt_port) mqttPortInput.value = config.mqtt_port;
                if (config.api_key) apiKeyInput.value = config.api_key;
                console.log("Current config loaded into form.");
            })
            .catch(error => {
                console.error("Error fetching current config:", error);
                statusDisplay.textContent = 'Could not load current settings.';
                statusDisplay.style.color = 'orange';
            });
    }

    function loadSystemInfo() {
        fetch("/getsysteminfo")
            .then(response => {
                if (!response.ok) {
                    if (infoDeviceId) infoDeviceId.textContent = "Error"; // Indicate error
                    throw new Error("Failed to fetch system info, status: " + response.status);
                }
                return response.json();
            })
            .then(info => {
                if (infoDeviceId && info.deviceId) infoDeviceId.textContent = info.deviceId;
                if (infoFirmwareVersion && info.firmwareVersion) infoFirmwareVersion.textContent = info.firmwareVersion;
                if (infoWifiStatus && info.wifiStatus) infoWifiStatus.textContent = info.wifiStatus;
                if (infoIpAddress && info.ipAddress) infoIpAddress.textContent = info.ipAddress;
                if (infoMqttStatus && info.mqttStatus) infoMqttStatus.textContent = info.mqttStatus;
                if (infoSystemTime && info.systemTime) infoSystemTime.textContent = info.systemTime;
                if (infoFreeHeap && info.freeHeap) infoFreeHeap.textContent = info.freeHeap + " bytes";
                // Update more fields here if added
                // e.g. if (infoMaxAllocHeap && info.maxAllocHeap) infoMaxAllocHeap.textContent = info.maxAllocHeap + " bytes";
                console.log("System info updated.");
            })
            .catch(error => {
                console.error("Error fetching system info:", error);
                if (infoDeviceId) infoDeviceId.textContent = "Error loading";
            });
    }

    function handleLogout() {
        sessionStorage.removeItem(SESSION_LOGIN_KEY);
        sessionStorage.removeItem(SESSION_LOGIN_TIMESTAMP_KEY);
        showLoginScreen();
        loginStatus.textContent = 'Logged out successfully.';
        loginStatus.style.color = 'green';
    }

    function checkLoginSession() {
        const isLoggedIn = sessionStorage.getItem(SESSION_LOGIN_KEY);
        const loginTimestamp = sessionStorage.getItem(SESSION_LOGIN_TIMESTAMP_KEY);

        if (isLoggedIn === 'true' && loginTimestamp) {
            const now = new Date().getTime();
            if (now - parseInt(loginTimestamp) < ONE_DAY_MS) {
                showMainContent();
                return true;
            } else {
                sessionStorage.removeItem(SESSION_LOGIN_KEY);
                sessionStorage.removeItem(SESSION_LOGIN_TIMESTAMP_KEY);
                loginStatus.textContent = 'Session expired. Please login again.';
                loginStatus.style.color = 'orange';
            }
        }
        showLoginScreen();
        return false;
    }

    // Attach Event Listeners
    if (loginButton) {
        loginButton.addEventListener('click', function() {
            if (adminPasswordInput.value === ADMIN_PASSWORD) {
                sessionStorage.setItem(SESSION_LOGIN_KEY, 'true');
                sessionStorage.setItem(SESSION_LOGIN_TIMESTAMP_KEY, new Date().getTime().toString());
                showMainContent();
            } else {
                loginStatus.textContent = 'Incorrect password.';
                loginStatus.style.color = 'red';
            }
        });
    }

    if (logoutButton) {
        logoutButton.addEventListener('click', handleLogout);
    }

    if (saveButton) {
        saveButton.addEventListener('click', function() {
            const ssid = ssidInput.value;
            const pass = passInput.value;
            const mqtt_server = mqttServerInput.value;
            const mqtt_port = mqttPortInput.value;
            const api_key = apiKeyInput.value;

            if (!ssid) {
                statusDisplay.textContent = 'SSID cannot be empty.';
                statusDisplay.style.color = 'red';
                return;
            }

            statusDisplay.textContent = 'Saving and attempting to connect...';
            statusDisplay.style.color = 'blue';

            const formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('pass', pass);
            formData.append('mqtt_server', mqtt_server);
            formData.append('mqtt_port', mqtt_port);
            formData.append('api_key', api_key);

            fetch('/save', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                statusDisplay.textContent = data;
                if (data.includes("Đã lưu") || data.includes("Saved")) { // Check for success message
                    statusDisplay.style.color = 'green';
                    // Optionally, you could try to ping the device's new IP after a delay
                    // or provide instructions to the user on how to find the device.
                } else {
                    statusDisplay.style.color = 'red';
                }
            })
            .catch(error => {
                console.error('Error saving config:', error);
                statusDisplay.textContent = 'Error saving configuration. Check console.';
                statusDisplay.style.color = 'red';
            });
        });
    }

    // Initial check
    checkLoginSession();

    // All functions and event listeners related to WiFi scanning have been removed.
    // - scanWifiButton event listener
    // - fetch('/scanwifi') call
    // - Logic to populate a WiFi list
}); 