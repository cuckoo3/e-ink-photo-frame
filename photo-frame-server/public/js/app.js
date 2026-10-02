// app.js - Main Dithering Workspace and Upload Workflow
const uploadInput = document.getElementById('upload');
const dropZone = document.getElementById('drop-zone');
const dropPrompt = document.getElementById('drop-prompt');
const ditherOptions = document.getElementById('dither-options');
const downloadButton = document.getElementById('download');
const uploadServerButton = document.getElementById('upload-server');
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const renderStatus = document.getElementById('render-status');

let currentLoadedImage = null;
let currentOffscreenCanvas = null;
let originalFileName = 'image.png';

document.addEventListener('DOMContentLoaded', () => {
    fetchDevices();
});

// Drag & Drop
['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
    dropZone.addEventListener(eventName, preventDefaults, false);
    document.body.addEventListener(eventName, preventDefaults, false);
});

function preventDefaults(e) {
    e.preventDefault();
    e.stopPropagation();
}

['dragenter', 'dragover'].forEach(eventName => {
    dropZone.addEventListener(eventName, () => dropZone.classList.add('drop-zone--over'), false);
});

['dragleave', 'drop'].forEach(eventName => {
    dropZone.addEventListener(eventName, () => dropZone.classList.remove('drop-zone--over'), false);
});

dropZone.addEventListener('drop', (e) => {
    const dt = e.dataTransfer;
    const files = dt.files;
    if (files && files.length > 0) {
        handleImageFile(files[0]);
    }
});

uploadInput.addEventListener('change', (event) => {
    const file = event.target.files[0];
    if (file) {
        handleImageFile(file);
    }
});

function handleImageFile(file) {
    if (!file.type.startsWith('image/')) {
        alert('Please drop an image file.');
        return;
    }
    originalFileName = file.name;
    const reader = new FileReader();
    reader.onload = function(e) {
        const img = new Image();
        img.onload = function() {
            currentLoadedImage = img;
            dropPrompt.classList.add('hidden');
            startProcessing();
        }
        img.src = e.target.result;
    }
    reader.readAsDataURL(file);
}

ditherOptions.addEventListener('change', () => {
    if (currentLoadedImage) {
        startProcessing();
    }
});

downloadButton.addEventListener('click', () => {
    if (!currentLoadedImage) {
        alert('Please load an image first.');
        return;
    }
    const link = document.createElement('a');
    link.download = 'dithered_image.png';
    link.href = canvas.toDataURL();
    link.click();
});

if (uploadServerButton) {
    uploadServerButton.addEventListener('click', async () => {
        if (!currentLoadedImage || !currentOffscreenCanvas) {
            alert('Please load an image first.');
            return;
        }

        const selectedMac = deviceSelect.value;
        if (!selectedMac) {
            alert('Please select a valid target device.');
            return;
        }

        renderStatus.textContent = 'Uploading...';
        renderStatus.className = 'font-bold text-blue-600 inline';

        try {
            const orientation = document.querySelector('input[name="orientation"]:checked').value;

            let uploadOriginalCanvas = currentOffscreenCanvas;
            let uploadDitheredCanvas = canvas;

            if (orientation === 'portrait') {
                uploadOriginalCanvas = rotateCanvasClockwise(currentOffscreenCanvas);
                uploadDitheredCanvas = rotateCanvasClockwise(canvas);
            }

            const originalBlob = await new Promise(resolve => uploadOriginalCanvas.toBlob(resolve, 'image/jpeg', 0.8));
            const ditheredBlob = await new Promise(resolve => uploadDitheredCanvas.toBlob(resolve, 'image/png'));

            const baseName = originalFileName.substring(0, originalFileName.lastIndexOf('.')) || originalFileName;

            const formData = new FormData();
            formData.append('mac', selectedMac);
            formData.append('original', originalBlob, `${baseName}.jpg`);
            formData.append('dithered', ditheredBlob, `${baseName}.png`);

            const response = await fetch('/admin/upload', {
                method: 'POST',
                body: formData
            });

            const resData = await response.json().catch(() => ({}));

            if (response.ok) {
                renderStatus.textContent = 'Uploaded successfully!';
                renderStatus.className = 'font-bold text-emerald-600 inline';
                loadDeviceImages(selectedMac);
            } else {
                renderStatus.textContent = resData.error || 'Upload failed.';
                renderStatus.className = 'font-bold text-red-600 inline';
            }
        } catch (error) {
            console.error('Upload Error:', error);
            renderStatus.textContent = 'Network or server error.';
            renderStatus.className = 'font-bold text-red-600 inline';
        }
    });
}

function startProcessing() {
    renderStatus.textContent = 'Processing...';
    renderStatus.className = 'font-bold text-blue-600 inline';

    setTimeout(() => {
        renderAndDither();
    }, 20);
}

function renderAndDither() {
    if (!currentLoadedImage) return;

    const orientation = document.querySelector('input[name="orientation"]:checked').value;

    currentOffscreenCanvas = getScaledAndCroppedCanvas(currentLoadedImage, orientation);

    canvas.width = currentOffscreenCanvas.width;   
    canvas.height = currentOffscreenCanvas.height; 

    if (orientation === 'portrait') {
        canvas.style.maxWidth = '1200px';
    } else {
        canvas.style.maxWidth = '1600px';
    }

    const offCtx = currentOffscreenCanvas.getContext('2d');
    const imageData = offCtx.getImageData(0, 0, canvas.width, canvas.height);

    applyDitheringToBuffer(imageData, ditherOptions.value);

    ctx.putImageData(imageData, 0, 0);

    renderStatus.textContent = 'Done';
    renderStatus.className = 'font-bold text-emerald-600 inline';
}