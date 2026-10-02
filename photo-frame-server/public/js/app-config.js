// app-config.js - Device and Configuration API Management
const deviceSelect = document.getElementById('deviceSelect');
const configStatus = document.getElementById('configStatus');
const sleepDurationInput = document.getElementById('sleepDurationInput');

async function fetchDevices() {
    try {
        const response = await fetch('/admin/devices');
        if (!response.ok) throw new Error('Failed to fetch devices');

        const data = await response.json();
        deviceSelect.innerHTML = '';

        if (data.macs && data.macs.length > 0) {
            data.macs.forEach(mac => {
                const option = document.createElement('option');
                option.value = mac;
                option.textContent = mac;
                deviceSelect.appendChild(option);
            });
            onDeviceChange();
        } else {
            deviceSelect.innerHTML = '<option value="">No devices found</option>';
        }
    } catch (error) {
        console.error('Error fetching device list:', error);
        deviceSelect.innerHTML = '<option value="">Error loading devices</option>';
    }
}

async function onDeviceChange() {
    const selectedMac = deviceSelect.value;
    clearConfigStatus();
    if (!selectedMac) return;

    try {
        const res = await fetch(`/admin/${selectedMac}/config`);
        const data = await res.json();
        if (data.success) {
            if (data.sleepDurationMin) {
                sleepDurationInput.value = data.sleepDurationMin;
            }
            if (data.orientation) {
                const radio = document.querySelector(`input[name="orientation"][value="${data.orientation}"]`);
                if (radio) {
                    radio.checked = true;
                    if (typeof currentLoadedImage !== 'undefined' && currentLoadedImage) {
                        startProcessing();
                    }
                }
            }
        }
    } catch (err) {
        console.error('Error fetching config:', err);
    }

    loadDeviceImages(selectedMac);
}

function clearConfigStatus() {
    configStatus.textContent = '';
    configStatus.className = 'font-semibold text-sm';
}

function onOrientationChange() {
    if (typeof currentLoadedImage !== 'undefined' && currentLoadedImage) {
        startProcessing();
    }
}

async function saveConfig() {
    const selectedMac = deviceSelect.value;
    const duration = sleepDurationInput.value;
    const selectedOrientation = document.querySelector('input[name="orientation"]:checked').value;

    if (!selectedMac || isNaN(duration) || duration < 1 || duration > 1440) {
        configStatus.textContent = 'Error: Enter minutes between 1 and 1440';
        configStatus.className = 'font-semibold text-sm text-red-600';
        return;
    }

    try {
        const res = await fetch(`/admin/${selectedMac}/config`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                sleepDurationMin: parseInt(duration, 10),
                orientation: selectedOrientation
            })
        });
        const data = await res.json();
        if (data.success) {
            configStatus.textContent = 'Saved successfully!';
            configStatus.className = 'font-semibold text-sm text-emerald-600';
            loadDeviceImages(selectedMac);
        } else {
            configStatus.textContent = `Error: ${data.error || 'Failed'}`;
            configStatus.className = 'font-semibold text-sm text-red-600';
        }
    } catch (err) {
        configStatus.textContent = 'Error: Request failed';
        configStatus.className = 'font-semibold text-sm text-red-600';
    }
}