const { ipcRenderer } = require('electron');
const path = require('path');
const fs = require('fs');

// Constants
const LIBRARY_FILE = path.join(__dirname, 'library.json');

// State
let selectedDevicePath = null;
let pcLibrary = []; // Objects: { path, name, size, artist, album, checked }
let deviceLibrary = [];
let currentView = 'library'; // 'library' or 'device'

// Filter State
let selectedArtist = null; // null = All
let selectedAlbum = null;  // null = All

// Elements
const navLibrary = document.getElementById('nav-library');
const navDevice = document.getElementById('nav-device');
const btnAddFolder = document.getElementById('btn-add-folder');
const btnSync = document.getElementById('btn-sync');
const btnRebuild = document.getElementById('btn-rebuild-db');
const btnRefresh = document.getElementById('btn-refresh');

const fileList = document.getElementById('file-list');
const statusOverlay = document.getElementById('status-overlay');
const statusTitle = document.getElementById('status-title');
const statusMessage = document.getElementById('status-message');
const lcdTitle = document.querySelector('.lcd-title');
const lcdSubtitle = document.querySelector('.lcd-subtitle');

const browserColumns = document.getElementById('browser-columns');
const listArtists = document.getElementById('list-artists');
const listAlbums = document.getElementById('list-albums');
const checkAll = document.getElementById('check-all');


// --- Init ---
loadLibrary();

// --- Handlers ---

// 1. Navigation
navLibrary.addEventListener('click', () => {
    setView('library');
});

navDevice.addEventListener('click', async () => {
    if (!selectedDevicePath || navDevice.classList.contains('active')) {
        const path = await ipcRenderer.invoke('select-folder');
        if (path) {
            selectedDevicePath = path;
            lcdSubtitle.innerText = "Device: " + path;
            scanDevice();
        }
    } else {
        scanDevice();
    }
    setView('device');
});

function setView(view) {
    currentView = view;
    // Determine active library for filtering
    let activeLib = currentView === 'library' ? pcLibrary : deviceLibrary;

    if (view === 'library') {
        navLibrary.classList.add('active');
        navDevice.classList.remove('active');
        document.getElementById('view-title').innerText = "Library"; // Fixed title string

        // Show Controls
        btnAddFolder.style.display = 'inline-block';
        btnSync.style.display = 'inline-block';
        btnRefresh.style.display = 'inline-block';

        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    } else {
        navDevice.classList.add('active');
        navLibrary.classList.remove('active');
        document.getElementById('view-title').innerText = "Device Music";

        // Hide/Change Controls
        btnAddFolder.style.display = 'none';
        btnSync.style.display = 'none';
        btnRefresh.style.display = 'none';

        lcdTitle.innerText = `${deviceLibrary.length} Songs on Device`;
    }

    // Checkbox Validations
    checkSyncReady();

    // Always show columns and update them based on current view's data
    browserColumns.classList.remove('hidden');
    selectedArtist = null; // Reset filters on view switch
    selectedAlbum = null;

    updateBrowser();
    renderList(filterLibrary(activeLib), currentView === 'library');
}

// 2. Add Folder & Scan
btnAddFolder.addEventListener('click', async () => {
    const folder = await ipcRenderer.invoke('select-folder');
    if (folder) {
        showStatus(true, "Scanning...", "Reading library...");
        const files = await ipcRenderer.invoke('scan-library', folder);

        // Merge into library (check for duplicates?)
        // For simplicity, just append new files
        const newFiles = files.map(f => ({ ...f, checked: true }));
        pcLibrary = [...pcLibrary, ...newFiles];

        saveLibrary();
        updateBrowser();
        renderList(filterLibrary(pcLibrary));
        showStatus(false);
        checkSyncReady();
        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    }
});

btnRefresh.addEventListener('click', () => {
    // Re-render and clear filters
    selectedArtist = null;
    selectedAlbum = null;
    updateBrowser();
    renderList(filterLibrary(pcLibrary));
});

// 3. Browser Logic (Artists / Albums)
function updateBrowser() {
    let activeLib = currentView === 'library' ? pcLibrary : deviceLibrary;
    // 1. Get Unique Artists
    const artists = new Set();
    activeLib.forEach(f => artists.add(f.artist || "Unknown"));
    const sortedArtists = Array.from(artists).sort();

    // Render Artists
    renderColumn(listArtists, ["All Artists", ...sortedArtists], selectedArtist, (val) => {
        selectedArtist = val === "All Artists" ? null : val;
        selectedAlbum = null; // Reset album when artist changes
        updateAlbumColumn();
        renderList(filterLibrary(activeLib), currentView === 'library');
    });

    updateAlbumColumn();
}

function updateAlbumColumn() {
    let activeLib = currentView === 'library' ? pcLibrary : deviceLibrary;
    const albums = new Set();
    // Filter albums by selected artist
    activeLib.forEach(f => {
        if (!selectedArtist || (f.artist || "Unknown") === selectedArtist) {
            albums.add(f.album || "Unknown");
        }
    });
    const sortedAlbums = Array.from(albums).sort();

    renderColumn(listAlbums, ["All Albums", ...sortedAlbums], selectedAlbum, (val) => {
        selectedAlbum = val === "All Albums" ? null : val;
        renderList(filterLibrary(activeLib), currentView === 'library');
    });
}

