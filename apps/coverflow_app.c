#include "audio.h"
#include "button.h"
#include "config.h"
#include "core_alloc.h"
#include "dir.h"
#include "file.h"
#include "gui/statusbar.h"
#include "kernel.h"
#include "lang.h"
#include "lcd.h"
#include "metadata.h"
#include "pathfuncs.h"
#include "playlist.h"
#include "powermgmt.h"
#include "recorder/bmp.h"
#include "recorder/jpeg_load.h" /* For read_jpeg_file */
#include "settings.h"
#include "splash.h" /* For splash */
#include "stdio.h"
#include "string-extra.h" /* For strlcpy */
#include "string.h"
#include "system.h"

/* Internal Coverflow App Port */
#include "gui/viewport.h"

#define STATUSBAR_HEIGHT 20
#define CONTENT_Y_OFFSET 18 /* Space below statusbar for text/art */

#ifndef LCD_WIDTH
#define LCD_WIDTH 240
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 320
#endif

// Coverflow Geometry (iPod Style with 3D perspective)
#define CENTER_WIDTH 160
#define CENTER_HEIGHT 160
#define SIDE_WIDTH 80 // Wider for 3D
#define SIDE_HEIGHT 140
#define REFLECTION_HEIGHT 40
#define Y_OFFSET 12 /* Centered in content viewport */

// Spacing
#define GAP_OFFSET 70
#define STACK_OFFSET 35

#define MAX_ALBUMS 300 /* Stable limit */

/* Album structure */
struct Album {
  char path[MAX_PATH];
  char name[96];       /* Balanced name length */
  char cover_file[32]; /* Reduced */
  bool is_jpeg;
  bool has_art;
  struct bitmap cover_bmp;
  size_t cache_offset; /* Relative to cache_base */
  bool loaded;
};

static struct Album *albums = NULL; /* Now dynamic */
static int *slot_owners = NULL;     /* Tracks which album is in which slot */
static int album_count = 0;
static int current_index = 0;
static float anim_pos = 0.0f;

#define NUM_ART_SLOTS 16
#define ALBUM_CACHE_SIZE (2048 * 1024) /* 2MB Cache */
#define SCRATCH_SIZE (512 * 1024)      /* 512KB Scratch for JPEGs */
#define ALBUM_STRUCTS_SIZE (MAX_ALBUMS * sizeof(struct Album))
#define SLOT_OWNERS_SIZE (NUM_ART_SLOTS * sizeof(int))

static int coverflow_mem_handle = -1;
static unsigned char *scratch_ptr = NULL;

/* ... headers ... */

static enum AppState { STATE_BROWSE, STATE_MENU } current_state;

/* Helper functions */
static void simple_basename(const char *path, char *dest) {
  const char *slash = strrchr(path, '/');
  if (slash)
    strcpy(dest, slash + 1);
  else
    strcpy(dest, path);
}

/* check_file unused for known filetypes, removed */

static bool is_music_file(const char *name) {
  const char *ext = strrchr(name, '.');
  if (!ext)
    return false;
  if (strcasecmp(ext, ".mp3") == 0)
    return true;
  if (strcasecmp(ext, ".flac") == 0)
    return true;
  if (strcasecmp(ext, ".ogg") == 0)
    return true;
  if (strcasecmp(ext, ".m4a") == 0)
    return true;
  if (strcasecmp(ext, ".wav") == 0)
    return true;
  if (strcasecmp(ext, ".wma") == 0)
    return true;
  if (strcasecmp(ext, ".ape") == 0)
    return true;
  return false;
}

static bool is_cover_file(const char *name) {
  const char *ext = strrchr(name, '.');
  if (!ext)
    return false;
  bool is_img = (strcasecmp(ext, ".jpg") == 0 ||
                 strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".bmp") == 0);
  if (!is_img)
    return false;
  if (strcasecmp(name, "cover.jpg") == 0)
    return true;
  if (strcasecmp(name, "cover.jpeg") == 0)
    return true;
  if (strcasecmp(name, "cover.bmp") == 0)
    return true;
  if (strcasecmp(name, "folder.jpg") == 0)
    return true;
  if (strcasecmp(name, "folder.jpeg") == 0)
    return true;
  if (strcasecmp(name, "front.jpg") == 0)
    return true;
  if (strcasecmp(name, "front.jpeg") == 0)
    return true;
  return false;
}

