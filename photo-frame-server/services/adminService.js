const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const sharp = require('sharp');

const router = express.Router();
const { upsertDeviceImage } = require('../utils/deviceRegistry');

function calculateMD5(filePath) {
    try {
        const fileBuffer = fs.readFileSync(filePath);
        const hashSum = crypto.createHash('md5');
        hashSum.update(fileBuffer);
        return hashSum.digest('hex');
    } catch (err) {
        console.error(`[MD5 Error] Failed to compute hash for ${filePath}:`, err);
        return null;
    }
}

module.exports = function(config, uploadsBaseDir, registry, saveRegistryToDisk) {
	// Dynamic storage configuration using MAC directly
	const storage = multer.diskStorage({
        destination: (req, file, cb) => {
            const mac = req.body.mac;
            if (!mac) {
                return cb(new Error('Device MAC address is missing from request body'));
            }

            let subFolder = '';

            if (file.fieldname === 'original') subFolder = config.originalFolder;
            else if (file.fieldname === 'dithered') subFolder = config.ditheredFolder;
            else return cb(new Error('Invalid field name'));

            const targetDir = path.join(uploadsBaseDir, mac, subFolder);

            if (!fs.existsSync(targetDir)) {
                fs.mkdirSync(targetDir, { recursive: true });
            }

            cb(null, targetDir);
        },
        filename: (req, file, cb) => {
            cb(null, file.originalname);
        }
    });

	/**
     * GET /admin/devices
     * Returns an array of all registered device MAC addresses.
     */
    router.get('/devices', (req, res) => {
        const macs = Object.keys(registry.devices);
        return res.json({
            count: macs.length,
            macs: macs
        });
    });
	
	// Configure Multer to store uploaded files in RAM buffers
	const upload = multer({ storage: multer.memoryStorage() });
	
	const { convertPngToBin } = require('../utils/imageConverter');

	/**
	 * POST /admin/upload
	 * Receives 'mac', 'original', and 'dithered' form fields,
	 * processes binary/thumbnail formats, saves raw original/dithered PNG files to disk,
	 * and updates registry memory & devices.json.
	 */
	router.post('/upload', upload.fields([
	    { name: 'original', maxCount: 1 },
	    { name: 'dithered', maxCount: 1 }
	]), async (req, res) => {
	    try {
	        if (!req.files || !req.files.original || !req.files.dithered) {
	            return res.status(400).json({ error: 'Both original and dithered images are required.' });
	        }

	        const mac = req.body.mac;
	        if (!mac) {
	            return res.status(400).json({ error: 'Device MAC address is required.' });
	        }

	        const originalFile = req.files.original[0];
	        const ditheredFile = req.files.dithered[0];
	        const baseName = path.parse(originalFile.originalname).name;

	        // Target directory paths using direct req.body.mac
	        const originalDirPath = path.join(uploadsBaseDir, mac, config.originalFolder);
	        const ditheredDirPath = path.join(uploadsBaseDir, mac, config.ditheredFolder);
	        const binDirPath = path.join(uploadsBaseDir, mac, config.binFolder);
	        const thumbnailDirPath = path.join(uploadsBaseDir, mac, config.thumbnailFolder);

	        // Ensure all target directories exist
	        await Promise.all([
	            fs.promises.mkdir(originalDirPath, { recursive: true }),
	            fs.promises.mkdir(ditheredDirPath, { recursive: true }),
	            fs.promises.mkdir(binDirPath, { recursive: true }),
	            fs.promises.mkdir(thumbnailDirPath, { recursive: true })
	        ]);

	        // File destination paths
	        const originalFilePath = path.join(originalDirPath, `${baseName}.jpg`);
	        const ditheredFilePath = path.join(ditheredDirPath, `${baseName}.png`);
	        const binFilePath = path.join(binDirPath, `${baseName}.bin`);
	        const thumbnailFilePath = path.join(thumbnailDirPath, `${baseName}.jpg`);

	        // 1. Save original and dithered PNG buffers to disk
	        await Promise.all([
	            fs.promises.writeFile(originalFilePath, originalFile.buffer),
	            fs.promises.writeFile(ditheredFilePath, ditheredFile.buffer)
	        ]);
			
			// Log original file details upon successfully writing to disk
	        console.log(`[Upload] Saved Original File:`, {
	            mac: mac,
	            filename: originalFile.originalname,
	            baseName: baseName,
	            mimetype: originalFile.mimetype,
	            size: `${(originalFile.size / 1024).toFixed(2)} KB`,
	            destination: originalFilePath
	        });

	        // 2. Generate 320x240 JPEG thumbnail from original image buffer
	        await sharp(originalFile.buffer)
	            .resize(config.thumbnailWidth, config.thumbnailHeight, { fit: 'cover' })
	            .jpeg({ quality: 80 })
	            .toFile(thumbnailFilePath);

	        // 3. Convert dithered image buffer directly to binary format (.bin)
	        await convertPngToBin(ditheredFile.buffer, binFilePath, config);

	        // 4. Compute MD5 checksum of the newly generated .bin file
	        const binMd5 = calculateMD5(binFilePath);

	        // 5. Update device registry memory and persist to devices.json
	        const newRecord = {
	            name: baseName,
	            createdAt: new Date().toISOString(),
	            md5: binMd5
	        };

	        upsertDeviceImage(mac, newRecord, registry, saveRegistryToDisk);

	        return res.status(200).json({
	            message: 'Images uploaded, saved, and registered successfully.',
	            mac: mac,
	            file: `${baseName}.bin`,
	            md5: binMd5
	        });
	    } catch (error) {
	        console.error('Upload processing error:', error);
	        return res.status(500).json({ error: 'Failed to process and save uploaded files.' });
	    }
	});

	/**
	 * GET /admin/rescan
	 * Full rescan endpoint. Iterates over all device MAC folders in uploads/
	 * to rebuild and sync the entire registry object, sorted by birthtime (newest first).
	 */
	router.get('/rescan', (req, res) => {
	    if (!fs.existsSync(uploadsBaseDir)) {
	        return res.status(404).json({ error: 'Uploads directory does not exist.' });
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
	                    createdAt: stats.birthtime.toISOString(),
	                    md5: calculateMD5(filePath)
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
	    saveRegistryToDisk();

	    console.log(`[Full Rescan] Completed. Scanned ${Object.keys(newDevicesState).length} device(s) and sorted by newest first.`);

	    return res.json({
	        message: 'Full registry rescan completed successfully.',
	        scannedDevicesCount: Object.keys(newDevicesState).length,
	        devices: newDevicesState
	    });
	});

    return router;
};