const { ipcRenderer } = require('electron');
const path = require('path');
const fs = require('fs');

// Constants
const LIBRARY_FILE = path.join(__dirname, 'library.json');

// State
let selectedDevicePath = null;
let pcLibrary = [];
let deviceLibrary = [];
let currentView = 'library'; // 'library' or 'device'

// Drill-Down State
let currentLevel = 0; // 0=Artist List, 1=Album List, 2=Track List
let navigationPath = {
    artist: null,
    album: null
};

// Elements
const navLibrary = document.getElementById('nav-library');
const navDevice = document.getElementById('nav-device');
const btnAddFolder = document.getElementById('btn-add-folder');
const btnSync = document.getElementById('btn-sync');
const btnRebuild = document.getElementById('btn-rebuild-db');
const btnRefresh = document.getElementById('btn-refresh');

const fileList = document.getElementById('file-list'); // TBody for tracks
const listWrapper = document.querySelector('.list-wrapper'); // Wrapper div
const tableHead = document.querySelector('.file-table thead'); // Table Header

// Navigation Controls
const btnBack = document.getElementById('btn-back');
const viewTitle = document.getElementById('view-title');

const statusOverlay = document.getElementById('status-overlay');
const statusTitle = document.getElementById('status-title');
const statusMessage = document.getElementById('status-message');
const lcdTitle = document.querySelector('.lcd-title');
const lcdSubtitle = document.querySelector('.lcd-subtitle');

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
        scanDevice(); // Just refresh view
    }
    setView('device');
});

function setView(view) {
    currentView = view;

    // Reset Navigation on view switch
    currentLevel = 0;
    navigationPath = { artist: null, album: null };

    if (view === 'library') {
        navLibrary.classList.add('active');
        navDevice.classList.remove('active');

        // Show Controls
        btnAddFolder.style.display = 'inline-block';
        btnSync.style.display = 'inline-block';
        btnRefresh.style.display = 'inline-block';

        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    } else {
        navDevice.classList.add('active');
        navLibrary.classList.remove('active');

        // Hide/Change Controls
        btnAddFolder.style.display = 'none';
        btnSync.style.display = 'none';
        btnRefresh.style.display = 'none';

        lcdTitle.innerText = (deviceLibrary ? deviceLibrary.length : 0) + " Songs on Device";
    }

    checkSyncReady();
    renderCurrentLevel();
}

// 2. Navigation Logic (Drill Down)

btnBack.addEventListener('click', () => {
    if (currentLevel > 0) {
        currentLevel--;
        if (currentLevel === 0) navigationPath.artist = null;
        if (currentLevel === 1) navigationPath.album = null; // Going back to artists, or back to albums
        renderCurrentLevel();
    }
});

