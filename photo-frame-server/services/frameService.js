const express = require('express');
const path = require('path');
const fs = require('fs');
const internalOnly = require('../utils/internalOnly');
const { formatMac, ensureDevice, getDeviceImages } = require('../utils/deviceRegistry');

const router = express.Router();

// Apply internal-only restriction to all ESP32 endpoints
router.use(internalOnly);

module.exports = function(config, uploadsBaseDir, registry, saveRegistryToDisk) {
    // POST /api/sync
/*
Client send following JSON:

{
  "mac": "24:DC:C3:A1:B2:C3",
  "images": [
    { "name": "photo01.bin", md5": xxxxxx },
    { "name": "photo02.bin", "md5": xxxxxx }
  ]
}

Server reply following JSON:

{
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

        if (!mac || !Array.isArray(images)) {
            console.warn('[Sync] Invalid sync request payload from client');
            return res.status(400).json({ error: 'Missing MAC address or images array' });
        }

        const formattedMac = formatMac(mac);

        // 1. Auto-register new device into devices.json if missing without creating folders
        const { isNew } = ensureDevice(formattedMac, registry, saveRegistryToDisk);
        if (isNew) {
            console.log(`[Sync] Registered new device MAC in registry: ${formattedMac}`);
        }

        // 2. Fetch server images directly from registry memory (devices.json)
        const serverRegistryImages = getDeviceImages(formattedMac, registry);

        // Map ESP payload by image name
        const espFilesMap = new Map();
        images.forEach(file => {
            espFilesMap.set(file.name, { size: file.size, md5: file.md5 });
        });

        const newDownloads = [];
        const registryFileNames = new Set();

        // 3. Determine images that ESP needs to download
        serverRegistryImages.forEach(img => {
            const fileName = img.name.endsWith('.bin') ? img.name : `${img.name}.bin`;
            registryFileNames.add(img.name);
            registryFileNames.add(fileName);

            const filePath = path.join(uploadsBaseDir, formattedMac, config.binFolder, fileName);
            let serverSize = 0;

            if (fs.existsSync(filePath)) {
                serverSize = fs.statSync(filePath).size;
            }

            const espFile = espFilesMap.get(img.name) || espFilesMap.get(fileName);

            let needsDownload = false;
            if (!espFile || (espFile.md5 && img.md5 && espFile.md5 !== img.md5)) {
                needsDownload = true;
            }

            if (needsDownload) {
                newDownloads.push({
                    name: fileName,
                    url: `/api/image/${formattedMac}/${fileName}`,
                    size: serverSize,
                    md5: img.md5
                });
            }
        });

        // 4. Determine images present on ESP that no longer exist in registry
        const filesToDelete = [];
        images.forEach(file => {
            const baseName = path.parse(file.name).name;
            if (!registryFileNames.has(file.name) && !registryFileNames.has(baseName)) {
                filesToDelete.push(file.name);
            }
        });

        return res.json({
            new: newDownloads,
            delete: filesToDelete
        });
    });

    // GET /api/image/:macFolder/:fileName
    router.get('/image/:macFolder/:fileName', (req, res) => {
        const { macFolder, fileName } = req.params;
        const filePath = path.join(uploadsBaseDir, macFolder, config.binFolder, fileName);

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