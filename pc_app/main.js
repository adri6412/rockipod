const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const fs = require('fs');
const dbGenerator = require('./modules/db_generator');

const createWindow = () => {
    // Create the browser window.
    const mainWindow = new BrowserWindow({
        width: 1200,
        height: 800,
        webPreferences: {
            nodeIntegration: true,
            contextIsolation: false, // For simpler prototyping, better security uses preload
        },
        autoHideMenuBar: true,
        backgroundColor: '#1E1E1E',
        icon: path.join(__dirname, 'icon.png') // Optional
    });

    // Load the index.html of the app.
    mainWindow.loadFile(path.join(__dirname, 'renderer/index.html'));

    // Open the DevTools.
    // mainWindow.webContents.openDevTools();
};

app.on('ready', createWindow);

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    }
});

// IPC Handlers
ipcMain.handle('select-folder', async () => {
    const win = BrowserWindow.getFocusedWindow();
    const result = await dialog.showOpenDialog(win, {
        properties: ['openDirectory']
    });
    if (result.canceled) return null;
    return result.filePaths[0];
});

const mm = require('music-metadata');

// ...

ipcMain.handle('scan-library', async (event, folderPath) => {
    const files = [];

    // We need an async scan to use await mm.parseFile
    async function scan(dir) {
        try {
            const list = await fs.promises.readdir(dir);
            for (const file of list) {
                const fullPath = path.join(dir, file);
                const stat = await fs.promises.stat(fullPath);

                if (stat.isDirectory()) {
                    await scan(fullPath);
                } else {
                    const ext = path.extname(file).toLowerCase();
                    if (['.mp3', '.flac', '.ogg', '.wav', '.m4a'].includes(ext)) {
                        let meta = { artist: 'Unknown', album: 'Unknown', title: file };
                        try {
                            const tags = await mm.parseFile(fullPath, { skipCovers: true });
                            if (tags.common.artist) meta.artist = tags.common.artist;
                            if (tags.common.album) meta.album = tags.common.album;
                            if (tags.common.title) meta.title = tags.common.title;
                        } catch (err) {
                            // console.warn('Meta parse error on ' + file);
                        }

                        files.push({
                            name: file,
                            path: fullPath,
                            size: stat.size,
                            artist: meta.artist,
                            album: meta.album,
                            title: meta.title
                        });
                    }
                }
            }
        } catch (e) {
            console.error(e);
        }
    }

    await scan(folderPath);
    return files;
});

ipcMain.handle('generate-db', async (event, musicDir, outputDir) => {
    try {
        const result = await dbGenerator.createDatabase(musicDir, path.join(outputDir, 'database.rdb'));
        return { success: true, count: result.count };
    } catch (error) {
        return { success: false, error: error.message };
    }
});
