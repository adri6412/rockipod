const { ipcRenderer } = require('electron');
const path = require('path');
const fs = require('fs');

// State
let selectedDevicePath = null;
let pcLibrary = []; // Objects: { path, name, size, artist, album }
let deviceLibrary = []; // Objects from device
let currentView = 'library'; // 'library' or 'device'

// Elements
const navLibrary = document.getElementById('nav-library');
const navDevice = document.getElementById('nav-device');
const btnAddFolder = document.getElementById('btn-add-folder'); // Top Right
const btnSync = document.getElementById('btn-sync'); // Top Right
const btnRebuild = document.getElementById('btn-rebuild-db'); // Top Right

const fileList = document.getElementById('file-list');
const statusOverlay = document.getElementById('status-overlay');
const statusTitle = document.getElementById('status-title');
const statusMessage = document.getElementById('status-message');
const lcdTitle = document.querySelector('.lcd-title');
const lcdSubtitle = document.querySelector('.lcd-subtitle');

// Handlers

// 1. Device Selection / Navigation
navDevice.addEventListener('click', async () => {
    // If no device selected, ask for it
    if (!selectedDevicePath) {
        const path = await ipcRenderer.invoke('select-folder');
        if (path) {
            selectedDevicePath = path;
            lcdSubtitle.innerText = "Device: " + path;
        }
    } else {
        // Already selected. Allow changing if clicked while already active
        if (navDevice.classList.contains('active')) {
            const path = await ipcRenderer.invoke('select-folder');
            if (path) {
                selectedDevicePath = path;
                lcdSubtitle.innerText = "Device: " + path;
            }
        }
    }

    // Switch UI to Device View
    currentView = 'device';
    navLibrary.classList.remove('active');
    navDevice.classList.add('active');
    document.getElementById('view-title').innerText = "Device Music";

    // Disable Add Folder/Sync in Device View
    btnAddFolder.style.display = 'none';
    btnSync.style.display = 'none';
    btnRebuild.style.display = 'inline-block';

    // Scan Device Content
    if (selectedDevicePath) {
        showStatus(true, "Reading Device...", "Scanning files on device...");
        const files = await ipcRenderer.invoke('scan-library', selectedDevicePath);

        deviceLibrary = files;

        renderList(deviceLibrary);
        showStatus(false);
        lcdTitle.innerText = `${deviceLibrary.length} Songs on Device`;
    } else {
        renderList([]);
        lcdTitle.innerText = "No Device Selected";
    }

    checkSyncReady();
});

navLibrary.addEventListener('click', () => {
    currentView = 'library';
    navLibrary.classList.add('active');
    navDevice.classList.remove('active');
    document.getElementById('view-title').innerText = "Library";

    // Show Controls
    btnAddFolder.style.display = 'inline-block';
    btnSync.style.display = 'inline-block';

    renderList(pcLibrary);
    lcdTitle.innerText = `${pcLibrary.length} Songs`;
});

// 2. Add Folder
btnAddFolder.addEventListener('click', async () => {
    const folder = await ipcRenderer.invoke('select-folder');
    if (folder) {
        showStatus(true, "Scanning...", "Reading library...");
        const files = await ipcRenderer.invoke('scan-library', folder);

        pcLibrary = [...pcLibrary, ...files];
        renderList(pcLibrary);
        showStatus(false);
        checkSyncReady();

        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    }
});

// 3. Rebuild DB
btnRebuild.addEventListener('click', async () => {
    if (!selectedDevicePath) return;

    statusTitle.innerText = "Rebuilding Database...";
    statusMessage.innerText = "Scanning device and generating .rdb file...";
    showStatus(true);

    // Call DB Generator directly on the device path
    const result = await ipcRenderer.invoke('generate-db', selectedDevicePath, selectedDevicePath);

    if (result.success) {
        showStatus(true, "Success!", `Database updated. Found ${result.count} tracks.`);
        setTimeout(() => showStatus(false), 3000);
    } else {
        showStatus(true, "Error", "DB Generation failed: " + result.error);
        setTimeout(() => showStatus(false), 5000);
    }
});

// 4. Sync
btnSync.addEventListener('click', async () => {
    if (!selectedDevicePath || pcLibrary.length === 0) return;

    // 1. Copy Files
    showStatus(true, "Syncing...", "Copying files to device...");

    let copiedCount = 0;

    // Create/Verify target music folder
    const targetDir = path.join(selectedDevicePath, 'Music');
    if (!fs.existsSync(targetDir)) {
        try {
            fs.mkdirSync(targetDir);
        } catch (e) { console.error(e); }
    }

    for (const file of pcLibrary) {
        const destPath = path.join(targetDir, file.name);
        try {
            statusMessage.innerText = `Copying ${file.name}...`;
            await new Promise((resolve, reject) => {
                fs.copyFile(file.path, destPath, (err) => {
                    if (err) reject(err);
                    else resolve();
                });
            });
            copiedCount++;
        } catch (e) {
            console.error(`Failed to copy ${file.name}`, e);
        }
    }

    // 2. Generate DB
    statusTitle.innerText = "Building Database...";
    statusMessage.innerText = "Parsing metadata and generating .rdb file...";

    const result = await ipcRenderer.invoke('generate-db', selectedDevicePath, selectedDevicePath);

    if (result.success) {
        showStatus(true, "Success!", `Copied ${copiedCount} files.\nDatabase updated with ${result.count} tracks.`);
        setTimeout(() => showStatus(false), 3000);
    } else {
        showStatus(true, "Error", "DB Generation failed: " + result.error);
        setTimeout(() => showStatus(false), 5000);
    }
});


// Helpers
function checkSyncReady() {
    btnSync.disabled = !(selectedDevicePath && pcLibrary.length > 0);
    btnRebuild.disabled = !selectedDevicePath;
}

function renderList(files) {
    fileList.innerHTML = '';
    files.forEach(file => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td style="text-align:center;">🎵</td>
            <td>${file.name}</td>
            <td>${file.artist || '-'}</td>
            <td>${file.album || '-'}</td>
            <td>${(file.size / 1024 / 1024).toFixed(1)} MB</td>
        `;
        fileList.appendChild(tr);
    });
}

function showStatus(show, title, msg) {
    if (show) {
        if (title) statusTitle.innerText = title;
        if (msg) statusMessage.innerText = msg;
        statusOverlay.classList.remove('hidden');
    } else {
        statusOverlay.classList.add('hidden');
    }
}
