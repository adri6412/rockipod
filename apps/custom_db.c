#include "custom_db.h"
#include "debug.h"
#include "file.h"
#include "system.h"
#include <string.h>

/* Global State */
static int db_fd = -1;
static struct db_header db_hdr;
static bool db_initialized = false;

/* String Buffer */
#define STR_BUF_SIZE 512
static char str_buf[STR_BUF_SIZE];

bool custom_db_init(void) {
  if (db_initialized && db_fd >= 0) {
    return true;
  }

  /* O_RDONLY must be defined in Rockbox system headers or via fcntl */
  db_fd = open(CUSTOM_DB_PATH, O_RDONLY);
  if (db_fd < 0) {
    DEBUGF("CustomDB: Failed to open %s\n", CUSTOM_DB_PATH);
    return false;
  }

  /* Read Header */
  if (read(db_fd, &db_hdr, sizeof(struct db_header)) !=
      sizeof(struct db_header)) {
    DEBUGF("CustomDB: Failed to read header\n");
    close(db_fd);
    db_fd = -1;
    return false;
  }

  /* Magic Check */
  if (memcmp(db_hdr.magic, DB_MAGIC, 4) != 0) {
    DEBUGF("CustomDB: Bad Magic\n");
    close(db_fd);
    db_fd = -1;
    return false;
  }

  db_initialized = true;
  return true;
}

void custom_db_close(void) {
  if (db_fd >= 0) {
    close(db_fd);
    db_fd = -1;
  }
  db_initialized = false;
}

int custom_db_get_entry_count(void) {
  if (!db_initialized)
    return 0;
  return db_hdr.entry_count;
}

int custom_db_get_artist_count(void) {
  if (!db_initialized)
    return 0;
  return db_hdr.artist_count;
}

bool custom_db_get_entry(int index, struct db_entry *out) {
  if (!db_initialized)
    return false;
  if (index < 0 || (uint32_t)index >= db_hdr.entry_count)
    return false;

  off_t offset = sizeof(struct db_header) + (index * sizeof(struct db_entry));

  if (lseek(db_fd, offset, SEEK_SET) < 0)
    return false;

  if (read(db_fd, out, sizeof(struct db_entry)) != sizeof(struct db_entry))
    return false;

  return true;
}

int custom_db_get_artist_start_index(int artist_idx) {
  if (!db_initialized)
    return -1;
  if (artist_idx < 0 || (uint32_t)artist_idx >= db_hdr.artist_count)
    return -1;

  off_t offset = db_hdr.artist_index_offset + (artist_idx * 4);

  if (lseek(db_fd, offset, SEEK_SET) < 0)
    return -1;

  uint32_t start_index;
  if (read(db_fd, &start_index, 4) != 4)
    return -1;

  return (int)start_index;
}

int custom_db_get_album_count(void) {
  if (!db_initialized)
    return 0;
  return db_hdr.album_count;
}

int custom_db_get_album_start_index(int album_idx) {
  if (!db_initialized)
    return -1;
  if (album_idx < 0 || (uint32_t)album_idx >= db_hdr.album_count)
    return -1;

  off_t offset = db_hdr.album_index_offset + (album_idx * 4);

  if (lseek(db_fd, offset, SEEK_SET) < 0)
    return -1;

  uint32_t start_index;
  if (read(db_fd, &start_index, 4) != 4)
    return -1;

  return (int)start_index;
}

const char *custom_db_get_string(uint32_t offset) {
  if (!db_initialized)
    return "<DB Error>";

  off_t abs_offset = db_hdr.string_pool_offset + offset;

  if (lseek(db_fd, abs_offset, SEEK_SET) < 0)
    return "<Seek Error>";

  int read_bytes = read(db_fd, str_buf, STR_BUF_SIZE - 1);
  if (read_bytes < 0)
    return "<Read Error>";

  str_buf[read_bytes] = '\0';

  /* Naive string reading: assuming it fits in buffer and null terminator is
   * present */
  /* If the string is longer, it will just get truncated by str_buf size */

  return str_buf;
}
