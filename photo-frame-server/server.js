const express = require('express');
const path = require('path');
const os = require('os');
const fs = require('fs');

const config = require('./config.json');
const initUdpListener = require('./services/udpService');
const frameService = require('./services/frameService');
const adminService = require('./services/adminService');

const app = express();

app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use(express.static(path.join(__dirname, 'public')));

// ---------------------------------------------------------------
// Registry State Initialization & Persistence
// ---------------------------------------------------------------
const REGISTRY_PATH = path.join(__dirname, 'devices.json');

// Memory state container for device records
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
                    "createdAt": "2026-09-17T14:30:00Z",
                    "md5": "e99a18c428cb38d5f260853678922e03"
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
            // Ensure devices map exists if file was blank
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
}

/**
 * Synchronize current in-memory registry object to devices.json on disk.
 */
function saveRegistryToDisk() {
    try {
        fs.writeFileSync(REGISTRY_PATH, JSON.stringify(registry, null, 2), 'utf8');
        console.log('[Registry] devices.json updated successfully.');
    } catch (err) {
        console.error('[Registry Error] Failed to write devices.json:', err);
    }
}

// Load registry into memory at server startup
initRegistry();

function getPhysicalLocalIP() {
    const interfaces = os.networkInterfaces();
    for (const name of Object.keys(interfaces)) {
        const lowerName = name.toLowerCase();
        if (
            lowerName.includes('vethernet') ||
            lowerName.includes('wsl') ||
            lowerName.includes('vbox') ||
            lowerName.includes('vmware') ||
            lowerName.includes('virtual')
        ) {
            continue;
        }

        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return '0.0.0.0';
}

const physicalIP = getPhysicalLocalIP();
const STORAGE_DIR = path.join(__dirname, config.storageFolder);

// 1. Initialize UDP Multicast Listener
initUdpListener(config, physicalIP);

// 2. Register ESP32 REST API (Internal Subnet Protected)
app.use('/api', frameService(config, STORAGE_DIR, registry, saveRegistryToDisk));

// 3. Register Web Admin Routes
app.use('/admin', adminService(config, STORAGE_DIR, registry, saveRegistryToDisk));

app.listen(config.httpPort, () => {
    console.log(`[HTTP] Web Admin interface running at http://localhost:${config.httpPort}`);
});