/**
 * dither.js
 * Image cropping, OKLab color matching, and buffer-based dithering algorithms.
 */

// ----------------- Palette & Color Distance (OKLab) -----------------

function getColors() {
    return [
        { r: 25,  g: 30,  b: 33,  val: 0x0 }, // Black
        { r: 232, g: 232, b: 232, val: 0x1 }, // White
        { r: 239, g: 222, b: 68,  val: 0x2 }, // Yellow
        { r: 178, g: 19,  b: 24,  val: 0x3 }, // Red
        { r: 33,  g: 87,  b: 186, val: 0x5 }, // Blue
        { r: 30,  g: 130, b: 60,  val: 0x6 }  // Green
    ];
}

function sRGBtoOKLab(c) {
    let r = c.r / 255, g = c.g / 255, b = c.b / 255;
    r = r > 0.04045 ? Math.pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = g > 0.04045 ? Math.pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = b > 0.04045 ? Math.pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    const l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969567 * b;
    const s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    const l_ = Math.cbrt(l), m_ = Math.cbrt(m), s_ = Math.cbrt(s);

    return {
        L: 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        a: 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        b: 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    };
}

function colorDistance(c1, c2) {
    const lab1 = sRGBtoOKLab(c1);
    const lab2 = sRGBtoOKLab(c2);
    const dL = lab1.L - lab2.L;
    const da = lab1.a - lab2.a;
    const db = lab1.b - lab2.b;
    return Math.sqrt(dL * dL + da * da + db * db);
}

function findNearestColor(pixel, colors) {
    let nearestColor = colors[0];
    let minDistance = colorDistance(pixel, colors[0]);
    for (let i = 1; i < colors.length; i++) {
        const distance = colorDistance(pixel, colors[i]);
        if (distance < minDistance) {
            minDistance = distance;
            nearestColor = colors[i];
        }
    }
    return nearestColor;
}

function clamp(value, min = 0, max = 255) {
    return Math.max(min, Math.min(value, max));
}

function getScaledAndCroppedCanvas(img, orientation = 'landscape') {
    const isPortrait = orientation === 'portrait';
    const targetWidth = isPortrait ? 1200 : 1600;
    const targetHeight = isPortrait ? 1600 : 1200;

    const scale = Math.max(targetWidth / img.width, targetHeight / img.height);
    const cropWidth = targetWidth / scale;
    const cropHeight = targetHeight / scale;
    const cropX = (img.width - cropWidth) / 2;
    const cropY = (img.height - cropHeight) / 2;

    const offscreen = document.createElement('canvas');
    offscreen.width = targetWidth;
    offscreen.height = targetHeight;
    const offCtx = offscreen.getContext('2d');

    offCtx.drawImage(
        img,
        cropX, cropY, cropWidth, cropHeight,
        0, 0, targetWidth, targetHeight
    );

    return offscreen;
}

/**
 * Rotates a portrait canvas (1200x1600) 90 degrees clockwise to 1600x1200 landscape.
 */
function rotateCanvasClockwise(sourceCanvas) {
    const rotated = document.createElement('canvas');
    rotated.width = sourceCanvas.height; // 1600
    rotated.height = sourceCanvas.width; // 1200

    const ctx = rotated.getContext('2d');
    ctx.translate(rotated.width, 0);
    ctx.rotate((90 * Math.PI) / 180);
    ctx.drawImage(sourceCanvas, 0, 0);

    return rotated;
}

/**
 * Draws a solid white border along the outer edges of a canvas context.
 */
function addWhiteBorder(context, width, height, borderWidth) {
    context.fillStyle = '#FFFFFF';
    context.fillRect(0, 0, width, borderWidth);
    context.fillRect(0, height - borderWidth, width, borderWidth);
    context.fillRect(0, 0, borderWidth, height);
    context.fillRect(width - borderWidth, 0, borderWidth, height);
}

// ----------------- Dithering Router & Algorithms -----------------

function applyDitheringToBuffer(imageData, selectedAlgorithm) {
    const colors = getColors();

    if (selectedAlgorithm === 'floyd-steinberg') {
        applyFloydSteinbergDithering(imageData, colors);
    } else if (selectedAlgorithm === 'jjn') {
        applyJJNDithering(imageData, colors);
    } else if (selectedAlgorithm === 'stucki') {
        applyStuckiDithering(imageData, colors);
    }
}

// Floyd-Steinberg
function applyFloydSteinbergDithering(imageData, colors) {
    const pixels = imageData.data;
    const width = imageData.width;
    const height = imageData.height;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const index = (y * width + x) * 4;
            const oldPixel = { r: pixels[index], g: pixels[index + 1], b: pixels[index + 2] };
            const newPixel = findNearestColor(oldPixel, colors);

            pixels[index]     = newPixel.r;
            pixels[index + 1] = newPixel.g;
            pixels[index + 2] = newPixel.b;

            const quantErrorR = oldPixel.r - newPixel.r;
            const quantErrorG = oldPixel.g - newPixel.g;
            const quantErrorB = oldPixel.b - newPixel.b;

            distributeFloydSteinbergError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB);
        }
    }
}

