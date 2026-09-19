const path = require('path');
const fs = require('fs');

/**
 * Normalizes MAC addresses to uppercase without colons (e.g., "24:DC:C3:A1:B2:C3" -> "24DCC3A1B2C3").
 */
function formatMac(mac) {
    if (!mac || typeof mac !== 'string') return '';
    return mac.replace(/:/g, '').toUpperCase();
}

/**
 * Ensures a device entry exists in registry memory and persists changes if newly registered.
 */
function ensureDevice(mac, registry, saveRegistryToDisk) {
    const formattedMac = formatMac(mac);
    if (!formattedMac) return null;

    let isNew = false;
    if (!registry.devices[formattedMac]) {
        registry.devices[formattedMac] = { images: [] };
        isNew = true;
        if (typeof saveRegistryToDisk === 'function') {
            saveRegistryToDisk();
        }
    }

    return { mac: formattedMac, isNew };
}

/**
 * Retrieves the image array for a specific device from registry memory.
 */
function getDeviceImages(mac, registry) {
    const formattedMac = formatMac(mac);
    if (!formattedMac || !registry.devices[formattedMac]) {
        return [];
    }
    return registry.devices[formattedMac].images || [];
}

/**
 * Updates or adds an image record for a specific device in the registry memory and persists to disk.
 * Always places the new/updated image record at the beginning of the array (newest first).
 */
function upsertDeviceImage(mac, imageRecord, registry, saveRegistryToDisk) {
    const formattedMac = formatMac(mac);
    if (!formattedMac) return;

    // Ensure the device exists in the registry
    ensureDevice(formattedMac, registry, saveRegistryToDisk);

    const images = registry.devices[formattedMac].images;
    const existingIndex = images.findIndex(img => img.name === imageRecord.name);

    // If record already exists, remove it from its current position
    if (existingIndex >= 0) {
        images.splice(existingIndex, 1);
    }

    // Always insert the fresh image record at index 0 (top of the array)
    images.unshift(imageRecord);

    // Save changes to devices.json
    if (typeof saveRegistryToDisk === 'function') {
        saveRegistryToDisk();
    }
}

module.exports = {
    formatMac,
    ensureDevice,
    getDeviceImages,
    upsertDeviceImage
};