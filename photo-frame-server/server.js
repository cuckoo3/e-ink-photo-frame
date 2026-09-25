// Catch unhandled exceptions to prevent systemd suicide
process.on('uncaughtException', (err) => {
    console.error('[CRASH] Uncaught Exception:', err);
});

process.on('unhandledRejection', (reason, promise) => {
    console.error('[CRASH] Unhandled Rejection at:', promise, 'reason:', reason);
});

const express = require('express');
const path = require('path');
const os = require('os');
const fs = require('fs');

const config = require('./config.json');
const initUdpListener = require('./services/udpService');
const frameService = require('./services/frameService');
const adminService = require('./services/adminService');
const { initRegistry } = require('./utils/deviceRegistry');

const app = express();

app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use(express.static(path.join(__dirname, 'public')));



// Load registry into memory at server startup
const registry = initRegistry();

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
app.use('/api', frameService(config, STORAGE_DIR));

// 3. Register Web Admin Routes
app.use('/admin', adminService(config, STORAGE_DIR));

app.listen(config.httpPort, () => {
    console.log(`[HTTP] Web Admin interface running at http://localhost:${config.httpPort}`);
});