bool is_disc_folder(const char *name) {
  if (strncasecmp(name, "CD", 2) == 0)
    return true;
  if (strncasecmp(name, "Disc", 4) == 0)
    return true;
  if (strncasecmp(name, "Disk", 4) == 0)
    return true;
  return false;
}

void get_parent_name(const char *path, char *dest) {
  const char *last = strrchr(path, '/');
  if (!last) {
    strcpy(dest, "Unknown");
    return;
  }
  char tmp[MAX_PATH];
  strlcpy(tmp, path, last - path + 1);
  simple_basename(tmp, dest);
}

void scan_recursive(const char *path, int depth) {
  if (depth > 5 || album_count >= MAX_ALBUMS)
    return;

  DIR *dir = opendir(path);
  if (!dir)
    return;

  bool found_music = false;
  char found_cover[64];
  found_cover[0] = '\0';

  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.')
      continue;
    struct dirinfo info = dir_get_info(dir, entry);

    if (!(info.attribute & ATTR_DIRECTORY)) {
      if (is_music_file(entry->d_name))
        found_music = true;
      if (found_cover[0] == '\0' && is_cover_file(entry->d_name)) {
        strcpy(found_cover, entry->d_name);
      }
    }
  }

  if (depth > 0 && found_music) {
    strcpy(albums[album_count].path, path);
    char base_name[MAX_PATH];
    simple_basename(path, base_name);

    if (strcasecmp(base_name, "Musica Flac") == 0)
      goto skip_album_add;

    if (is_disc_folder(base_name)) {
      char parent_name[MAX_PATH];
      get_parent_name(path, parent_name);
      snprintf(albums[album_count].name, sizeof(albums[album_count].name),
               "%s (%s)", parent_name, base_name);
    } else {
      strlcpy(albums[album_count].name, base_name,
              sizeof(albums[album_count].name));
    }

    if (found_cover[0] != '\0') {
      strlcpy(albums[album_count].cover_file, found_cover,
              sizeof(albums[album_count].cover_file));
      albums[album_count].has_art = true;
      char *ext = strrchr(found_cover, '.');
      albums[album_count].is_jpeg = (ext && (strcasecmp(ext, ".jpg") == 0 ||
                                             strcasecmp(ext, ".jpeg") == 0));
    } else {
      /* Parent Cover Check logic omitted for brevity/RAM saving in native/fast
         implementation Use default cover logic instead if needed, or re-add if
         user demands it. Keeping it simple: No art found in folder = No art.
      */
      albums[album_count].has_art = false;
    }

    albums[album_count].loaded = false;
    memset(&albums[album_count].cover_bmp, 0, sizeof(struct bitmap));
    album_count++;

    if (album_count % 10 == 0) {
      char msg[32];
      snprintf(msg, sizeof(msg), "Found %d...", album_count);
      splash(0, msg);
      lcd_update();
    }

  skip_album_add:;
  }
  closedir(dir);

  /* Pass 2: Recurse */
  dir = opendir(path);
  if (dir) {
    while ((entry = readdir(dir)) && album_count < MAX_ALBUMS) {
      if (entry->d_name[0] == '.')
        continue;
      if (strcasecmp(entry->d_name, "System Volume Information") == 0)
        continue;
      if (strcasecmp(entry->d_name, ".rockbox") == 0)
        continue;

      struct dirinfo info = dir_get_info(dir, entry);
      if (info.attribute & ATTR_DIRECTORY) {
        char next_path[MAX_PATH];
        snprintf(next_path, sizeof(next_path), "%s/%s", path, entry->d_name);
        scan_recursive(next_path, depth + 1);
      }
    }
    closedir(dir);
  }
}

