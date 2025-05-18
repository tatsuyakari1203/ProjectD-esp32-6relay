document.addEventListener('DOMContentLoaded', function() {
    const loginSection = document.getElementById('login-section');
    const configSection = document.getElementById('config-section');
    const adminPasswordInput = document.getElementById('adminPassword');
    const loginButton = document.getElementById('loginButton');
    const loginStatus = document.getElementById('login-status');

    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('pass');
    const saveButton = document.getElementById('saveButton');
    const statusDisplay = document.getElementById('status');

    // Hardcoded admin password (as per initial requirement)
    const ADMIN_PASSWORD = "admin123";

    if (loginButton) {
        loginButton.addEventListener('click', function() {
            if (adminPasswordInput.value === ADMIN_PASSWORD) {
                loginSection.style.display = 'none';
                configSection.style.display = 'block';
                loginStatus.textContent = '';
            } else {
                loginStatus.textContent = 'Incorrect password.';
                loginStatus.style.color = 'red';
            }
        });
    }

    if (saveButton) {
        saveButton.addEventListener('click', function() {
            const ssid = ssidInput.value;
            const pass = passInput.value;

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

    // All functions and event listeners related to WiFi scanning have been removed.
    // - scanWifiButton event listener
    // - fetch('/scanwifi') call
    // - Logic to populate a WiFi list
}); 