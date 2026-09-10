const express = require('express');
const dgram = require('dgram');
const path = require('path');
const os = require('os');

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