function isPrivateIP(ip) {
    // Standardize IPv6-mapped IPv4 addresses (e.g. ::ffff:192.168.1.50)
    const cleanIp = ip.replace(/^::ffff:/, '');

    if (cleanIp === '127.0.0.1' || cleanIp === '::1' || cleanIp === 'localhost') {
        return true;
    }

    const parts = cleanIp.split('.').map(Number);
    if (parts.length !== 4) return false;

    // 10.0.0.0/8
    if (parts[0] === 10) return true;
    // 172.16.0.0/12
    if (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) return true;
    // 192.168.0.0/16
    if (parts[0] === 192 && parts[1] === 168) return true;

    return false;
}

module.exports = function internalOnly(req, res, next) {
    const clientIp = req.headers['x-forwarded-for'] || req.socket.remoteAddress || '';
    
    if (isPrivateIP(clientIp)) {
        return next();
    }

    console.warn(`[${new Date().toLocaleString()}] [Security] Blocked external access attempt to internal endpoint from IP: ${clientIp}`);
    return res.status(403).json({ error: 'Forbidden: Access allowed from local network only.' });
};