function renderColumn(element, items, selectedValue, onClick) {
    element.innerHTML = '';
    items.forEach(item => {
        const li = document.createElement('li');
        li.className = 'browser-item';
        if (item === (selectedValue || "All Artists") || item === (selectedValue || "All Albums")) {
            li.classList.add('selected');
        }
        li.innerText = item;
        li.addEventListener('click', () => {
            onClick(item);
            // Re-render columns to update selected state
            // Optimization: Just update status classes? For now full re-render is fine.
            if (element.id === 'list-artists') updateBrowser(); // Full update
            else updateAlbumColumn(); // Just albums
        });
        element.appendChild(li);
    });
}

function filterLibrary(lib) {
    return lib.filter(f => {
        if (selectedArtist && (f.artist || "Unknown") !== selectedArtist) return false;
        if (selectedAlbum && (f.album || "Unknown") !== selectedAlbum) return false;
        return true;
    });
}


// 4. Rebuild DB
btnRebuild.addEventListener('click', async () => {
    if (!selectedDevicePath) return;
    performDbGen();
});

async function performDbGen() {
    statusTitle.innerText = "Rebuilding Database...";
    statusMessage.innerText = "Scanning device and generating .rdb file...";
    showStatus(true);

    const result = await ipcRenderer.invoke('generate-db', selectedDevicePath, selectedDevicePath);

    if (result.success) {
        showStatus(true, "Success!", `Database updated. Found ${result.count} tracks.`);
        setTimeout(() => showStatus(false), 3000);
    } else {
        showStatus(true, "Error", "DB Gen failed: " + result.error);
        setTimeout(() => showStatus(false), 5000);
    }
}

// 5. Sync (Selective)
btnSync.addEventListener('click', async () => {
    // Sync ONLY CHECKED items from PC Library
    const itemsToSync = pcLibrary.filter(f => f.checked);

    if (!selectedDevicePath || itemsToSync.length === 0) {
        alert("Please select a device and at least one song to sync.");
        return;
    }

    showStatus(true, "Syncing...", `Copying ${itemsToSync.length} files...`);

    // Create 'Music' folder
    const targetDir = path.join(selectedDevicePath, 'Music');
    if (!fs.existsSync(targetDir)) {
        try { fs.mkdirSync(targetDir); } catch (e) { }
    }

    let copiedCount = 0;
    for (const file of itemsToSync) {
        const destPath = path.join(targetDir, file.name);
        try {
            statusMessage.innerText = `Copying ${file.name}...`;
            if (!fs.existsSync(destPath)) { // Skip if exists? Or overwrite? Let's skip for speed if same size
                await fs.promises.copyFile(file.path, destPath);
            }
            copiedCount++;
        } catch (e) {
            console.error(`Copy failed: ${file.name} `, e);
        }
    }

    // Auto Rebuild DB after Sync
    await performDbGen();
});

// 6. Checkbox Logic
checkAll.addEventListener('change', (e) => {
    const checked = e.target.checked;
    // Update currently filtered view checkboxes
    const visibleFiles = filterLibrary(pcLibrary);
    visibleFiles.forEach(f => f.checked = checked);
    renderList(visibleFiles);
    checkSyncReady();
});


// Helpers
async function scanDevice() {
    showStatus(true, "Reading Device...", "Scanning files...");
    const files = await ipcRenderer.invoke('scan-library', selectedDevicePath);
    deviceLibrary = files;
    renderList(deviceLibrary, false);
    showStatus(false);
    lcdTitle.innerText = `${deviceLibrary.length} Songs on Device`;
}

function checkSyncReady() {
    // Sync enabled if device selected AND at least one file checked
    const hasChecked = pcLibrary.some(f => f.checked);
    btnSync.disabled = !(selectedDevicePath && hasChecked);
    btnRebuild.disabled = !selectedDevicePath;
}

function renderList(files, selectable = true) {
    fileList.innerHTML = '';
    files.forEach(file => {
        const tr = document.createElement('tr');

        // Checkbox Cell
        const tdCheck = document.createElement('td');
        tdCheck.style.textAlign = 'center';
        if (selectable) {
            const cb = document.createElement('input');
            cb.type = 'checkbox';
            cb.checked = file.checked !== false; // Default true
            cb.addEventListener('change', (e) => {
                file.checked = e.target.checked;
                checkSyncReady();
            });
            tdCheck.appendChild(cb);
        }
        tr.appendChild(tdCheck);

        // Icon
        const tdIcon = document.createElement('td');
        tdIcon.style.textAlign = 'center';
        tdIcon.innerHTML = '🎵';
        tr.appendChild(tdIcon);

        // Data
        tr.innerHTML += `
            <td>${file.name}</td>
            <td>${file.artist || 'Unknown'}</td>
            <td>${file.album || 'Unknown'}</td>
            <td>${(file.size / 1024 / 1024).toFixed(1)} MB</td>
        `;
        fileList.appendChild(tr);
    });
}

// Persistence
function loadLibrary() {
    if (fs.existsSync(LIBRARY_FILE)) {
        try {
            const data = fs.readFileSync(LIBRARY_FILE, 'utf-8');
            pcLibrary = JSON.parse(data);
            pcLibrary.forEach(f => { if (f.checked === undefined) f.checked = true; }); // Ensure checked state
            updateBrowser();
            renderList(pcLibrary);
            lcdTitle.innerText = `${pcLibrary.length} Songs`;
        } catch (e) {
            console.error("Failed to load library", e);
        }
    }
}

function saveLibrary() {
    try {
        fs.writeFileSync(LIBRARY_FILE, JSON.stringify(pcLibrary, null, 2));
    } catch (e) {
        console.error("Failed to save library", e);
    }
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
