const express = require('express');
const dgram = require('dgram');
const multer = require('multer');
const path = require('path');
const os = require('os');
const fs = require('fs');
const crypto = require('crypto');

// Load configuration parameters from config.json
const config = require('./config.json');

const app = express();
// Parse JSON bodies (asynchronous API payloads)
app.use(express.json());

// Parse URL-encoded bodies (standard HTML form posts)
app.use(express.urlencoded({ extended: true }));

// Serve static frontend files (index.html, js, css)
app.use(express.static(path.join(__dirname, 'public')));

// Helper function to detect local physical network interface IP
function getPhysicalLocalIP() {
    const interfaces = os.networkInterfaces();
    for (const name of Object.keys(interfaces)) {
        // Skip Hyper-V virtual switches, WSL, VirtualBox, and VMware interfaces
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
            // Select non-internal IPv4 address
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return '0.0.0.0'; // Fallback to all interfaces if no physical match found
}

const physicalIP = getPhysicalLocalIP();

// 2. In-memory state tracking
let systemStatus = {
    lastEspIp: 'Not Connected',
    lastSeen: 'None'
};

// =========================================================================
// 3. Prepare for file upload
// =========================================================================

// Resolve target directories: uploads/original and uploads/simulate
const originalDir = path.join(__dirname, 'uploads/original');
const simulateDir = path.join(__dirname, 'uploads/simulate');

// Ensure upload directories exist
[originalDir, simulateDir].forEach(dir => {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
});

// Configure Multer storage to route files into their respective folders
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        if (file.fieldname === 'original') {
            cb(null, originalDir);
        } else if (file.fieldname === 'dithered') {
            cb(null, simulateDir);
        } else {
            cb(new Error('Invalid field name'), null);
        }
    },
    filename: (req, file, cb) => {
        // Save using the original filename sent from the client
        cb(null, file.originalname);
    }
});

const upload = multer({ storage });


// =========================================================================
// 3. UDP Multicast Listener Setup
// =========================================================================
const udpServer = dgram.createSocket({ type: 'udp4', reuseAddr: true });

udpServer.on('error', (err) => {
    console.error(`[UDP Error] Server error:\n${err.stack}`);
    udpServer.close();
});

udpServer.on('listening', () => {
    try {
        // Explicitly set outgoing multicast interface and join membership on physical IP
        if (physicalIP !== '0.0.0.0') {
            udpServer.setMulticastInterface(physicalIP);
            udpServer.addMembership(config.multicastAddress, physicalIP);
            console.log(`[UDP] Bound explicitly to physical interface: ${physicalIP}`);
        } else {
            udpServer.addMembership(config.multicastAddress, '0.0.0.0');
            console.warn(`[UDP Warning] No physical interface detected. Listening on 0.0.0.0`);
        }

        const address = udpServer.address();
        console.log(`[UDP] Successfully joined multicast group ${config.multicastAddress}:${address.port}`);
    } catch (err) {
        console.error(`[UDP Error] Failed to join multicast group: ${err.message}`);
    }
});

udpServer.on('message', (msg, rinfo) => {
    const messageStr = msg.toString().trim();
    
    // Validate request string against configuration
    if (messageStr === config.discoveryRequestMsg) {
        systemStatus.lastEspIp = rinfo.address;
        systemStatus.lastSeen = new Date().toLocaleString();
        
        console.log(`[UDP] Discovery request received from ESP32 (${rinfo.address}:${rinfo.port})`);
        
        // Dynamically build ACK payload using httpPort from config
        const ackPayload = `${config.discoveryAckResponse}:${config.httpPort}`;
        const ackBuffer = Buffer.from(ackPayload);
        
        udpServer.send(ackBuffer, 0, ackBuffer.length, rinfo.port, rinfo.address, (err) => {
            if (err) {
                console.error(`[UDP] Failed to send ACK:`, err);
            } else {
                console.log(`[UDP] Successfully sent ${ackPayload} to ${rinfo.address}:${rinfo.port}`);
            }
        });
    }
});

// Bind UDP Socket using configured port
udpServer.bind(config.udpPort, '0.0.0.0');

// =========================================================================
// 4. REST API Endpoints
// =========================================================================

// Setup upload directory path
const STORAGE_DIR = path.join(__dirname, 'uploads');

app.get('/api/status', (req, res) => {
    res.json(systemStatus);
});


// Helper function to compute MD5 hash of a local file on the server
function getFileMD5(filePath) {
    try {
        const fileBuffer = fs.readFileSync(filePath);
        const hashSum = crypto.createHash('md5');
        hashSum.update(fileBuffer);
		md5 = hashSum.digest('hex');
        return md5;
    } catch (err) {
		console.error(err);
        return null;
    }
}

// Helper function to format MAC address into a clean folder name (e.g. "24DCC3A1B2C3")
function formatMacFolderName(mac) {
    if (!mac || typeof mac !== 'string') {
        console.warn('formatMacFolderName received invalid MAC:', mac);
        return '';
    }
    return mac.replace(/:/g, '').toUpperCase();
}

