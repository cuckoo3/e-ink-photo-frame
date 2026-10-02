const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const sharp = require('sharp');

const router = express.Router();
const { getRegistry, upsertDeviceImage, getDeviceImages, saveRegistryToDisk, rescanDevices } = require('../utils/deviceRegistry');

function calculateMD5(filePath) {
    try {
        const fileBuffer = fs.readFileSync(filePath);
        const hashSum = crypto.createHash('md5');
        hashSum.update(fileBuffer);
        return hashSum.digest('hex');
    } catch (err) {
        console.error(`[${new Date().toLocaleString()}] [MD5 Error] Failed to compute hash for ${filePath}:`, err);
        return null;
    }
}

module.exports = function(config, uploadsBaseDir) {
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
		const registry = getRegistry();
		
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
			
			// --- 1. Sanitize & Truncate BaseName ---
	        let rawBaseName = path.parse(originalFile.originalname).name.replace(/[^a-zA-Z0-9._-]/g, '_');
	        
	        // Reserve characters for the longest extension (e.g., ".bin" is 4 chars)
	        const maxExtLen = 4;
	        const maxBaseNameLen = (config.maxFilenameLen || 32) - maxExtLen;

	        let baseName = rawBaseName;
	        if (baseName.length > maxBaseNameLen) {
	            baseName = baseName.substring(0, maxBaseNameLen);
	            console.warn(`[Upload] BaseName truncated from '${rawBaseName}' to '${baseName}' for MAC: ${mac}`);
	        }

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

			// --- 2. Check File Count Limit via Registry (No FIFO: Reject upload if limit reached) ---
	        const currentImages = getDeviceImages(mac); //
	        const maxCount = config.maxFileCount || 10;
	        const isOverwritingExisting = currentImages.some(img => img.name === baseName);

	        // Reject upload if capacity is reached and this is not overwriting an existing file
	        if (currentImages.length >= maxCount && !isOverwritingExisting) {
				console.error(`[${new Date().toLocaleString()}] Fail to upload. Max file limit reached (${maxCount}).`);
	            return res.status(400).json({ 
	                error: `Max file limit reached (${maxCount}).` 
	            });
	        }

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
			console.log(`[${new Date().toLocaleString()}] [Upload] Saved Original File:`, JSON.stringify({
			    mac: mac,
			    filename: originalFile.originalname,
			    baseName: baseName,
			    mimetype: originalFile.mimetype,
			    size: `${(originalFile.size / 1024).toFixed(2)} KB`,
			    destination: originalFilePath
			}));

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

	        upsertDeviceImage(mac, newRecord);

	        return res.status(200).json({
	            message: 'Images uploaded, saved, and registered successfully.',
	            mac: mac,
	            file: `${baseName}.bin`,
	            md5: binMd5
	        });
	    } catch (error) {
	        console.error(`[${new Date().toLocaleString()}] [Upload Processing Error]:`, error);
	        return res.status(500).json({ error: 'Failed to process and save uploaded files.' });
	    }
	});
	
	/**
	 * GET /admin/:deviceId/config
	 * Retrieves configuration (sleep duration in minutes & orientation) for a given device
	 */
     router.get('/:deviceId/config', (req, res) => {
         const { deviceId } = req.params;
 		const registry = getRegistry();
 		
         const device = registry.devices[deviceId] || {};
         const sleepDurationMin = device.sleepDurationMin !== undefined ? device.sleepDurationMin : 240;
         const orientation = device.orientation || 'landscape';

         return res.json({
             success: true,
             deviceId,
             sleepDurationMin,
             orientation
         });
     });
	
	/**
     * POST /admin/:deviceId/config
     * Updates configuration (sleep duration in minutes & orientation) and persists to devices.json
     */

	router.post('/:deviceId/config', (req, res) => {
        const { deviceId } = req.params;
        const { sleepDurationMin, orientation } = req.body;

        if (sleepDurationMin === undefined || isNaN(sleepDurationMin) || sleepDurationMin < 1) {
            return res.status(400).json({ success: false, error: 'Invalid sleep duration value in minutes.' });
        }

        if (orientation && !['landscape', 'portrait'].includes(orientation)) {
            return res.status(400).json({ success: false, error: 'Invalid orientation value. Must be "landscape" or "portrait".' });
        }

		const registry = getRegistry();
        if (!registry.devices[deviceId]) {
            registry.devices[deviceId] = { images: [] };
        }

        registry.devices[deviceId].sleepDurationMin = parseInt(sleepDurationMin, 10);
        if (orientation) {
            registry.devices[deviceId].orientation = orientation;
        }

        saveRegistryToDisk();

        console.log(`[${new Date().toLocaleString()}] [Config Update] Device ${deviceId} sleepDurationMin set to ${sleepDurationMin} min, orientation set to ${registry.devices[deviceId].orientation}`);

        return res.json({
            success: true,
            deviceId,
            sleepDurationMin: registry.devices[deviceId].sleepDurationMin,
            orientation: registry.devices[deviceId].orientation
        });
    });
	
	/**
	 * GET /admin/images/:mac
	 * Get list of photos 
	 * return JSON
	{
	  "images": [
	    {
	      "name": "20260921_photo01",
	      "thumbUrl": "/admin/photo/24DCC3A1B2C3/20260921_photo01/thumb",
	      "ditheredUrl": "/admin/photo/24DCC3A1B2C3/20260921_photo01/dithered"
	    }
	  ]
	}
	
	 */
	router.get('/images/:mac', (req, res) => {
	    const { mac } = req.params;

		// Fetch device files and strip .bin extension for clean display
	    const images = getDeviceImages(mac).map(img => {
	        return {
	            name: img.name,
	            thumbUrl: `/admin/image/${mac}/${img.name}/thumb`,
	            ditheredUrl: `/admin/image/${mac}/${img.name}/dithered`
	        };
	    });

	    res.json({
	        images
	    });
	});

	/**
	 * GET /admin/image/:mac/:id/:type
	 * Securely streams image asset (thumb -> JPG, dithered -> PNG)
	 */
	router.get('/image/:mac/:name/:type', (req, res) => {
	    const { mac, name, type } = req.params;

	    // Validate type parameter
	    if (type !== 'thumb' && type !== 'dithered') {
	        return res.status(400).send('Invalid asset type. Expected "thumb" or "dithered"');
	    }

	    const isThumb = type === 'thumb';
	    const folder = isThumb ? config.thumbnailFolder : config.ditheredFolder;
	    const ext = isThumb ? 'jpg' : 'png';
	    const contentType = `image/${ext}`;

	    const filePath = path.join(uploadsBaseDir, mac, folder, `${name}.${ext}`);

	    // Prevent directory traversal attacks
	    if (!filePath.startsWith(uploadsBaseDir)) {
	        return res.status(403).send('Forbidden');
	    }

	    if (!fs.existsSync(filePath)) {
	        return res.status(404).send(`${isThumb ? 'Thumbnail' : 'Dithered image'} not found`);
	    }

	    res.setHeader('Content-Type', contentType);
	    res.sendFile(filePath, (err) => {
	        if (err && !res.headersSent) {
	            res.status(500).send(`Error streaming ${type} asset`);
	        }
	    });
	});
	
	/**
	 * DELETE /admin/image/:mac/:name
	 * Deletes an image asset (original, thumbnail JPG, dithered PNG, and BIN)
	 * and removes its entry from the registry.
	 */
	router.delete('/image/:mac/:name', (req, res) => {
	    const { mac, name } = req.params; // 1. Corrected destructuring

	    try {
	        // 2. Locate file paths
	        const originalPath = path.join(uploadsBaseDir, mac, config.originalFolder, `${name}.jpg`);
	        const thumbPath = path.join(uploadsBaseDir, mac, config.thumbnailFolder, `${name}.jpg`);
	        const ditheredPngPath = path.join(uploadsBaseDir, mac, config.ditheredFolder, `${name}.png`);
	        const binPath = path.join(uploadsBaseDir, mac, config.binFolder, `${name}.bin`);

	        // Directory traversal protection
	        if (!thumbPath.startsWith(uploadsBaseDir)) {
	            return res.status(403).json({ success: false, error: 'Forbidden path traversal' });
	        }

	        // 3. Remove physical files from storage if they exist
	        if (fs.existsSync(originalPath)) fs.unlinkSync(originalPath);
	        if (fs.existsSync(thumbPath)) fs.unlinkSync(thumbPath);
	        if (fs.existsSync(ditheredPngPath)) fs.unlinkSync(ditheredPngPath);
	        if (fs.existsSync(binPath)) fs.unlinkSync(binPath);

	        // 4. Access registry safely using registry.devices[mac]
	        const registry = getRegistry();
	        
	        if (registry && registry.devices && registry.devices[mac] && Array.isArray(registry.devices[mac].images)) {
	            // Filter using the correct variable 'name'
	            registry.devices[mac].images = registry.devices[mac].images.filter(img => img.name !== name);
	            saveRegistryToDisk();
	        }

	        return res.json({
	            success: true,
	            message: `Image ${name} successfully deleted from device ${mac}`
	        });
	    } catch (err) {
	        console.error(`[${new Date().toLocaleString()}] Failed to delete image ${name} for MAC ${mac}:`, err);
	        return res.status(500).json({
	            success: false,
	            error: 'Server error occurred while deleting image'
	        });
	    }
	});
	
	/**
	 * GET /admin/rescan
	 * Full rescan endpoint. Iterates over all device MAC folders in uploads/
	 * to rebuild and sync the entire registry object, sorted by birthtime (newest first).
	 */
	router.get('/rescan', (req, res) => {
	    const result = rescanDevices(uploadsBaseDir, config);

	    if (!result.success) {
	        return res.status(404).json({ error: result.error });
	    }

	    console.log(`[${new Date().toLocaleString()}] [Full Rescan] Completed. Scanned ${result.scannedDevicesCount} device(s) and sorted by newest first.`);

	    return res.json({
	        message: 'Full registry rescan completed successfully.',
	        scannedDevicesCount: result.scannedDevicesCount,
	        devices: result.devices
	    });
	});

    return router;
};