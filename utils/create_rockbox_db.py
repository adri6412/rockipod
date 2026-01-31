import os
import struct
import argparse
import sys

# Try to import mutagen for metadata parsing
try:
    import mutagen
    from mutagen.easyid3 import EasyID3
    from mutagen.flac import FLAC
    from mutagen.oggvorbis import OggVorbis
    from mutagen.mp4 import MP4
    from mutagen.wave import WAVE
    HAS_MUTAGEN = True
except ImportError:
    HAS_MUTAGEN = False
    print("Warning: 'mutagen' library not found. Falling back to filename parsing.")
    print("To install: pip install mutagen")

MAGIC = b"RDB1"

class DBEntry:
    def __init__(self, path, title, artist, album):
        self.path = path
        self.title = title
        self.artist = artist
        self.album = album
        
        # Indices into string pool (assigned later)
        self.path_idx = 0
        self.title_idx = 0
        self.artist_idx = 0
        self.album_idx = 0

def get_metadata(filepath):
    """Extract metadata using mutagen or fallback to filename."""
    title = os.path.basename(filepath)
    artist = "Unknown Artist"
    album = "Unknown Album"
    
    if HAS_MUTAGEN:
        try:
            f = mutagen.File(filepath, easy=True)
            if f:
                # EasyID3/EasyMP4/FLAC/OggVorbis standardizes tags
                if 'title' in f: title = f['title'][0]
                if 'artist' in f: artist = f['artist'][0]
                if 'album' in f: album = f['album'][0]
        except Exception as e:
            # print(f"Error parsing {filepath}: {e}")
            pass
            
    return title, artist, album

def create_database(music_dir, output_file):
    entries = []
    
    print(f"Scanning {music_dir}...")
    
    supported_exts = ('.mp3', '.flac', '.ogg', '.wav', '.m4a')
    
    for root, dirs, files in os.walk(music_dir):
        for file in files:
            if file.lower().endswith(supported_exts):
                full_path = os.path.join(root, file)
                # Make path relative to music_dir (or absolute depending on device structure)
                # Rockbox usually expects paths relative to root, e.g., "/Music/Song.mp3"
                # For now, we'll store the full path provided or relative if possible
                title, artist, album = get_metadata(full_path)
                
                # Rockbox expects paths relative to the root of the storage (e.g. /Music/Song.mp3)
                # We assume the user runs this on the mounted drive content.
                # using os.path.splitdrive(full_path)[1] gives us the path without the drive letter.
                drive, path_no_drive = os.path.splitdrive(full_path)
                rel_path = path_no_drive.replace("\\", "/")
                
                entries.append(DBEntry(rel_path, title, artist, album))

    print(f"Found {len(entries)} tracks.")
    
    # Sort entries by Artist, then Album, then Title
    entries.sort(key=lambda x: (x.artist.lower(), x.album.lower(), x.title.lower()))
    
    # Build String Pool
    string_pool = bytearray()
    string_map = {} # string -> offset
    
    def add_string(s):
        s = str(s)
        if s in string_map:
            return string_map[s]
        
        offset = len(string_pool)
        encoded = s.encode('utf-8') + b'\0'
        string_pool.extend(encoded)
        string_map[s] = offset
        return offset

    # Assign indices and build Artist/Album Index
    artist_index = [] # List of start_entry_indices
    album_index = []  # List of start_entry_indices for albums (global list order)
    
    current_artist_idx = -1
    current_album_idx = -1
    
    for i, entry in enumerate(entries):
        entry.path_idx = add_string(entry.path)
        entry.title_idx = add_string(entry.title)
        entry.artist_idx = add_string(entry.artist)
        entry.album_idx = add_string(entry.album)
        
        if entry.artist_idx != current_artist_idx:
            artist_index.append(i)
            current_artist_idx = entry.artist_idx
            
        # Note: Albums are sorted within artist. So unique albums are detected
        # when album_idx changes.
        if entry.album_idx != current_album_idx:
            album_index.append(i)
            current_album_idx = entry.album_idx

    print(f"Found {len(artist_index)} unique artists.")
    print(f"Found {len(album_index)} unique albums.")
        
    # Write Binary File
    # Header: Magic (4), EntryCount (4), ArtistCount (4), AlbumCount (4), 
    #         ArtistIndexOffset (4), AlbumIndexOffset (4), StringPoolOffset (4)
    # Entries: (16 bytes each)
    # Artist Index: (4 bytes each)
    # Album Index: (4 bytes each)
    # String Pool
    
    header_size = 4 + 4 + 4 + 4 + 4 + 4 + 4 # 28 bytes
    entries_total_size = len(entries) * 16
    artist_index_total_size = len(artist_index) * 4
    album_index_total_size = len(album_index) * 4
    
    artist_index_offset = header_size + entries_total_size
    album_index_offset = artist_index_offset + artist_index_total_size
    string_pool_offset = album_index_offset + album_index_total_size
    
    print(f"Writing {output_file}...")
    with open(output_file, 'wb') as f:
        # Header
        f.write(MAGIC)
        f.write(struct.pack('<I', len(entries)))
        f.write(struct.pack('<I', len(artist_index)))
        f.write(struct.pack('<I', len(album_index)))
        f.write(struct.pack('<I', artist_index_offset))
        f.write(struct.pack('<I', album_index_offset))
        f.write(struct.pack('<I', string_pool_offset))
        
        # Entries
        for entry in entries:
            f.write(struct.pack('<IIII', 
                entry.title_idx, 
                entry.artist_idx, 
                entry.album_idx, 
                entry.path_idx
            ))
            
        # Artist Index
        for start_idx in artist_index:
            f.write(struct.pack('<I', start_idx))

        # Album Index
        for start_idx in album_index:
            f.write(struct.pack('<I', start_idx))
            
        # String Pool
        f.write(string_pool)
        
    print(f"Done! Database size: {os.path.getsize(output_file)} bytes")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create Rockbox Custom Database')
    parser.add_argument('music_dir', help='Directory containing music files')
    parser.add_argument('output_file', help='Output database file (e.g. database.rdb)')
    
    args = parser.parse_args()
    
    create_database(args.music_dir, args.output_file)
