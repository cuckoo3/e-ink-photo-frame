const sharp = require('sharp');
const fs = require('fs');
const path = require('path');

/**
 * Maps exact canvas palette RGB values to 4-bit EPD color codes.
 */
function rgbToSixColor(r, g, b) {
    // Exact match checks for the 6 target palette colors
    if (r === 25  && g === 30  && b === 33)  return 0x0; // Black
    if (r === 232 && g === 232 && b === 232) return 0x1; // White
    if (r === 239 && g === 222 && b === 68)  return 0x2; // Yellow
    if (r === 178 && g === 19  && b === 24)  return 0x3; // Red
    if (r === 33  && g === 87  && b === 186) return 0x5; // Blue
    if (r === 30  && g === 130 && b === 60)  return 0x6; // Green

    // Fallback: If browser canvas / PNG encoding slightly shifted a color,
    // quick threshold check keeps it accurate without overhead.
    if (r < 100 && g < 100 && b < 100) return 0x0; // Black
    if (r > 180 && g > 180 && b < 100) return 0x2; // Yellow
    if (r > 150 && g < 100 && b < 100) return 0x3; // Red
    if (r < 100 && g < 100 && b > 120) return 0x5; // Blue
    if (r < 100 && g > 100 && b < 100) return 0x6; // Green

    return 0x1; // Default: White
}

/**
 * Converts a PNG image buffer into a dual-driver .bin file matching Python logic.
 */
async function convertPngToBin(pngBuffer, outputBinPath, config = {}) {
    const width = config.epdWidth / 2;    // 600 Bytes
    const width1 = width / 2;      // 300 Bytes (CS0 / CS1 split)
    const height = config.epdHeight;      // 1600 Rows

    let pipeline = sharp(pngBuffer);
    const metadata = await pipeline.metadata();

    let imageWidth = metadata.width;
    let imageHeight = metadata.height;

    // 1. Rotate 90 deg counter-clockwise (270 deg clockwise) if landscape
    if (imageHeight < imageWidth) {
        pipeline = pipeline.rotate(270);
        const temp = imageWidth;
        imageWidth = imageHeight;
        imageHeight = temp;
    }

    // 2. Validate strict target dimensions
    if (imageWidth !== config.epdWidth || imageHeight !== config.epdHeight) {
        throw new Error(
            `Invalid image dimensions (${imageWidth}x${imageHeight}). Must be ${epdWidth}x${epdHeight}.`
        );
    }

    // 3. Resize precisely and extract raw RGB buffer
    const { data } = await pipeline
		.removeAlpha()
        .raw()
        .toBuffer({ resolveWithObject: true });

    // Allocate 2D Byte Buffer: 1600 rows x 600 bytes
    const rawBytes = Array.from({ length: height }, () => new Uint8Array(width));

    // 4. Combine High/Low Nibbles per pixel pair
    for (let r = 0; r < height; r++) {
        for (let c = 0; c < config.epdWidth; c += 2) {
            // First Pixel (High Nibble)
            const idx1 = (r * config.epdWidth + c) * 3;
            const color1 = rgbToSixColor(data[idx1], data[idx1 + 1], data[idx1 + 2]);

            // Second Pixel (Low Nibble)
            const idx2 = (r * config.epdWidth + (c + 1)) * 3;
            const color2 = rgbToSixColor(data[idx2], data[idx2 + 1], data[idx2 + 2]);

            // Combine into 1 byte (High << 4 | Low)
            rawBytes[r][c / 2] = (color1 << 4) | color2;
        }
    }

    // 5. CS0 / CS1 Dual-Chip Memory Reconstruction (Per-Row Interleaving)
    // Target Buffer: 1600 * 600 = 960,000 bytes
    const reconstructedBytes = Buffer.alloc(height * width);
    let offset = 0;

    for (let r = 0; r < height; r++) {
        // CS0: Copy first 300 bytes of row `r`
        for (let c = 0; c < width1; c++) {
            reconstructedBytes[offset++] = rawBytes[r][c];
        }
        // CS1: Copy remaining 300 bytes of row `r`
        for (let c = width1; c < width; c++) {
            reconstructedBytes[offset++] = rawBytes[r][c];
        }
    }

    // 6. Output to target binary path
    const binDir = path.dirname(outputBinPath);
    if (!fs.existsSync(binDir)) {
        fs.mkdirSync(binDir, { recursive: true });
    }

    await fs.promises.writeFile(outputBinPath, reconstructedBytes);
    return reconstructedBytes.length; // Returns 960000
}

module.exports = { convertPngToBin };