const express = require('express');
const dgram = require('dgram');
const path = require('path');

// Load configuration parameters from config.json
const config = require('./config.json');

const app = express();
app.use(express.json());

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
        udpServer.addMembership(config.multicastAddress, '0.0.0.0');
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