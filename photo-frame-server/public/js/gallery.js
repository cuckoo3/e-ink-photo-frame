// gallery.js - Asset Gallery, Modals, and Image Transformations
const hoverPopup = document.getElementById('hoverPopup');
const hoverImage = document.getElementById('hoverImage');
const hoverImageTitle = document.getElementById('hoverImageTitle');

// Mouse tracking for preview popup
document.addEventListener('mousemove', (e) => {
    if (!hoverPopup.classList.contains('hidden')) {
        const offset = 15;
        let left = e.clientX + offset;
        let top = e.clientY + offset;

        const popupRect = hoverPopup.getBoundingClientRect();
        if (left + popupRect.width > window.innerWidth) {
            left = e.clientX - popupRect.width - offset;
        }
        if (top + popupRect.height > window.innerHeight) {
            top = e.clientY - popupRect.height - offset;
        }

        hoverPopup.style.left = `${Math.max(10, left)}px`;
        hoverPopup.style.top = `${Math.max(10, top)}px`;
    }
});

function rotateImageUrlAntiClockwise(url) {
    return new Promise((resolve) => {
        const img = new Image();
        img.crossOrigin = 'anonymous';
        img.onload = function() {
            const tempCanvas = document.createElement('canvas');
            tempCanvas.width = img.height; 
            tempCanvas.height = img.width; 

            const ctx = tempCanvas.getContext('2d');
            ctx.translate(0, tempCanvas.height);
            ctx.rotate((-90 * Math.PI) / 180);
            ctx.drawImage(img, 0, 0);

            resolve(tempCanvas.toDataURL('image/png'));
        };
        img.onerror = function() {
            resolve(url);
        };
        img.src = url;
    });
}

async function loadDeviceImages(deviceId) {
    const grid = document.getElementById('imageGrid');
    grid.innerHTML = '<p class="text-gray-500 col-span-full text-center py-4">Loading images...</p>';

    try {
        const res = await fetch(`/admin/images/${deviceId}`);
        const data = await res.json();
        grid.innerHTML = '';

        if (!data.images || data.images.length === 0) {
            grid.innerHTML = '<p class="text-gray-400 col-span-full text-center py-4">No images stored for this device.</p>';
            return;
        }

        const orientation = document.querySelector('input[name="orientation"]:checked').value;

        for (const image of data.images) {
            let displayThumbUrl = image.thumbUrl;

            if (orientation === 'portrait') {
                displayThumbUrl = await rotateImageUrlAntiClockwise(image.thumbUrl);
            }

            const card = document.createElement('div');
            card.className = "bg-white border rounded-lg shadow-sm p-3 flex flex-col items-center justify-between hover:shadow-md transition";

            card.innerHTML = `
                <div class="w-full h-36 flex flex-col items-center justify-center overflow-hidden rounded bg-gray-50 mb-3 cursor-pointer"
                     onclick="openModal('${image.ditheredUrl}', '${image.name}')"
                     onmouseenter="showPopup('${image.ditheredUrl}', '${image.name}')" 
                     onmouseleave="hidePopup()">
                    <img src="${displayThumbUrl}" alt="${image.name}" class="object-cover max-h-28 max-w-full rounded mb-1">
                    <span class="text-xs text-gray-600 font-mono truncate w-full text-center px-1">${image.name}</span>
                </div>
                <button onclick="deleteImage('${deviceId}', '${image.name}')" class="w-full bg-red-500 hover:bg-red-600 text-white text-xs font-semibold py-1.5 px-3 rounded transition">
                    Delete
                </button>
            `;
            grid.appendChild(card);
        }
    } catch (err) {
        grid.innerHTML = '<p class="text-red-500 col-span-full text-center py-4">Failed to load images.</p>';
    }
}

async function showPopup(ditheredUrl, imageName) {
    const orientation = document.querySelector('input[name="orientation"]:checked').value;
    
    hoverImageTitle.textContent = imageName;
    hoverPopup.classList.remove('hidden');

    if (orientation === 'portrait') {
        const rotatedDataUrl = await rotateImageUrlAntiClockwise(ditheredUrl);
        hoverImage.src = rotatedDataUrl;
    } else {
        hoverImage.src = ditheredUrl;
    }
}

function hidePopup() {
    hoverPopup.classList.add('hidden');
    hoverImage.src = '';
    hoverImageTitle.textContent = '';
}

async function openModal(ditheredUrl, imageName) {
    hidePopup();
    const modal = document.getElementById('imageModal');
    const modalImg = document.getElementById('modalImage');
    const orientation = document.querySelector('input[name="orientation"]:checked').value;

    document.getElementById('modalTitle').textContent = imageName;

    if (orientation === 'portrait') {
        const rotatedDataUrl = await rotateImageUrlAntiClockwise(ditheredUrl);
        modalImg.src = rotatedDataUrl;
        modalImg.style.maxWidth = '1200px';
        modalImg.style.width = '100%';
        modalImg.style.height = 'auto';
    } else {
        modalImg.src = ditheredUrl;
        modalImg.style.maxWidth = '1600px';
        modalImg.style.width = '100%';
        modalImg.style.height = 'auto';
    }

    modal.classList.remove('hidden');
}

function closeModal() {
    const modal = document.getElementById('imageModal');
    const modalImg = document.getElementById('modalImage');
    modal.classList.add('hidden');
    modalImg.src = '';
    modalImg.style.maxWidth = '';
    modalImg.style.width = '';
    modalImg.style.height = '';
}

async function deleteImage(deviceId, imageName) {
    if (!confirm(`Are you sure you want to delete ${imageName}?`)) return;

    try {
        const res = await fetch(`/admin/image/${deviceId}/${imageName}`, { method: 'DELETE' });
        const data = await res.json();
        if (data.success) {
            hidePopup();
            closeModal();
            loadDeviceImages(deviceId);
        } else {
            alert(`Delete failed: ${data.error}`);
        }
    } catch (err) {
        alert('Failed to delete image.');
    }
}