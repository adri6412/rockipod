const fs = require('fs');
const path = require('path');
const mm = require('music-metadata');

const MAGIC = "RDB1";
const SUPPORTED_EXTS = ['.mp3', '.flac', '.ogg', '.wav', '.m4a'];

// Helper class for DB Entry
class DBEntry {
    constructor(relPath, title, artist, album) {
        this.relPath = relPath;
        this.title = title || path.basename(relPath);
        this.artist = artist || "Unknown Artist";
        this.album = album || "Unknown Album";
        this.artist = artist || "Unknown Artist";
        this.album = album || "Unknown Album";

        this.path_idx = 0;
        this.title_idx = 0;
        this.artist_idx = 0;
        this.album_idx = 0;
    }
}

async function getMetadata(filePath) {
    try {
        const metadata = await mm.parseFile(filePath, { skipCovers: true });
        return {
            title: metadata.common.title,
            artist: metadata.common.artist,
            album: metadata.common.album
        };
    } catch (error) {
        // console.warn(`Error parsing ${filePath}:`, error.message);
        return { title: null, artist: null, album: null };
    }
}

async function createDatabase(musicDir, outputFile) {
    const entries = [];

    // Recursive scan
    async function scan(dir) {
        const list = await fs.promises.readdir(dir);
        for (const file of list) {
            const fullPath = path.join(dir, file);
            const stat = await fs.promises.stat(fullPath);

            if (stat.isDirectory()) {
                await scan(fullPath);
            } else {
                const ext = path.extname(file).toLowerCase();
                if (SUPPORTED_EXTS.includes(ext)) {
                    // Metadata
                    const meta = await getMetadata(fullPath);

                    // Relative Path (Unix style for Rockbox)
                    // We assume musicDir is the root of the device or sync folder
                    // If scanning a PC folder to sync to a device, we might need adjustments.
                    // But usually for generation we assume the file IS on the device.
                    // For this tool, if we 'Sync', we copy then generate. 
                    // Let's assume musicDir points to the destination folder ON DEVICE.

                    /* Rockbox expects paths relative to root, e.g. /Music/Song.mp3 
                       But 'musicDir' might be 'E:\Music'. 
                       We need relative path from drive root OR musicDir?
                       The Python script did: os.path.splitdrive(fullPath)[1] -> /Music/Song.mp3
                       So it assumes fullPath includes the mount point structure.
                    */

                    const drive = path.parse(fullPath).root;
                    let relPath = fullPath.substring(drive.length); // Remove 'E:\'
                    relPath = relPath.replace(/\\/g, '/'); // Force forward slashes
                    if (!relPath.startsWith('/')) relPath = '/' + relPath;

                    entries.push(new DBEntry(relPath, meta.title, meta.artist, meta.album));
                }
            }
        }
    }

    await scan(musicDir);

    // Sort: Artist -> Album -> Title
    entries.sort((a, b) => {
        const artCmp = (a.artist || "").toLowerCase().localeCompare((b.artist || "").toLowerCase());
        if (artCmp !== 0) return artCmp;

        const albCmp = (a.album || "").toLowerCase().localeCompare((b.album || "").toLowerCase());
        if (albCmp !== 0) return albCmp;

        return (a.title || "").toLowerCase().localeCompare((b.title || "").toLowerCase());
    });

    // String Pool
    const stringPool = [];
    let currentPoolSize = 0;
    const stringMap = new Map();

    function addString(s) {
        if (!s) s = "";
        if (stringMap.has(s)) return stringMap.get(s);

        const offset = currentPoolSize;
        const buffer = Buffer.from(s + '\0', 'utf-8'); // Null terminated
        stringPool.push(buffer);
        currentPoolSize += buffer.length;

        stringMap.set(s, offset);
        return offset;
    }

    // Build Indices
    const artistIndex = [];
    const albumIndex = [];

    let currentArtistIdx = -1;
    let currentAlbumIdx = -1;

    entries.forEach((entry, i) => {
        entry.path_idx = addString(entry.relPath);
        entry.title_idx = addString(entry.title);
        entry.artist_idx = addString(entry.artist);
        entry.album_idx = addString(entry.album);

        if (entry.artist_idx !== currentArtistIdx) {
            artistIndex.push(i);
            currentArtistIdx = entry.artist_idx;
        }

        if (entry.album_idx !== currentAlbumIdx) {
            albumIndex.push(i);
            currentAlbumIdx = entry.album_idx;
        }
    });

    // Create Binary Buffer
    // Header (28 bytes) + Entries (16 * N) + ArtistIdx (4 * N) + AlbumIdx (4 * N) + Pool

    const headerSize = 28;
    const entriesSize = entries.length * 16;
    const artistIndexSize = artistIndex.length * 4;
    const albumIndexSize = albumIndex.length * 4;

    const artistIndexOffset = headerSize + entriesSize;
    const albumIndexOffset = artistIndexOffset + artistIndexSize;
    const stringPoolOffset = albumIndexOffset + albumIndexSize;

    const finalSize = stringPoolOffset + currentPoolSize;

    const buf = Buffer.alloc(finalSize);
    let offset = 0;

    // 1. Header
    offset += buf.write(MAGIC, offset); // 4 bytes
    offset = buf.writeUInt32LE(entries.length, offset);
    offset = buf.writeUInt32LE(artistIndex.length, offset);
    offset = buf.writeUInt32LE(albumIndex.length, offset);
    offset = buf.writeUInt32LE(artistIndexOffset, offset);
    offset = buf.writeUInt32LE(albumIndexOffset, offset);
    offset = buf.writeUInt32LE(stringPoolOffset, offset);

    // 2. Entries
    entries.forEach(entry => {
        offset = buf.writeUInt32LE(entry.title_idx, offset);
        offset = buf.writeUInt32LE(entry.artist_idx, offset);
        offset = buf.writeUInt32LE(entry.album_idx, offset);
        offset = buf.writeUInt32LE(entry.path_idx, offset);
    });

    // 3. Artist Index
    artistIndex.forEach(idx => {
        offset = buf.writeUInt32LE(idx, offset);
    });

    // 4. Album Index
    albumIndex.forEach(idx => {
        offset = buf.writeUInt32LE(idx, offset);
    });

    // 5. String Pool
    const poolBuf = Buffer.concat(stringPool);
    poolBuf.copy(buf, stringPoolOffset);

    await fs.promises.writeFile(outputFile, buf);

    return { count: entries.length };
}

module.exports = { createDatabase };
