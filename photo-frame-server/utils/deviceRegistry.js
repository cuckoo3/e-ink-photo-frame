const path = require('path');
const fs = require('fs');

const REGISTRY_PATH = path.join(__dirname, '../devices.json');

// Global in-memory registry object
let registry = { devices: {} };

/**
 * Initialize registry from devices.json on disk.
 * Creates an empty devices.json if one does not exist.
 * 
 * JSON to store the devices and images
{
    "devices":
    {
        "24DCC3A1B2C3":
        {
            "images":
            [
                {
                    "name": "family_photo_01",
                    "createdAt": "2026-09-17T14:30:00Z"
                }
            ]
        }
    }
}
 */
function initRegistry() {
    if (fs.existsSync(REGISTRY_PATH)) {
        try {
            const rawData = fs.readFileSync(REGISTRY_PATH, 'utf8');
            registry = JSON.parse(rawData);
            if (!registry.devices) {
                registry.devices = {};
            }
            console.log(`[Registry] Successfully loaded registry from ${REGISTRY_PATH}`);
        } catch (err) {
            console.error('[Registry Error] Failed to parse devices.json, initializing empty state:', err);
            registry = { devices: {} };
        }
    } else {
        console.log('[Registry] devices.json not found. Creating a new one...');
        saveRegistryToDisk();
    }
    return registry;
}

/**
 * Synchronizes current in-memory registry object to devices.json on disk.
 */
function saveRegistryToDisk() {
    try {
        fs.writeFileSync(REGISTRY_PATH, JSON.stringify(registry, null, 2), 'utf8');
        console.log('[Registry] devices.json updated successfully.');
    } catch (err) {
        console.error('[Registry Error] Failed to write devices.json:', err);
    }
}

/**
 * Helper to get current in-memory registry reference
 */
function getRegistry() {
    return registry;
}

/**
 * Normalizes MAC addresses to uppercase without colons (e.g., "24:DC:C3:A1:B2:C3" -> "24DCC3A1B2C3").
 */
function formatMac(mac) {
    if (!mac || typeof mac !== 'string') return '';
    return mac.replace(/:/g, '').toUpperCase();
}

/**
 * Ensures a device entry exists in registry memory and persists changes if newly registered.
 */
function ensureDevice(mac) {
    const formattedMac = formatMac(mac);
    if (!formattedMac) return null;

    let isNew = false;
    if (!registry.devices[formattedMac]) {
        registry.devices[formattedMac] = { images: [] };
        isNew = true;
        if (typeof saveRegistryToDisk === 'function') {
            saveRegistryToDisk();
        }
    }

    return { mac: formattedMac, isNew };
}

/**
 * Retrieves the image array for a specific device from registry memory.
 */
function getDeviceImages(mac) {
    const formattedMac = formatMac(mac);
    if (!formattedMac || !registry.devices[formattedMac]) {
        return [];
    }
    return registry.devices[formattedMac].images || [];
}

/**
 * Updates or adds an image record for a specific device in the registry memory and persists to disk.
 * Always places the new/updated image record at the beginning of the array (newest first).
 */
function upsertDeviceImage(mac, imageRecord) {
    const formattedMac = formatMac(mac);
    if (!formattedMac) return;

    // Ensure the device exists in the registry
    ensureDevice(formattedMac, registry, saveRegistryToDisk);

    const images = registry.devices[formattedMac].images;
    const existingIndex = images.findIndex(img => img.name === imageRecord.name);

    // If record already exists, remove it from its current position
    if (existingIndex >= 0) {
        images.splice(existingIndex, 1);
    }

    // Always insert the fresh image record at index 0 (top of the array)
    images.unshift(imageRecord);

    // Save changes to devices.json
    if (typeof saveRegistryToDisk === 'function') {
        saveRegistryToDisk();
    }
}

/**
 * Scans all device directories in the uploads folder,
 * updates the registry memory state sorted by birthtime (newest first), and persists to disk.
 */
function rescanDevices(uploadsBaseDir, config) {
    if (!fs.existsSync(uploadsBaseDir)) {
        return { success: false, error: 'Uploads directory does not exist.' };
    }

    // Retrieve all device directory entries inside the uploads base folder
    const entries = fs.readdirSync(uploadsBaseDir, { withFileTypes: true });
    const deviceDirectories = entries
        .filter(entry => entry.isDirectory())
        .map(entry => entry.name);

    const newDevicesState = {};

    deviceDirectories.forEach(mac => {
        const binFolder = path.join(uploadsBaseDir, mac, config.binFolder);

        if (fs.existsSync(binFolder)) {
            const binFiles = fs.readdirSync(binFolder).filter(file => file.endsWith('.bin'));

            const scannedImages = binFiles.map(fileName => {
                const baseName = path.parse(fileName).name;
                const filePath = path.join(binFolder, fileName);
                const stats = fs.statSync(filePath);

                return {
                    name: baseName,
                    createdAt: stats.birthtime.toISOString()
                };
            });

            // Sort images by creation timestamp in descending order (newest first)
            scannedImages.sort((a, b) => new Date(b.createdAt) - new Date(a.createdAt));

            newDevicesState[mac] = { images: scannedImages };
        } else {
            // Preserve empty image list structure if bin directory does not exist
            newDevicesState[mac] = { images: [] };
        }
    });

    // Overwrite in-memory registry state
    registry.devices = newDevicesState;

    // Persist updated registry state to devices.json
    if (typeof saveRegistryToDisk === 'function') {
        saveRegistryToDisk();
    }

    return {
        success: true,
        scannedDevicesCount: Object.keys(newDevicesState).length,
        devices: newDevicesState
    };
}

module.exports = {
	initRegistry,
    saveRegistryToDisk,
    getRegistry,
    formatMac,
    ensureDevice,
    getDeviceImages,
    upsertDeviceImage,
    rescanDevices
};