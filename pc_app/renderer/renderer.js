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
let currentLevel = -1; // -1 = Root, 0 = Artist/Album/Track List, 1 = Album List/Tracks, 2 = Tracks
let navigationPath = {
    mode: null, // 'artist', 'album', 'track'
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
const btnClear = document.getElementById('btn-clear');

const statusOverlay = document.getElementById('status-overlay');
const statusTitle = document.getElementById('status-title');
const statusMessage = document.getElementById('status-message');

const fileList = document.getElementById('file-list'); // TBody for tracks
const listWrapper = document.querySelector('.list-wrapper'); // Wrapper div
const tableHead = document.querySelector('.file-table thead'); // Table Header
const checkAll = document.getElementById('check-all');
const searchInput = document.getElementById('search-input');

// Navigation Controls
const btnBack = document.getElementById('btn-back');
const viewTitle = document.getElementById('view-title');

const lcdTitle = document.querySelector('.lcd-title');
const lcdSubtitle = document.querySelector('.lcd-subtitle');

// --- Event Listeners ---

// Search
if (searchInput) {
    searchInput.addEventListener('input', (e) => {
        const query = e.target.value.toLowerCase();
        if (query.length > 0) {
            // Switch to Track view for search results if not already deeply nested
            if (currentLevel === -1 || navigationPath.mode !== 'track') {
                currentLevel = 0;
                navigationPath.mode = 'track';
            }
            renderCurrentLevel();
        } else {
            renderCurrentLevel();
        }
    });
}

// Navigation
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

btnBack.addEventListener('click', () => {
    if (currentLevel > -1) {
        if (currentLevel === 0) {
            // Back to Root
            currentLevel = -1;
            navigationPath = { mode: null, artist: null, album: null };
        } else if (currentLevel === 1) {
            currentLevel--;
            if (navigationPath.mode === 'artist') navigationPath.artist = null;
            if (navigationPath.mode === 'album') { /* Back from Tracks(1) to Albums(0) - no extra clear needed */ }
            else if (navigationPath.mode === 'artist') {
                // Back from Albums(1) to Artists(0)
                navigationPath.album = null;
            }
        } else if (currentLevel === 2) {
            // Only possible in Artist mode: Tracks(2) -> Albums(1)
            currentLevel--;
            navigationPath.album = null;
        }
        renderCurrentLevel();
    }
});

btnAddFolder.addEventListener('click', async () => {
    const folder = await ipcRenderer.invoke('select-folder');
    if (folder) {
        showStatus(true, "Scanning...", "Reading library...");
        const files = await ipcRenderer.invoke('scan-library', folder);

        // Deduplicate
        const existingPaths = new Set(pcLibrary.map(f => f.path));
        const uniqueNewFiles = files.filter(f => !existingPaths.has(f.path));

        if (uniqueNewFiles.length > 0) {
            const newFilesWithCheck = uniqueNewFiles.map(f => ({ ...f, checked: true }));
            pcLibrary = [...pcLibrary, ...newFilesWithCheck];
            saveLibrary();
            renderCurrentLevel();
            checkSyncReady();
            lcdTitle.innerText = `${pcLibrary.length} Songs`;
            showStatus(true, "Done", `Added ${uniqueNewFiles.length} new tracks.`);
            setTimeout(() => showStatus(false), 2000);
        } else {
            showStatus(true, "Info", "No new tracks found (duplicates skipped).");
            setTimeout(() => showStatus(false), 2000);
        }
    }
});

btnRefresh.addEventListener('click', () => {
    renderCurrentLevel();
});

btnClear.addEventListener('click', () => {
    if (confirm("Are you sure you want to clear your entire library?")) {
        pcLibrary = [];
        navigationPath = { mode: null, artist: null, album: null };
        currentLevel = -1;
        saveLibrary();
        renderCurrentLevel();
        lcdTitle.innerText = "Library Cleared";
        checkSyncReady();
    }
});

btnRebuild.addEventListener('click', async () => {
    if (!selectedDevicePath) return;
    performDbGen();
});

btnSync.addEventListener('click', async () => {
    const itemsToSync = pcLibrary.filter(f => f.checked);

    if (!selectedDevicePath || itemsToSync.length === 0) {
        alert("Please select a device and at least one song to sync.");
        return;
    }

    showStatus(true, "Syncing...", `Copying ${itemsToSync.length} files...`);

    const musicRootDir = path.join(selectedDevicePath, 'Music');
    if (!fs.existsSync(musicRootDir)) {
        try { fs.mkdirSync(musicRootDir); } catch (e) { }
    }

    // Sanitize helper
    const sanitize = (name) => {
        return (name || "Unknown").replace(/[<>:"/\\|?*]/g, '_').trim();
    };

    let copiedCount = 0;
    for (const file of itemsToSync) {
        // Structure: Music / Artist / Album / Filename
        const artistDir = path.join(musicRootDir, sanitize(file.artist));
        const albumDir = path.join(artistDir, sanitize(file.album));

        if (copiedCount === 0) {
            alert(`DEBUG SYNC:\nFile: ${file.name}\nArtist metadata: '${file.artist}'\nTarget Folder: ${artistDir}\n\nIf Artist is 'Toto' here, the file tag is wrong.`);
        }

        try {
            if (!fs.existsSync(artistDir)) fs.mkdirSync(artistDir);
            if (!fs.existsSync(albumDir)) fs.mkdirSync(albumDir);

            const destPath = path.join(albumDir, file.name);

            statusMessage.innerText = `Copying ${file.name}...`;
            if (!fs.existsSync(destPath)) {
                await fs.promises.copyFile(file.path, destPath);
            }
            copiedCount++;
        } catch (e) {
            console.error(`Copy failed: ${file.name} `, e);
        }
    }

    await performDbGen();
});

if (checkAll) {
    checkAll.addEventListener('change', (e) => {
        const checked = e.target.checked;
        let visibleTracks = [];

        if (currentLevel === 2) {
            visibleTracks = pcLibrary.filter(f => {
                return (f.artist || "Unknown") === navigationPath.artist &&
                    (f.album || "Unknown") === navigationPath.album;
            });
        } else if (currentLevel === 1 && navigationPath.mode === 'album') {
            visibleTracks = pcLibrary.filter(f => (f.album || "Unknown") === navigationPath.album);
        } else if (currentLevel === 0 && navigationPath.mode === 'track') {
            visibleTracks = pcLibrary;
        }

        if (visibleTracks.length > 0) {
            visibleTracks.forEach(f => f.checked = checked);
            renderCurrentLevel();
        }
        checkSyncReady();
    });
}


// --- Functions ---

function setView(view) {
    currentView = view;

    // Reset Navigation to Root on view switch
    currentLevel = -1;
    navigationPath = { mode: null, artist: null, album: null };

    if (view === 'library') {
        navLibrary.classList.add('active');
        navDevice.classList.remove('active');

        btnAddFolder.style.display = 'inline-block';
        btnSync.style.display = 'inline-block';
        btnRefresh.style.display = 'inline-block';

        lcdTitle.innerText = `${pcLibrary.length} Songs`;
    } else {
        navDevice.classList.add('active');
        navLibrary.classList.remove('active');

        btnAddFolder.style.display = 'none';
        btnSync.style.display = 'none';
        btnRefresh.style.display = 'none';

        lcdTitle.innerText = (deviceLibrary ? deviceLibrary.length : 0) + " Songs on Device";
    }

    checkSyncReady();
    renderCurrentLevel();
}

function renderCurrentLevel() {
    let activeLib = currentView === 'library' ? pcLibrary : deviceLibrary;
    if (!activeLib) activeLib = [];

    // Search Filter
    if (searchInput && searchInput.value.length > 0) {
        const query = searchInput.value.toLowerCase();
        activeLib = activeLib.filter(f =>
            (f.title || f.name).toLowerCase().includes(query) ||
            (f.artist || "").toLowerCase().includes(query) ||
            (f.album || "").toLowerCase().includes(query)
        );
    }

    fileList.innerHTML = '';
    updateHeader();
    updateCheckAllVisibility();

    if (currentLevel === -1) {
        // LEVEL -1: ROOT MENU
        setTableMode('list');

        const menuItems = [
            { name: "Artists", icon: "👤", mode: 'artist' },
            { name: "Albums", icon: "💿", mode: 'album' },
            { name: "Tracks", icon: "🎵", mode: 'track' }
        ];

        menuItems.forEach(item => {
            const row = createListRow(item.icon + ' ' + item.name, () => {
                navigationPath.mode = item.mode;
                currentLevel = 0;
                renderCurrentLevel();
            });
            fileList.appendChild(row);
        });

    } else if (currentLevel === 0) {
        // LEVEL 0
        setTableMode('list');

        if (navigationPath.mode === 'artist') {
            // Show Artists
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

        } else if (navigationPath.mode === 'album') {
            // Show Albums (All)
            const albums = new Set();
            activeLib.forEach(f => albums.add(f.album || "Unknown"));
            const sortedAlbums = Array.from(albums).sort();

            sortedAlbums.forEach(album => {
                const row = createListRow('💿 ' + album, () => {
                    navigationPath.album = album;
                    currentLevel = 1;
                    renderCurrentLevel();
                });
                fileList.appendChild(row);
            });

        } else if (navigationPath.mode === 'track') {
            // Show All Tracks
            setTableMode('tracks');
            renderTracks(activeLib);
        }

    } else if (currentLevel === 1) {
        // LEVEL 1

        if (navigationPath.mode === 'artist') {
            // Selected Artist -> Show Albums
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

        } else if (navigationPath.mode === 'album') {
            // Selected Album -> Show Tracks
            setTableMode('tracks');
            const tracks = activeLib.filter(f => (f.album || "Unknown") === navigationPath.album);
            renderTracks(tracks);
        }

    } else if (currentLevel === 2) {
        // LEVEL 2 (Only for Artist Mode)
        // Selected Artist -> Selected Album -> Show Tracks
        setTableMode('tracks');
        const tracks = activeLib.filter(f => {
            if ((f.artist || "Unknown") !== navigationPath.artist) return false;
            if ((f.album || "Unknown") !== navigationPath.album) return false;
            return true;
        });
        renderTracks(tracks);
    }
}

function renderTracks(tracks) {
    const isDeviceView = currentView === 'device';
    // Update Select All Checkbox State based on visible tracks
    if (checkAll && !isDeviceView && tracks.length > 0) {
        const allChecked = tracks.every(t => t.checked);
        checkAll.checked = allChecked;
        // Ensure it's visible
        checkAll.style.visibility = 'visible';
    } else if (checkAll) {
        checkAll.checked = false;
    }

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
                // Update header checkbox immediately
                if (checkAll) {
                    checkAll.checked = tracks.every(t => t.checked);
                }
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

function updateHeader() {
    const baseTitle = currentView === 'library' ? "Library" : "Device";
    btnBack.classList.remove('hidden');

    if (currentLevel === -1) {
        viewTitle.innerText = baseTitle;
        btnBack.classList.add('hidden');
    } else if (currentLevel === 0) {
        if (navigationPath.mode === 'artist') viewTitle.innerText = "Artists";
        else if (navigationPath.mode === 'album') viewTitle.innerText = "Albums";
        else viewTitle.innerText = "All Tracks";
    } else if (currentLevel === 1) {
        if (navigationPath.mode === 'artist') viewTitle.innerText = navigationPath.artist;
        else viewTitle.innerText = navigationPath.album;
    } else {
        viewTitle.innerText = `${navigationPath.artist} - ${navigationPath.album}`;
    }
}

function updateCheckAllVisibility() {
    if (!checkAll) return;
    let shouldShow = false;
    if (currentLevel === 2) shouldShow = true;
    else if (currentLevel === 1 && navigationPath.mode === 'album') shouldShow = true;
    else if (currentLevel === 0 && navigationPath.mode === 'track') shouldShow = true;

    if (currentView === 'device') shouldShow = false;

    checkAll.style.visibility = shouldShow ? 'visible' : 'hidden';
}

function setTableMode(mode) {
    const textHeaders = tableHead.querySelectorAll('th');
    if (mode === 'list') {
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

    const td1 = document.createElement('td');
    tr.appendChild(td1);

    const td2 = document.createElement('td');
    td2.style.textAlign = 'center';
    td2.innerHTML = '👉';
    tr.appendChild(td2);

    const td3 = document.createElement('td');
    td3.innerText = text;
    td3.style.fontWeight = '500';
    tr.appendChild(td3);

    return tr;
}

function checkSyncReady() {
    const hasChecked = pcLibrary.some(f => f.checked);
    btnSync.disabled = !hasChecked;
    btnRebuild.disabled = !selectedDevicePath;
}

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

function loadLibrary() {
    if (fs.existsSync(LIBRARY_FILE)) {
        try {
            const data = fs.readFileSync(LIBRARY_FILE, 'utf-8');
            pcLibrary = JSON.parse(data);
            pcLibrary.forEach(f => { if (f.checked === undefined) f.checked = true; });
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


// --- Init Exec ---
try {
    loadLibrary();
    // Force initial view render
    setView('library');
} catch (e) {
    alert("CRITICAL INIT ERROR: " + e.toString());
}