void load_cover_native(int index) {
  if (index < 0 || index >= album_count || albums[index].loaded)
    return;

  if (!albums[index].has_art) {
    albums[index].loaded = true;
    albums[index].cover_bmp.width = 0;
    return;
  }

  /* Resolve base addresses */
  unsigned char *mem_base =
      (unsigned char *)core_get_data(coverflow_mem_handle);
  unsigned char *cache_base = mem_base + ALBUM_STRUCTS_SIZE + SLOT_OWNERS_SIZE;

  size_t slot_size = ALBUM_CACHE_SIZE / NUM_ART_SLOTS;
  slot_size &= ~31;
  int slot = index % NUM_ART_SLOTS;

  /* If this slot was owned by another album, mark that album as NOT loaded */
  if (slot_owners[slot] != -1 && slot_owners[slot] < album_count) {
    albums[slot_owners[slot]].loaded = false;
  }
  slot_owners[slot] = index;

  char path[MAX_PATH];
  snprintf(path, sizeof(path), "%s/%s", albums[index].path,
           albums[index].cover_file);

  albums[index].cache_offset = (slot * slot_size);
  struct bitmap load_bm;
  load_bm.width = 200;
  load_bm.height = 200;
  load_bm.data = cache_base + albums[index].cache_offset;

  int flags = FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT;
  int result;

  if (albums[index].is_jpeg) {
    result = read_jpeg_file(path, &load_bm, slot_size, flags, NULL);
  } else {
    result = read_bmp_file(path, &load_bm, slot_size, flags, NULL);
  }

  if (result > 0) {
    albums[index].cover_bmp = load_bm;
    /* Store slot-relative data pointer for structural integrity */
    albums[index].cover_bmp.data = (void *)albums[index].cache_offset;
  } else {
    albums[index].cover_bmp.width = 0;
  }
  albums[index].loaded = true;
}

void scale_bitmap_3d(const struct bitmap *src, struct bitmap *dst, int w, int h,
                     bool apply_perspective) {
  if (!src->data || src->width <= 0)
    return;
  dst->width = w;
  dst->height = h;
  unsigned short *s_data = (unsigned short *)src->data;
  unsigned short *d_data = (unsigned short *)dst->data;
  int x_ratio = (int)((src->width << 16) / w) + 1;
  int y_ratio = (int)((src->height << 16) / h) + 1;

  for (int y = 0; y < h; y++) {
    int sy = (y * y_ratio) >> 16;
    int margin = 0;

    if (apply_perspective) {
      int center_y = h / 2;
      int dist = (y < center_y) ? (center_y - y) : (y - center_y);
      margin = (dist * w) / (h * 3);
    }

    for (int x = 0; x < w; x++) {
      if (apply_perspective && (x < margin || x > (w - margin))) {
        d_data[y * w + x] = lcd_get_background();
        continue;
      }

      int sx;
      if (apply_perspective) {
        int effective_line_w = w - 2 * margin;
        if (effective_line_w <= 0)
          effective_line_w = 1;
        sx = ((x - margin) * src->width) / effective_line_w;
      } else {
        sx = (x * x_ratio) >> 16;
      }

      if (sx < 0)
        sx = 0;
      if (sx >= src->width)
        sx = src->width - 1;
      d_data[y * w + x] = s_data[sy * src->width + sx];
    }
  }
}

void render_album(int index, int x, int y, int w, int h, bool use_3d) {
  load_cover_native(index);
  struct Album *alb = &albums[index];

  if (!alb->loaded || alb->cover_bmp.width == 0) {
    lcd_drawrect(x, y, w, h);
    int cx = x + w / 2, cy = y + h / 2;
    int s = w / 5;
    lcd_drawline(cx - s, cy - s, cx - s, cy + s);
    lcd_drawline(cx - s, cy - s, cx + s, cy);
    lcd_drawline(cx - s, cy + s, cx + s, cy);
    return;
  }

  /* Resolve dynamic pointer: Base + StructsArea + SlotsArea +
   * AlbumOffsetInCache
   */
  unsigned char *mem_base =
      (unsigned char *)core_get_data(coverflow_mem_handle);
  unsigned char *cache_base = mem_base + ALBUM_STRUCTS_SIZE + SLOT_OWNERS_SIZE;
  struct bitmap real_bm = alb->cover_bmp;
  real_bm.data = cache_base + alb->cache_offset;

  struct bitmap scaled_bm;
  scaled_bm.data = scratch_ptr;
  scale_bitmap_3d(&real_bm, &scaled_bm, w, h, use_3d);
  lcd_bitmap((fb_data *)scaled_bm.data, x, y, w, h);
}

