#ifndef _CUSTOM_DB_H
#define _CUSTOM_DB_H

#include <stdbool.h>
#include <stdint.h>

/* Database location */
#define CUSTOM_DB_PATH "/database.rdb"

#define DB_MAGIC "RDB1"

struct db_header {
  char magic[4];
  uint32_t entry_count;
  uint32_t artist_count;
  uint32_t album_count;
  uint32_t artist_index_offset;
  uint32_t album_index_offset;
  uint32_t string_pool_offset;
} __attribute__((packed));

struct db_entry {
  uint32_t title_idx;
  uint32_t artist_idx;
  uint32_t album_idx;
  uint32_t path_idx;
} __attribute__((packed));

/* API */
bool custom_db_init(void);
void custom_db_close(void);
int custom_db_get_entry_count(void);
int custom_db_get_artist_count(void);
int custom_db_get_album_count(void);
int custom_db_get_album_start_index(int album_idx);

/* Fills generic buffer with string */
const char *custom_db_get_string(uint32_t offset);

/* Fills entry struct */
bool custom_db_get_entry(int index, struct db_entry *out);

/* Get the start entry index for a given artist index */
int custom_db_get_artist_start_index(int artist_idx);

#endif