/**
 * 1. POST /api/sync
 * 
 * 
 * {
	  "mac": "24:DC:C3:A1:B2:C3",
	  "local_files": [
	    { "name": "photo01.bin", "size": 96000, md5": xxxxxx },
	    { "name": "photo02.bin", "size": 96000, "md5": xxxxxx }
	  ]
	}
 * Receives ESP32 local files list and computes the diff payload.
 */
app.post('/api/sync', (req, res) => {
    const { mac, local_files } = req.body;

    if (!mac || !Array.isArray(local_files)) {
        console.warn('[Sync] Invalid sync request payload from client');
        return res.status(400).json({ error: 'Missing MAC address or local_files array' });
    }

    const deviceFolder = path.join(STORAGE_DIR, formatMacFolderName(mac), 'bin');

    // Ensure the device folder exists (creates it on first contact if missing)
    if (!fs.existsSync(deviceFolder)) {
        fs.mkdirSync(deviceFolder, { recursive: true });
        console.log(`[Sync] Created new image directory for device MAC: ${mac}`);
    }

    // Map ESP32 local files by name for O(1) lookup
    const espFilesMap = new Map();
    local_files.forEach(file => {
        espFilesMap.set(file.name, { size: file.size, md5: file.md5 });
    });

    // Scan server-managed directory for this device
    const serverFiles = fs.readdirSync(deviceFolder).filter(file => file.endsWith('.bin'));

    const newDownloads = [];
    const serverFileNames = new Set(serverFiles);

    // Step A: Determine which files need to be downloaded or updated on the ESP32
    serverFiles.forEach(fileName => {
        const filePath = path.join(deviceFolder, fileName);
        const stats = fs.statSync(filePath);
        const serverSize = stats.size;
        const serverMD5 = getFileMD5(filePath);

        const espFile = espFilesMap.get(fileName);

        let needsDownload = false;

        if (!espFile) {
            // File doesn't exist on ESP32
            needsDownload = true;
            console.log(`[Sync] [${mac}] New file needed on client: ${fileName}`);
        } else if (espFile.size !== serverSize) {
            // File size mismatch (corrupted or updated)
            needsDownload = true;
            console.log(`[Sync] [${mac}] Size mismatch for ${fileName} (Client: ${espFile.size}, Server: ${serverSize})`);
        } else if (espFile.md5 && serverMD5 && espFile.md5 !== serverMD5) {
            // MD5 mismatch (file content updated on server)
            needsDownload = true;
            console.log(`[Sync] [${mac}] MD5 mismatch for ${fileName} (Client: ${espFile.md5}, Server: ${serverMD5})`);
        }

        if (needsDownload) {
            newDownloads.push({
                name: fileName,
                url: `/api/image/${formatMacFolderName(mac)}/${fileName}`,
                size: serverSize,
                md5: serverMD5
            });
        }
    });

    // Step B: Determine which files on the ESP32 are no longer on the server
    const filesToDelete = [];
    local_files.forEach(file => {
        if (!serverFileNames.has(file.name)) {
            filesToDelete.push(file.name);
            console.log(`[Sync] [${mac}] File marked for deletion on client: ${file.name}`);
        }
    });

    // Step C: Send the diff response back to ESP32
    const responsePayload = {
        new: newDownloads,
        delete: filesToDelete
    };

    console.log(`[Sync] [${mac}] Diff computed: ${newDownloads.length} to download, ${filesToDelete.length} to delete.`);
    return res.json(responsePayload);
});

/**
 * 2. GET /api/images/:macFolder/:fileName
 * Serves binary file streams to the ESP32 sequentially.
 */
app.get('/api/image/:macFolder/:fileName', (req, res) => {
    const { macFolder, fileName } = req.params;
    const filePath = path.join(STORAGE_DIR, macFolder, 'bin', fileName);

    // Prevent directory traversal attacks
    if (!filePath.startsWith(STORAGE_DIR)) {
        return res.status(403).send('Forbidden');
    }

    if (!fs.existsSync(filePath)) {
        return res.status(404).send('File not found');
    }

    // Stream file as binary
    res.setHeader('Content-Type', 'application/octet-stream');

	// Send file stream
	res.sendFile(filePath, (err) => {
	    if (err) {
	        console.error(`[HTTP Error] Failed to send ${filePath}:`, err);
	        if (!res.headersSent) {
	            res.status(500).send('Error streaming file');
	        }
	    } else {
	        console.log(`[HTTP] Successfully sent binary image: ${path.basename(filePath)}`);
	    }
	});
});




// Handling dual-file upload route
app.post('/upload', upload.fields([
    { name: 'original', maxCount: 1 },
    { name: 'dithered', maxCount: 1 }
]), (req, res) => {
    if (!req.files || !req.files.original || !req.files.dithered) {
        return res.status(400).json({ error: 'Both original and dithered images are required.' });
    }

    console.log('Saved Original Image:', req.files.original[0].path);
    console.log('Saved Dithered Image:', req.files.dithered[0].path);

    res.status(200).json({
        message: 'Images uploaded and saved successfully.',
        files: {
            original: req.files.original[0].filename,
            dithered: req.files.dithered[0].filename
        }
    });
});


// =========================================================================
// 5. Start Web Server
// =========================================================================
app.listen(config.httpPort, () => {
    console.log(`[HTTP] Web Admin interface running at http://localhost:${config.httpPort}`);
});