void draw_coverflow_frame(void) {
  lcd_set_foreground(LCD_BLACK);

  int center_idx = (int)(anim_pos + 0.5f);
  int range = 5;

  /* Right Side */
  for (int i = range; i >= 1; i--) {
    int idx = center_idx + i;
    if (idx >= album_count)
      continue;
    float dist = (float)idx - anim_pos;
    int w, h, x, y;

    if (dist >= 1.0f) {
      w = SIDE_WIDTH;
      h = SIDE_HEIGHT;
      int stack_dist = (int)((dist - 1.0f) * STACK_OFFSET);
      x = (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET + stack_dist -
          (SIDE_WIDTH / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - SIDE_HEIGHT) / 2;
    } else {
      w = CENTER_WIDTH - (int)((CENTER_WIDTH - SIDE_WIDTH) * dist);
      h = CENTER_HEIGHT - (int)((CENTER_HEIGHT - SIDE_HEIGHT) * dist);
      int center_pos_x = LCD_WIDTH / 2;
      int side_pos_x = (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET;
      int cur_center_x =
          center_pos_x + (int)((side_pos_x - center_pos_x) * dist);
      x = cur_center_x - (w / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - h) / 2;
    }
    render_album(idx, x, y, w, h, true);
  }

  /* Left Side */
  for (int i = range; i >= 1; i--) {
    int idx = center_idx - i;
    if (idx < 0)
      continue;
    float dist = anim_pos - (float)idx;
    int w, h, x, y;
    if (dist >= 1.0f) {
      w = SIDE_WIDTH;
      h = SIDE_HEIGHT;
      int stack_dist = (int)((dist - 1.0f) * STACK_OFFSET);
      x = (LCD_WIDTH / 2) - (CENTER_WIDTH / 2) - GAP_OFFSET - stack_dist -
          (SIDE_WIDTH / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - SIDE_HEIGHT) / 2;
    } else {
      w = CENTER_WIDTH - (int)((CENTER_WIDTH - SIDE_WIDTH) * dist);
      h = CENTER_HEIGHT - (int)((CENTER_HEIGHT - SIDE_HEIGHT) * dist);
      int center_pos_x = LCD_WIDTH / 2;
      int side_pos_x = (LCD_WIDTH / 2) - (CENTER_WIDTH / 2) - GAP_OFFSET;
      int cur_center_x =
          center_pos_x + (int)((side_pos_x - center_pos_x) * dist);
      x = cur_center_x - (w / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - h) / 2;
    }
    render_album(idx, x, y, w, h, true);
  }

  /* Center */
  if (center_idx >= 0 && center_idx < album_count) {
    float dist = anim_pos - (float)center_idx;
    float abs_dist = dist > 0 ? dist : -dist;
    int w = CENTER_WIDTH - (int)((CENTER_WIDTH - SIDE_WIDTH) * abs_dist);
    int h = CENTER_HEIGHT - (int)((CENTER_HEIGHT - SIDE_HEIGHT) * abs_dist);
    int center_pos_x = LCD_WIDTH / 2;
    int target_x = (dist > 0)
                       ? ((LCD_WIDTH / 2) - (CENTER_WIDTH / 2) - GAP_OFFSET)
                       : ((LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET);
    int cur_center_x =
        center_pos_x + (int)((target_x - center_pos_x) * abs_dist);
    int x = cur_center_x - (w / 2);
    int y = Y_OFFSET + (CENTER_HEIGHT - h) / 2;
    render_album(center_idx, x, y, w, h, false);

    if (abs_dist < 0.2f) {
      int tw, th;
      lcd_getstringsize(albums[center_idx].name, &tw, &th);
      lcd_putsxy((LCD_WIDTH - tw) / 2, Y_OFFSET + CENTER_HEIGHT + 10,
                 albums[center_idx].name);
    }
  }
}

bool coverflow_app(void) {
  /* 1. Reset everything to a known clean state on entry */
  lcd_set_viewport(NULL);
  lcd_set_background(LCD_WHITE);
  lcd_set_foreground(LCD_BLACK);
  lcd_clear_display();
  lcd_update();

  /* Allocate ALL App RAM Dynamically (Free on Start Music) */
  size_t total_required =
      ALBUM_STRUCTS_SIZE + SLOT_OWNERS_SIZE + ALBUM_CACHE_SIZE + SCRATCH_SIZE;
  coverflow_mem_handle = core_alloc(total_required);
  if (coverflow_mem_handle < 0) {
    splash(HZ * 2, "Error: No RAM for RockIpod!");
    return false;
  }
  core_pin(coverflow_mem_handle);
  unsigned char *mem_base =
      (unsigned char *)core_get_data(coverflow_mem_handle);
  memset(mem_base, 0, total_required); /* CRITICAL: Clear uninitialized RAM */

  albums = (struct Album *)mem_base;
  slot_owners = (int *)(mem_base + ALBUM_STRUCTS_SIZE);
  for (int i = 0; i < NUM_ART_SLOTS; i++)
    slot_owners[i] = -1;

  scratch_ptr =
      mem_base + ALBUM_STRUCTS_SIZE + SLOT_OWNERS_SIZE + ALBUM_CACHE_SIZE;

  splash(HZ, "Scanning Library...");

  album_count = 0;
  scan_recursive("/", 0);

  if (album_count == 0) {
    splash(HZ * 2, "No Albums Found");
    return false;
  }

  current_state = STATE_BROWSE;
  anim_pos = current_index;
  bool dirty = true;
  bool exit_app = false;
  bool start_playing = false;

  /* Statusbar Tracking */
  int last_min = -1;
  int last_batt = -1;
  int last_audio = -1;

  /* Viewport Setup */
  struct viewport status_vp, content_vp;
  viewport_set_defaults(&status_vp, SCREEN_MAIN);
  status_vp.height = STATUSBAR_HEIGHT;

  viewport_set_defaults(&content_vp, SCREEN_MAIN);
  content_vp.y = STATUSBAR_HEIGHT;
  content_vp.height = LCD_HEIGHT - STATUSBAR_HEIGHT;

  /* Initial Bar Draw */
  lcd_set_viewport(NULL);
  gui_statusbar_draw(&statusbars.statusbars[SCREEN_MAIN], true, &status_vp);

  while (!exit_app) {
    if (!dirty && current_state == STATE_BROWSE) {
      float target = (float)current_index;
      float diff = target - anim_pos;
      if (diff > 0.005f || diff < -0.005f) {
        dirty = true;
      } else {
        anim_pos = target;
      }
    }

    /* Check for Statusbar changes (Clock/Battery/Audio) manually to avoid
     * frame-by-frame flicker */
    struct tm *tm = get_time();
    int cur_min = tm->tm_min;
    int cur_batt = battery_level();
    int cur_audio = audio_status();

    if (cur_min != last_min || cur_batt != last_batt ||
        cur_audio != last_audio) {
      gui_statusbar_draw(&statusbars.statusbars[SCREEN_MAIN], true, &status_vp);
      last_min = cur_min;
      last_batt = cur_batt;
      last_audio = cur_audio;
    }

    if (dirty) {
      lcd_set_viewport(&content_vp);
      lcd_set_background(LCD_WHITE);
      lcd_clear_display();

      /* Animation step: Slide towards current_index */
      float target = (float)current_index;
      float diff = target - anim_pos;
      if (diff > 0.005f || diff < -0.005f) {
        anim_pos += diff * 0.2f;
        dirty = true;
      } else {
        anim_pos = target;
      }

      draw_coverflow_frame();

      /* Update only the content area to prevent vibration */
      lcd_update_viewport();
      dirty = false;
    }

    int button = button_get(false);
    if (button == BUTTON_NONE) {
      /* UNPIN while idle to allow system (skins/icons) to use RAM */
      core_unpin(coverflow_mem_handle);
      yield();
      core_pin(coverflow_mem_handle);

      /* Refresh global pointers after repin */
      unsigned char *re_base =
          (unsigned char *)core_get_data(coverflow_mem_handle);
      albums = (struct Album *)re_base;
      slot_owners = (int *)(re_base + ALBUM_STRUCTS_SIZE);
      scratch_ptr =
          re_base + ALBUM_STRUCTS_SIZE + SLOT_OWNERS_SIZE + ALBUM_CACHE_SIZE;
      continue;
    }

    /* Only set dirty if the button is one we care about */
    switch (button & ~(BUTTON_REPEAT)) {
    case BUTTON_LEFT:
    case BUTTON_RIGHT:
    case BUTTON_SELECT:
    case BUTTON_PLAY:
    case BUTTON_POWER:
#ifdef BUTTON_BACK
    case BUTTON_BACK:
#endif
#ifdef BUTTON_HOME
    case BUTTON_HOME:
#endif
      dirty = true;
      break;
    }

    switch (button) {
    case BUTTON_LEFT:
    case BUTTON_LEFT | BUTTON_REPEAT:
      if (current_index > 0)
        current_index--;
      else
        current_index = album_count - 1;
      break;
    case BUTTON_RIGHT:
    case BUTTON_RIGHT | BUTTON_REPEAT:
      if (current_index < album_count - 1)
        current_index++;
      else
        current_index = 0;
      break;
    case BUTTON_SELECT:
    case BUTTON_PLAY: {
      char path_to_play[MAX_PATH];
      strlcpy(path_to_play, albums[current_index].path, MAX_PATH);

      /* CRITICAL: Release ALL RAM (Structs + Cache) before Audio starts */
      if (coverflow_mem_handle >= 0) {
        core_unpin(coverflow_mem_handle);
        core_free(coverflow_mem_handle);
        coverflow_mem_handle = -1;
        albums = NULL;
      }

      /* Force System Compaction */
      struct viewport *vp = NULL;
      lcd_set_viewport(vp);
      
      audio_stop();
      sleep(HZ); /* 1s delay for system to settle and defrag */
      playlist_create(NULL, NULL);
      playlist_insert_directory(NULL, path_to_play, PLAYLIST_INSERT_LAST, false,
                                false);
      if (playlist_amount() > 0) {
        playlist_start(0, 0, 0);
        start_playing = true;
        exit_app = true;
      } else {
        splash(HZ, "Empty Playlist!");
        exit_app = true;
      }
    } break;
    case BUTTON_POWER:
#ifdef BUTTON_BACK
    case BUTTON_BACK:
#endif
#ifdef BUTTON_HOME
    case BUTTON_HOME:
#endif
      exit_app = true;
      break;
    }
  }

  /* CRITICAL: Full System Graphics Reset and screen sync before exit */
  lcd_set_viewport(NULL);
  lcd_set_background(LCD_BLACK);
  lcd_set_foreground(LCD_WHITE);
  lcd_set_drawmode(DRMODE_SOLID);
  lcd_clear_display();

  /* Force system status bar redraw to restore battery icon and other elements
   */
  gui_statusbar_draw(&statusbars.statusbars[SCREEN_MAIN], true, NULL);

  lcd_update();

  /* Release all memory if function exits through other ways */
  if (coverflow_mem_handle >= 0) {
    core_unpin(coverflow_mem_handle);
    core_free(coverflow_mem_handle);
    coverflow_mem_handle = -1;
    albums = NULL;
  }

  return start_playing;
}
