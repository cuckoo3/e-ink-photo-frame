const express = require('express');
const path = require('path');
const fs = require('fs');
const internalOnly = require('../utils/internalOnly');
const { getRegistry, formatMac, ensureDevice, getDeviceImages } = require('../utils/deviceRegistry');

const router = express.Router();

// Apply internal-only restriction to all ESP32 endpoints
router.use(internalOnly);

module.exports = function(config, uploadsBaseDir) {
    // POST /api/sync
/*
Client send following JSON:

{
  "mac": "24:DC:C3:A1:B2:C3",
  "images": [
    { "name": "photo01.bin" },
    { "name": "photo02.bin" }
  ]
}

Server reply following JSON:

{
	"sleepDurationMin": 240,
  "new": [
    { "name": "photo03.bin", "url": "/api/image/A1B2C3D4E5/photo03.bin" }
  ],
  "delete": [
    "photo01.bin"
  ]
}
*/
    router.post('/sync', (req, res) => {
        const { mac, images } = req.body;
		
		console.log('[Sync Request Received]:', JSON.stringify(req.body, null, 2));

        if (!mac || !Array.isArray(images)) {
            console.warn('[Sync] Invalid sync request payload from client');
            return res.status(400).json({ error: 'Missing MAC address or images array' });
        }

		const registry = getRegistry();
        const formattedMac = formatMac(mac);
		
        // 1. Auto-register new device into devices.json if missing without creating folders
        const { isNew } = ensureDevice(formattedMac);
        if (isNew) {
            console.log(`[Sync] Registered new device MAC in registry: ${formattedMac}`);
        }
		
		// Fetch device entry and configured sleep duration in minutes (defaults to 240 min)
        const deviceRecord = registry.devices[formattedMac] || {};
        const sleepDurationMin = deviceRecord.sleepDurationMin !== undefined ? deviceRecord.sleepDurationMin : 240;
		
        // 2. Fetch server images directly from registry memory (devices.json)
        const serverRegistryImages = getDeviceImages(formattedMac);

		// Map ESP payload by image name
		const espFilesMap = new Map();
		images.forEach(file => {
		    // Ensure entry has .bin extension for accurate lookup
		    const fileName = file.name.endsWith('.bin') ? file.name : `${file.name}.bin`;
		    espFilesMap.set(fileName, true);
		});

		const newDownloads = [];
		const registryFileNames = new Set();

		// 1. Process server images (Standardize everything to .bin)
		serverRegistryImages.forEach(img => {
		    const fileName = img.name.endsWith('.bin') ? img.name : `${img.name}.bin`;
		    registryFileNames.add(fileName); // Added only ONCE as a .bin file

		    // Check if the ESP32 already has this exact .bin file
		    if (!espFilesMap.has(fileName)) {
		        newDownloads.push({
		            name: fileName,
		            url: `/api/image/${formattedMac}/${fileName}`
		        });
		    }
		});

		// 2. Determine images on ESP32 that no longer exist in server registry
		const filesToDelete = [];
		images.forEach(file => {
		    const fileName = file.name.endsWith('.bin') ? file.name : `${file.name}.bin`;
		    if (!registryFileNames.has(fileName)) {
		        filesToDelete.push(fileName);
		    }
		});

		const responsePayload = {
            sleepDurationMin: sleepDurationMin,
            new: newDownloads,
            delete: filesToDelete,
			playlist: Array.from(registryFileNames)		// Send full, ordered list of binary filenames expected on the ESP32
        };

        console.log(`[Sync Response - ${formattedMac}]:`, JSON.stringify(responsePayload, null, 2));

        return res.json(responsePayload);
    });

    // GET /api/image/:macFolder/:fileName
    router.get('/image/:macFolder/:fileName', (req, res) => {
        const { macFolder, fileName } = req.params;
        const filePath = path.join(uploadsBaseDir, macFolder, config.binFolder, fileName);
		
		console.log(`[Image Request Received - ${macFolder}/${fileName}`);

        if (!filePath.startsWith(uploadsBaseDir)) {
            return res.status(403).send('Forbidden');
        }

        if (!fs.existsSync(filePath)) {
            return res.status(404).send('File not found');
        }

        res.setHeader('Content-Type', 'application/octet-stream');
        res.sendFile(filePath, (err) => {
            if (err && !res.headersSent) {
                res.status(500).send('Error streaming file');
            }
        });
    });

    return router;
};