function renderCurrentLevel() {
    let activeLib = currentView === 'library' ? pcLibrary : deviceLibrary;
    if (!activeLib) activeLib = [];

    // Reset Table/List Content
    fileList.innerHTML = '';

    // Update Header
    updateHeader();

    if (currentLevel === 0) {
        // LEVEL 0: ARTISTS
        // We use the same table structure but just 'Name' column basically
        // Or we can dynamically hide columns. Let's hide unrelated columns for simplicity.
        setTableMode('list'); // Mode: simple list

        // Get Unique Artists
        const artists = new Set();
        activeLib.forEach(f => artists.add(f.artist || "Unknown"));
        const sortedArtists = Array.from(artists).sort();

        sortedArtists.forEach(artist => {
            const row = createListRow('👤 ' + artist, () => {
                navigationPath.artist = artist;
                currentLevel = 1;
                renderCurrentLevel();
            });
            fileList.appendChild(row);
        });

    } else if (currentLevel === 1) {
        // LEVEL 1: ALBUMS (Filtered by Artist)
        setTableMode('list');

        const albums = new Set();
        activeLib.forEach(f => {
            if ((f.artist || "Unknown") === navigationPath.artist) {
                albums.add(f.album || "Unknown");
            }
        });
        const sortedAlbums = Array.from(albums).sort();

        sortedAlbums.forEach(album => {
            const row = createListRow('💿 ' + album, () => {
                navigationPath.album = album;
                currentLevel = 2;
                renderCurrentLevel();
            });
            fileList.appendChild(row);
        });

    } else if (currentLevel === 2) {
        // LEVEL 2: TRACKS
        setTableMode('tracks');

        const tracks = activeLib.filter(f => {
            if ((f.artist || "Unknown") !== navigationPath.artist) return false;
            if ((f.album || "Unknown") !== navigationPath.album) return false;
            return true;
        });

        // Determine syncable state
        const isDeviceView = currentView === 'device';

        tracks.forEach(file => {
            const tr = document.createElement('tr');

            // Checkbox
            const tdCheck = document.createElement('td');
            tdCheck.style.textAlign = 'center';
            if (!isDeviceView) {
                const cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.checked = file.checked !== false;
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

            tr.innerHTML += `
                 <td>${file.name}</td>
                 <td>${file.artist || 'Unknown'}</td>
                 <td>${file.album || 'Unknown'}</td>
                 <td>${(file.size / 1024 / 1024).toFixed(1)} MB</td>
             `;

            fileList.appendChild(tr);
        });
    }
}

function updateHeader() {
    if (currentLevel === 0) {
        viewTitle.innerText = currentView === 'library' ? "Library (Artists)" : "Device (Artists)";
        btnBack.classList.add('hidden');
    } else if (currentLevel === 1) {
        viewTitle.innerText = navigationPath.artist;
        btnBack.classList.remove('hidden');
    } else {
        viewTitle.innerText = `${navigationPath.artist} - ${navigationPath.album}`;
        btnBack.classList.remove('hidden');
    }
}

function setTableMode(mode) {
    // Show/Hide headers based on mode
    const textHeaders = tableHead.querySelectorAll('th');
    // Headers: [Check, Icon, Name, Artist, Album, Size]
    // Indices: 0, 1, 2, 3, 4, 5

    if (mode === 'list') {
        // Only show Check(empty) and Name
        textHeaders[3].style.display = 'none'; // Artist
        textHeaders[4].style.display = 'none'; // Album
        textHeaders[5].style.display = 'none'; // Size
    } else {
        textHeaders[3].style.display = '';
        textHeaders[4].style.display = '';
        textHeaders[5].style.display = '';
    }
}

function createListRow(text, onClick) {
    const tr = document.createElement('tr');
    tr.style.cursor = 'pointer';
    tr.addEventListener('click', onClick);

    // Empty Checkbox
    const td1 = document.createElement('td');
    tr.appendChild(td1);

    // Icon (Arrow or Folder)
    const td2 = document.createElement('td');
    td2.style.textAlign = 'center';
    td2.innerHTML = '📂'; // Use folder for container levels
    tr.appendChild(td2);

    // Name
    const td3 = document.createElement('td');
    td3.innerText = text;
    td3.style.fontWeight = '500';
    tr.appendChild(td3);

    // Hover effect class handled by CSS (tr:hover)
    return tr;
}



// 3. Add Folder & Scan
btnAddFolder.addEventListener('click', async () => {
    const folder = await ipcRenderer.invoke('select-folder');
    if (folder) {
        showStatus(true, "Scanning...", "Reading library...");
        const files = await ipcRenderer.invoke('scan-library', folder);

        const newFiles = files.map(f => ({ ...f, checked: true }));
        pcLibrary = [...pcLibrary, ...newFiles];

        saveLibrary();
        renderCurrentLevel();
        showStatus(false);
        checkSyncReady();
        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    }
});

btnRefresh.addEventListener('click', () => {
    renderCurrentLevel();
});

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
    // Only items that are checked.
    // NOTE: In Drill Down mode, user might not see checked files if they are not in the current view level.
    // But checked state persists in `pcLibrary`.
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
            if (!fs.existsSync(destPath)) {
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

// Helpers
async function scanDevice() {
    showStatus(true, "Reading Device...", "Scanning files...");
    try {
        const files = await ipcRenderer.invoke('scan-library', selectedDevicePath);
        deviceLibrary = files;
    } catch (e) { deviceLibrary = []; }

    if (currentView === 'device') renderCurrentLevel();
    showStatus(false);
    lcdTitle.innerText = `${deviceLibrary.length} Songs on Device`;
}

function checkSyncReady() {
    const hasChecked = pcLibrary.some(f => f.checked);
    btnSync.disabled = !(selectedDevicePath && hasChecked);
    btnRebuild.disabled = !selectedDevicePath;
}

// Persistence
function loadLibrary() {
    if (fs.existsSync(LIBRARY_FILE)) {
        try {
            const data = fs.readFileSync(LIBRARY_FILE, 'utf-8');
            pcLibrary = JSON.parse(data);
            pcLibrary.forEach(f => { if (f.checked === undefined) f.checked = true; });
            renderCurrentLevel();
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