function distributeFloydSteinbergError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB) {
    const damping = 0.8;
    const neighbors = [
        { dx: 1, dy: 0, w: 7 / 16 },
        { dx: -1, dy: 1, w: 3 / 16 },
        { dx: 0, dy: 1, w: 5 / 16 },
        { dx: 1, dy: 1, w: 1 / 16 }
    ];

    for (let i = 0; i < neighbors.length; i++) {
        const nx = x + neighbors[i].dx;
        const ny = y + neighbors[i].dy;
        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            const index = (ny * width + nx) * 4;
            const weight = neighbors[i].w * damping;
            pixels[index]     = clamp(pixels[index]     + quantErrorR * weight);
            pixels[index + 1] = clamp(pixels[index + 1] + quantErrorG * weight);
            pixels[index + 2] = clamp(pixels[index + 2] + quantErrorB * weight);
        }
    }
}

// JJN
function applyJJNDithering(imageData, colors) {
    const pixels = imageData.data;
    const width = imageData.width;
    const height = imageData.height;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const index = (y * width + x) * 4;
            const oldPixel = { r: pixels[index], g: pixels[index + 1], b: pixels[index + 2] };
            const newPixel = findNearestColor(oldPixel, colors);

            pixels[index]     = newPixel.r;
            pixels[index + 1] = newPixel.g;
            pixels[index + 2] = newPixel.b;

            const quantErrorR = oldPixel.r - newPixel.r;
            const quantErrorG = oldPixel.g - newPixel.g;
            const quantErrorB = oldPixel.b - newPixel.b;

            distributeJJNError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB);
        }
    }
}

function distributeJJNError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB) {
    const damping = 0.8; 
    const neighbors = [
        { dx: 1, dy: 0, weight: (7 / 48) * damping },
        { dx: 2, dy: 0, weight: (5 / 48) * damping },
        { dx: -2, dy: 1, weight: (3 / 48) * damping },
        { dx: -1, dy: 1, weight: (5 / 48) * damping },
        { dx: 0, dy: 1, weight: (7 / 48) * damping },
        { dx: 1, dy: 1, weight: (5 / 48) * damping },
        { dx: 2, dy: 1, weight: (3 / 48) * damping },
        { dx: -2, dy: 2, weight: (1 / 48) * damping },
        { dx: -1, dy: 2, weight: (3 / 48) * damping },
        { dx: 0, dy: 2, weight: (5 / 48) * damping },
        { dx: 1, dy: 2, weight: (3 / 48) * damping },
        { dx: 2, dy: 2, weight: (1 / 48) * damping }
    ];

    for (let i = 0; i < neighbors.length; i++) {
        const nx = x + neighbors[i].dx;
        const ny = y + neighbors[i].dy;
        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            const index = (ny * width + nx) * 4;
            const w = neighbors[i].weight;
            pixels[index]     = clamp(pixels[index]     + quantErrorR * w);
            pixels[index + 1] = clamp(pixels[index + 1] + quantErrorG * w);
            pixels[index + 2] = clamp(pixels[index + 2] + quantErrorB * w);
        }
    }
}

// Stucki
function applyStuckiDithering(imageData, colors) {
    const pixels = imageData.data;
    const width = imageData.width;
    const height = imageData.height;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const index = (y * width + x) * 4;
            const oldPixel = { r: pixels[index], g: pixels[index + 1], b: pixels[index + 2] };
            const newPixel = findNearestColor(oldPixel, colors);

            pixels[index]     = newPixel.r;
            pixels[index + 1] = newPixel.g;
            pixels[index + 2] = newPixel.b;

            const quantErrorR = oldPixel.r - newPixel.r;
            const quantErrorG = oldPixel.g - newPixel.g;
            const quantErrorB = oldPixel.b - newPixel.b;

            distributeStuckiError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB);
        }
    }
}

function distributeStuckiError(pixels, width, height, x, y, quantErrorR, quantErrorG, quantErrorB) {
    const damping = 0.8; 
    const neighbors = [
        { dx: 1, dy: 0, weight: (8 / 42) * damping },
        { dx: 2, dy: 0, weight: (4 / 42) * damping },
        { dx: -2, dy: 1, weight: (2 / 42) * damping },
        { dx: -1, dy: 1, weight: (4 / 42) * damping },
        { dx: 0, dy: 1, weight: (8 / 42) * damping },
        { dx: 1, dy: 1, weight: (5 / 42) * damping },
        { dx: 2, dy: 1, weight: (2 / 42) * damping },
        { dx: -2, dy: 2, weight: (1 / 42) * damping },
        { dx: -1, dy: 2, weight: (2 / 42) * damping },
        { dx: 0, dy: 2, weight: (4 / 42) * damping },
        { dx: 1, dy: 2, weight: (2 / 42) * damping },
        { dx: 2, dy: 2, weight: (1 / 42) * damping }
    ];

    for (let i = 0; i < neighbors.length; i++) {
        const nx = x + neighbors[i].dx;
        const ny = y + neighbors[i].dy;
        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            const index = (ny * width + nx) * 4;
            const w = neighbors[i].weight;
            pixels[index]     = clamp(pixels[index]     + quantErrorR * w);
            pixels[index + 1] = clamp(pixels[index + 1] + quantErrorG * w);
            pixels[index + 2] = clamp(pixels[index + 2] + quantErrorB * w);
        }
    }
}