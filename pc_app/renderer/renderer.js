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
        document.getElementById('view-title').innerText = "Library";

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

    tr.innerHTML += `
            <td>${file.name}</td>
            <td>${file.artist || 'Unknown'}</td>
            <td>${file.album || 'Unknown'}</td>
            <td>${(file.size / 1024 / 1024).toFixed(1)} MB</td>
        `; fileList.appendChild(tr);
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

