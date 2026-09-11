const express = require('express');
const dgram = require('dgram');
const path = require('path');
const os = require('os');
const fs = require('fs');

// Load configuration parameters from config.json
const config = require('./config.json');

const app = express();
app.use(express.json());

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

// 1. Serve static web files from the 'public' directory
app.use(express.static(path.join(__dirname, 'public')));

// 2. In-memory state tracking
let systemStatus = {
    lastEspIp: 'Not Connected',
    lastSeen: 'None',
    currentMessage: 'E-Paper System Ready'
};


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
const UPLOAD_BIN_DIR = path.join(__dirname, 'uploads/bin/');

app.get('/api/status', (req, res) => {
    res.json(systemStatus);
});

app.post('/api/update-message', (req, res) => {
    const { message } = req.body;
    if (message !== undefined) {
        systemStatus.currentMessage = message;
        console.log(`[Admin] Display message updated to: "${message}"`);
        res.json({ success: true, message: systemStatus.currentMessage });
    } else {
        res.status(400).json({ success: false, error: 'Missing message field' });
    }
});

app.get('/api/image', (req, res) => {
	console.log(req.headers);
	const deviceMac = req.headers['x-device-mac'];
	if (!deviceMac)
		console.log('[Server] Request: /api/image: Missing X-Device-MAC header');
	else
		console.log(`[Server] Request: /api/image: received from ESP32 MAC: ${deviceMac}`);
	
	requestedFile = "image.bin";
	if (requestedFile) {
        filePath = path.join(UPLOAD_BIN_DIR, path.basename(requestedFile));
    } else {
		// get the latest file from the UPLOAD_BIN_DIR
        try {
            const files = fs.readdirSync(UPLOAD_BIN_DIR)
                .filter(file => file.endsWith('.bin'))
                .map(file => ({
                    name: file,
                    time: fs.statSync(path.join(UPLOAD_BIN_DIR, file)).mtime.getTime()
                }))
                .sort((a, b) => b.time - a.time); // Sort newest first

            if (files.length === 0) {
                return res.status(404).send('No .bin images found on server');
            }

            filePath = path.join(UPLOAD_BIN_DIR, files[0].name);
        } catch (err) {
            console.error('[HTTP Error] Failed to read uploads directory:', err);
            return res.status(500).send('Server storage error');
        }
    }

    // Verify file existence before sending
    if (!fs.existsSync(filePath)) {
        return res.status(404).send('Requested image file not found');
    }

    // Set binary content headers for ESP32 streaming
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

app.get('/api/display-data', (req, res) => {
    res.json({
        message: systemStatus.currentMessage,
        updatedAt: systemStatus.lastSeen
    });
});

// =========================================================================
// 5. Start Web Server
// =========================================================================
app.listen(config.httpPort, () => {
    console.log(`[HTTP] Web Admin interface running at http://localhost:${config.httpPort}`);
});