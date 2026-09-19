const dgram = require('dgram');

function initUdpListener(config, physicalIP) {
    const udpServer = dgram.createSocket({ type: 'udp4', reuseAddr: true });

    udpServer.on('error', (err) => {
        console.error(`[UDP Error] Server error:\n${err.stack}`);
        udpServer.close();
    });

    udpServer.on('listening', () => {
        try {
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
        
        if (messageStr === config.discoveryRequestMsg) {
            console.log(`[UDP] Discovery request received from ESP32 (${rinfo.address}:${rinfo.port})`);
            
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

    udpServer.bind(config.udpPort, '0.0.0.0');
    return udpServer;
}

module.exports = initUdpListener;