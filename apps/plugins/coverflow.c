#include "plugin.h"

/* No PLUGIN_HEADER here */

#ifndef LCD_WIDTH
#define LCD_WIDTH 240
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 320
#endif

// Coverflow Geometry (iPod Style with 3D perspective)
#define CENTER_WIDTH 160
#define CENTER_HEIGHT 160
#define SIDE_WIDTH 80   // Wider for 3D perspective effect
#define SIDE_HEIGHT 140 // Slightly shorter
#define REFLECTION_HEIGHT 40
#define Y_OFFSET 40

// Spacing
#define GAP_OFFSET 70   // Reduced gap for 3D effect
#define STACK_OFFSET 35 // Increased spacing for depth

#define MAX_ALBUMS 500
#define MAX_DEPTH 3

/* Album structure */
struct Album {
  char path[MAX_PATH];
  char name[MAX_PATH];
  char cover_file[64];
  bool is_jpeg;
  bool has_art;
  struct bitmap cover_bmp;
  bool loaded;
};

struct Album albums[MAX_ALBUMS];
int album_count = 0;
// Current index acts as the target
int current_index = 0;
// Floating point position for animation
float anim_pos = 0.0f;

unsigned char *plugin_buffer = NULL;
size_t plugin_buffer_size = 0;

// Pointers to memory constraints
unsigned char *bitmap_storage_start = NULL;
unsigned char *scratch_buffer = NULL; // For scaling operations

struct bitmap *default_cover = NULL;

enum PluginState { STATE_BROWSE, STATE_MENU };

enum PluginState current_state = STATE_BROWSE;
int menu_selection = 0; // 0 = Play Album, 1 = Cancel

/* Helper for basename */
void simple_basename(const char *path, char *dest) {
  const char *slash = rb->strrchr(path, '/');
  if (slash)
    rb->strcpy(dest, slash + 1);
  else
    rb->strcpy(dest, path);
}

/* Check if a file exists (helper) */
bool check_file(const char *dir, const char *file) {
  char path[MAX_PATH];
  rb->snprintf(path, sizeof(path), "%s/%s", dir, file);
  int fd = rb->open(path, O_RDONLY);
  if (fd >= 0) {
    rb->close(fd);
    return true;
  }
  return false;
}

// Helper to identify music files
bool is_music_file(const char *name) {
  const char *ext = rb->strrchr(name, '.');
  if (!ext)
    return false;
  if (rb->strcasecmp(ext, ".mp3") == 0)
    return true;
  if (rb->strcasecmp(ext, ".flac") == 0)
    return true;
  if (rb->strcasecmp(ext, ".ogg") == 0)
    return true;
  if (rb->strcasecmp(ext, ".m4a") == 0)
    return true;
  if (rb->strcasecmp(ext, ".wav") == 0)
    return true;
  if (rb->strcasecmp(ext, ".wma") == 0)
    return true;
  if (rb->strcasecmp(ext, ".ape") == 0)
    return true;
  return false;
}

// Helper to identify cover art
bool is_cover_file(const char *name) {
  const char *ext = rb->strrchr(name, '.');
  if (!ext)
    return false;
  // Check extensions first
  bool is_img =
      (rb->strcasecmp(ext, ".jpg") == 0 || rb->strcasecmp(ext, ".jpeg") == 0 ||
       rb->strcasecmp(ext, ".bmp") == 0);
  if (!is_img)
    return false;

  // Check strict names
  if (rb->strcasecmp(name, "cover.jpg") == 0)
    return true;
  if (rb->strcasecmp(name, "cover.jpeg") == 0)
    return true;
  if (rb->strcasecmp(name, "cover.bmp") == 0)
    return true;
  if (rb->strcasecmp(name, "folder.jpg") == 0)
    return true;
  if (rb->strcasecmp(name, "folder.jpeg") == 0)
    return true;
  if (rb->strcasecmp(name, "front.jpg") == 0)
    return true;
  if (rb->strcasecmp(name, "front.jpeg") == 0)
    return true;

  return false;
}

/* Recursive scan */
// Helper to check if directory name suggests a multi-disc part
bool is_disc_folder(const char *name) {
  if (rb->strncasecmp(name, "CD", 2) == 0)
    return true;
  if (rb->strncasecmp(name, "Disc", 4) == 0)
    return true;
  if (rb->strncasecmp(name, "Disk", 4) == 0)
    return true;
  return false;
}

// Helper to get parent name
void get_parent_name(const char *path, char *dest) {
  // path is .../Parent/CD1
  // Find last slash
  const char *last = rb->strrchr(path, '/');
  if (!last) {
    rb->strcpy(dest, "Unknown");
    return;
  }

  // We need the slash before that
  // Create temp copy to truncate
  char tmp[MAX_PATH];
  rb->strlcpy(tmp, path, last - path + 1); // Copy up to just before last slash
  simple_basename(tmp, dest);
}

void scan_recursive(const char *path, int depth) {
  if (depth > 5 || album_count >= 1000)
    return;

  DIR *dir = rb->opendir(path);
  if (!dir)
    return;

  bool found_music = false;
  char found_cover[64];
  found_cover[0] = '\0';

  // Pass 1: Check files
  struct dirent *entry;
  while ((entry = rb->readdir(dir))) {
    if (entry->d_name[0] == '.')
      continue;
    struct dirinfo info = rb->dir_get_info(dir, entry);

    if (!(info.attribute & ATTR_DIRECTORY)) {
      if (is_music_file(entry->d_name))
        found_music = true;
      if (found_cover[0] == '\0' && is_cover_file(entry->d_name)) {
        rb->strcpy(found_cover, entry->d_name);
      }
    }
  }

  // Exclude Root (depth 0) regardless of music
  if (depth > 0 && found_music) {
    // Add Album
    rb->strcpy(albums[album_count].path, path);

    char base_name[MAX_PATH];
    simple_basename(path, base_name);

    // Explicit exclusion of "Musica Flac" root folder
    if (rb->strcasecmp(base_name, "Musica Flac") == 0) {
      // Skip adding this as album
      goto skip_album_add;
    }

    // Handle CD1/CD2 logic
    if (is_disc_folder(base_name)) {
      char parent_name[MAX_PATH];
      get_parent_name(path, parent_name);
      rb->snprintf(albums[album_count].name, MAX_PATH, "%s (%s)", parent_name,
                   base_name);
    } else {
      rb->strcpy(albums[album_count].name, base_name);
    }

    if (found_cover[0] != '\0') {
      rb->strcpy(albums[album_count].cover_file, found_cover);
      albums[album_count].has_art = true;
      char *ext = rb->strrchr(found_cover, '.');
      albums[album_count].is_jpeg =
          (ext && (rb->strcasecmp(ext, ".jpg") == 0 ||
                   rb->strcasecmp(ext, ".jpeg") == 0));
    } else {
      // Try Parent for cover?
      // Construct parent path check
      char parent_path[MAX_PATH];
      char tmp_path[MAX_PATH];
      rb->strcpy(tmp_path, path);
      char *last_slash = rb->strrchr(tmp_path, '/');
      if (last_slash) {
        *last_slash = '\0'; // Truncate to parent
        rb->strcpy(parent_path, tmp_path);

        // Quick check common names in parent
        const char *candidates[] = {"cover.jpg", "folder.jpg", "cover.bmp"};
        bool found_parent_cover = false;
        for (int i = 0; i < 3; i++) {
          if (check_file(parent_path, candidates[i])) {
            // Hack: Store relative path?
            rb->snprintf(albums[album_count].cover_file, 64, "../%s",
                         candidates[i]);

            char *ext = rb->strrchr(candidates[i], '.');
            albums[album_count].is_jpeg =
                (ext && (rb->strcasecmp(ext, ".jpg") == 0 ||
                         rb->strcasecmp(ext, ".jpeg") == 0));
            albums[album_count].has_art = true;
            found_parent_cover = true;
            break;
          }
        }
        if (!found_parent_cover) {
          albums[album_count].has_art = false;
        }
      } else {
        albums[album_count].has_art = false;
      }
    }

    albums[album_count].loaded = false;
    rb->memset(&albums[album_count].cover_bmp, 0, sizeof(struct bitmap));
    album_count++;

    if (album_count % 10 == 0) {
      rb->splashf(0, "Found %d...", album_count);
      rb->lcd_update();
    }

  skip_album_add:;
  }

  rb->closedir(dir);

  // Pass 2: Recurse
  dir = rb->opendir(path);
  if (dir) {
    while ((entry = rb->readdir(dir)) && album_count < 1000) {
      if (entry->d_name[0] == '.')
        continue;
      if (rb->strcasecmp(entry->d_name, "System Volume Information") == 0)
        continue;
      if (rb->strcasecmp(entry->d_name, ".rockbox") == 0)
        continue;

      struct dirinfo info = rb->dir_get_info(dir, entry);
      if (info.attribute & ATTR_DIRECTORY) {
        char next_path[MAX_PATH];
        rb->snprintf(next_path, sizeof(next_path), "%s/%s", path,
                     entry->d_name);
        scan_recursive(next_path, depth + 1);
      }
    }
    rb->closedir(dir);
  }
}
// Stub replacement corrected:
// Stub replacement corrected:
void load_cover(int index) {
  if (index < 0 || index >= album_count)
    return;
  if (albums[index].loaded)
    return;

  // Safety check for path
  if (!albums[index].has_art) {
    albums[index].loaded = true;
    albums[index].cover_bmp.width = 0;
    return;
  }

  char path[MAX_PATH];
  rb->snprintf(path, sizeof(path), "%s/%s", albums[index].path,
               albums[index].cover_file);

  // Dynamic slot sizing: Reserve ~50KB for scratch
  int reserved = 160 * 160 * 2;

  // Calculate remaining memory for slots
  size_t total_avail = plugin_buffer_size - reserved;

  // FORCE 20 SLOTS MINIMUM to cover visible range (11 items) + buffer
  // Typical Rockbox buffer is >2MB, so 2MB / 20 = 100KB per slot -> perfect for
  // resized covers
  int num_slots = 20;
  size_t slot_size = total_avail / num_slots;

  // Cap max useful size
  if (slot_size > 500 * 1024)
    slot_size = 500 * 1024; // 500KB cap

  // Min size check - if we drop below 100KB (needed for 200x200 BMP), we reduce
  // slots
  if (slot_size < 100 * 1024) {
    // Emergency fallback: Reduce slots to keep size valid
    slot_size = 100 * 1024;
    num_slots = total_avail / slot_size;
  }

  int slot = index % num_slots;

  albums[index].cover_bmp.data =
      (void *)(bitmap_storage_start + (slot * slot_size));
  albums[index].cover_bmp.width = 0; // Reset

  int result = 0;
  if (albums[index].is_jpeg) {
#ifdef HAVE_JPEG
    result = rb->read_jpeg_file(path, &albums[index].cover_bmp, slot_size,
                                FORMAT_NATIVE, NULL);
#endif
  } else {
    result = rb->read_bmp_file(path, &albums[index].cover_bmp, slot_size,
                               FORMAT_NATIVE, NULL);
  }

  if (result <= 0) {
    // Load failed
    albums[index].cover_bmp.width = 0;
  }
  albums[index].loaded = true;
}

// Software Scaler with optional 3D perspective
void scale_bitmap_3d(const struct bitmap *src, struct bitmap *dst, int w, int h,
                     bool apply_perspective) {
  if (!src->data || src->width <= 0)
    return;

  dst->width = w;
  dst->height = h;

  unsigned short *s_data = (unsigned short *)src->data;
  unsigned short *d_data = (unsigned short *)dst->data;

  /* Standard scaling ratios */
  int x_ratio = (int)((src->width << 16) / w) + 1;
  int y_ratio = (int)((src->height << 16) / h) + 1;

  for (int y = 0; y < h; y++) {
    int sy = (y * y_ratio) >> 16;

    /* 3D Perspective Calculation */
    int effective_w = w;
    int x_offset = 0;

    if (apply_perspective) {
      /* Create trapezoid effect:
         - Center Y lines are wider
         - Top/Bottom Y lines show perspective (narrower width, shifted)
      */
      int center_y = h / 2;
      int dist = (y < center_y) ? (center_y - y) : (y - center_y);

      /* Stronger perspective: Width reduces as we move away from y-center */
      // Reduction factor: 0 at center, max at edges
      int reduction = (dist * w) / (h * 1.5);

      /* Shift X start to center the reduced width line?
         Or shift to one side? For Coverflow we want symmetric vertical
         perspective but horizontal rotation. Actually, simply shearing
         (shifting X) is faster and looks "ok". Let's INCREASE the shear
         significantly.
      */

      // Shear: Shift pixels right based on Y distance from something?
      // Let's stick to the previous shear logic but make it MUCH stronger and
      // dependent on left/right position? Actually, just strong
      // vertical-dependent shear makes it look tilted.

      x_offset = (dist * w) / (h); // Strong shear!

      // Direction of shear depends on Y? No, top and bottom move opposite?
      // No, for rotation around Y axis, top and bottom move "in" (trapezoid).
      // Standard bitmap scaler can't easily do trapezoid (variable width per
      // line). But we can simulate it by sampling differently!

      /*
       To get a trapezoid:
       At y=0 (top), we want to sample a WIDE range of source pixels into a
       NARROW dst range. At y=h/2 (mid), normal sampling. At y=h (bot), same as
       top.
      */

      // Let's try variable sampling rate per line!
      // Top line: squeezes entire source width into smaller dst width?
      // No, dst width is fixed (w). We want to display less of the image?
      // No, we want to squeeze the image into the center of the line.

      // This is getting complex for software rendering.
      // Let's stick to simple SHEAR but stronger.
      // Shear moves the whole line.
      // If top moves right, bottom moves left -> Rotation around Z/center.
      // If top and bottom move "in" -> perspective.
    }

    for (int x = 0; x < w; x++) {
      int sx;

      if (apply_perspective) {
        /* Trapezoid simulation:
           We want to map dst x [0..w] to source x [0..src->width].
           But at edges (top/bot), we want to map a WIDER source range to the
           [0..w] dst range? No, that zooms in.

           We want the image to appear NARROWER at top/bottom.
           So we want to put transparency at x < margin and x > w-margin.
           And map the source image to [margin .. w-margin].
        */
        int center_y = h / 2;
        int dist = (y < center_y) ? (center_y - y) : (y - center_y);
        int margin = (dist * w) / (h * 3); // Max margin = w/3 at edges

        if (x < margin || x > (w - margin)) {
          d_data[y * w + x] = rb->lcd_get_background(); // Match background
          continue;
        }

        // Map x from [margin..w-margin] to [0..src->width]
        int effective_line_w = w - 2 * margin;
        if (effective_line_w <= 0)
          effective_line_w = 1;

        int x_in_line = x - margin;
        sx = (x_in_line * src->width) / effective_line_w;
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

// Keep simple scaler for backward compatibility
void scale_bitmap(const struct bitmap *src, struct bitmap *dst, int w, int h) {
  scale_bitmap_3d(src, dst, w, h, false);
}

// Reflection drawer
void draw_reflection(int x, int y_start, int w, int h,
                     const struct bitmap *bm) {
  if (!bm->data)
    return;

  for (int r = 0; r < h; r++) {
    if (r >= bm->height)
      break;
    int src_y = bm->height - 1 - r;
    if (r % 2 != 0)
      continue; // Simple skip for transparency effect

    rb->lcd_bitmap_part((fb_data *)bm->data, 0, src_y, bm->width, x,
                        y_start + r, w, 1);
  }
}

// Render a single album at position with specific size and optional 3D effect
void render_album_geometry_3d(int index, int x, int y, int w, int h,
                              bool use_3d) {
  load_cover(index);
  struct Album *alb = &albums[index];

  if (!alb->loaded || alb->cover_bmp.width == 0) {
    // Draw placeholder box
    rb->lcd_drawrect(x, y, w, h);

    // Draw "No Art" Triangle Icon (Play symbol style)
    // Center: cx, cy
    int cx = x + w / 2;
    int cy = y + h / 2;
    int s = w / 5; // Scale relative to width

    if (s > 2) {
      // Points:
      // Top Left: cx - s, cy - s
      // Bottom Left: cx - s, cy + s
      // Right Middle: cx + s, cy

      // Rockbox doesn't have filltriangle in core API easily accessible?
      // Use drawline.
      rb->lcd_drawline(cx - s, cy - s, cx - s, cy + s); // Vertical Left
      rb->lcd_drawline(cx - s, cy - s, cx + s, cy);     // Top Slope
      rb->lcd_drawline(cx - s, cy + s, cx + s, cy);     // Bottom Slope
    }
    return;
  }

  struct bitmap scaled_bm;
  scaled_bm.data = scratch_buffer;

  // Scale with optional 3D perspective for side covers
  scale_bitmap_3d(&alb->cover_bmp, &scaled_bm, w, h, use_3d);

  rb->lcd_bitmap((fb_data *)scaled_bm.data, x, y, w, h);
  draw_reflection(x, y + h, w, REFLECTION_HEIGHT, &scaled_bm);
}

// Wrapper for backward compatibility
void render_album_geometry(int index, int x, int y, int w, int h) {
  render_album_geometry_3d(index, x, y, w, h, false);
}

// Text scrolling
long scroll_tick = 0;

void draw_scrolling_text(int y, const char *text) {
  int w, h;
  rb->lcd_getstringsize(text, &w, &h);

  if (w <= LCD_WIDTH) {
    // Center static
    rb->lcd_putsxy((LCD_WIDTH - w) / 2, y, text);
  } else {
    // Helper to draw scrolling
    // Cycle: Scroll Left -> Pause -> Reset -> Pause
    int cycle_len = w - LCD_WIDTH + 40;
    if (cycle_len < 40)
      cycle_len = 40; // Safety

    // Slower scroll: divide tick by 4
    int t = (scroll_tick / 3) % (cycle_len + 40);

    int offset = 0;
    if (t < 20)
      offset = 0; // Pause start
    else if (t < 20 + cycle_len)
      offset = t - 20; // Scroll
    else
      offset = cycle_len; // Pause end

    rb->lcd_putsxy(10 - offset, y, text);
  }
}

void draw_frame(void) {
  scroll_tick++;
  rb->lcd_clear_display();

  int center_idx = (int)(anim_pos + 0.5f);

  // Calculate center geometry
  // We need to draw from outside (far) to inside (center) for correct overlap
  // (Painters Algo)

  int range = 5; // How many covers to draw on each side

  // Draw Right Side (iterating backwards from far right to near right)
  for (int i = range; i >= 1; i--) {
    int idx = center_idx + i;
    if (idx >= album_count)
      continue;

    float dist = (float)idx - anim_pos; // Positive value

    // Interpolation logic
    int w, h, x, y;

    if (dist >= 1.0f) {
      // Fully Side State
      w = SIDE_WIDTH;
      h = SIDE_HEIGHT;
      // x is calculated from center + gap + stack
      // gap is for the first item, stack thereafter
      // dist 1.0 -> CenterX + Gap
      // dist 2.0 -> CenterX + Gap + Stack
      int stack_dist = (int)((dist - 1.0f) * STACK_OFFSET);
      x = (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET + stack_dist -
          (SIDE_WIDTH / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - SIDE_HEIGHT) / 2;
    } else {
      // Transition State (0 < dist < 1)
      // Interpolate Width, Height
      w = CENTER_WIDTH - (int)((CENTER_WIDTH - SIDE_WIDTH) * dist);
      h = CENTER_HEIGHT - (int)((CENTER_HEIGHT - SIDE_HEIGHT) * dist);

      // Interpolate X
      // Start X (dist=0) = (LCD_WIDTH - CENTER_WIDTH)/2
      // End X (dist=1) = (LCD_WIDTH/2) + (CENTER_WIDTH/2) + GAP_OFFSET -
      // (SIDE_WIDTH/2)

      int start_x = (LCD_WIDTH - CENTER_WIDTH) / 2 +
                    (CENTER_WIDTH - w) / 2; // Center centered
      int end_x =
          (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET - (SIDE_WIDTH / 2);

      // Wait, simple linear interp on center coordinate might be better
      int center_pos_x = LCD_WIDTH / 2;
      int side_pos_x = (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET;

      int cur_center_x =
          center_pos_x + (int)((side_pos_x - center_pos_x) * dist);
      x = cur_center_x - (w / 2);
      y = Y_OFFSET + (CENTER_HEIGHT - h) / 2;
    }

    render_album_geometry_3d(idx, x, y, w, h, true); // 3D effect!
  }

  // Draw Left Side (iterating backwards from far left to near left)
  for (int i = range; i >= 1; i--) {
    int idx = center_idx - i;
    if (idx < 0)
      continue;

    float dist = anim_pos - (float)idx; // Positive value

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
    render_album_geometry_3d(idx, x, y, w, h, true); // 3D effect!
  }

  // Draw Center
  if (center_idx >= 0 && center_idx < album_count) {
    // Calculate dist for center (usually 0 unless anim_pos is between integers)
    // Actually center_idx is purely integer nearest.
    // But the "center" item might be animating away if anim_pos is x.5
    // Wait, the "Center" item is simply the one closest to anim_pos.

    float dist = anim_pos - (float)center_idx;
    // dist is between -0.5 and 0.5 roughly
    // We process it similarly but we need to handle signed distance

    // Actually the loop above handles indices != center_idx.
    // center_idx itself is always the "front" most item.
    // Its visual properties depend on |dist|.
    // Ideally dist should be close to 0.

    float abs_dist = dist > 0 ? dist : -dist;

    int w = CENTER_WIDTH - (int)((CENTER_WIDTH - SIDE_WIDTH) * abs_dist);
    int h = CENTER_HEIGHT - (int)((CENTER_HEIGHT - SIDE_HEIGHT) * abs_dist);

    int center_pos_x = LCD_WIDTH / 2;
    // If dist > 0 (moving right), X shifts left?
    // No, anim_pos increases -> we are moving to next album.
    // Current center is shifting LEFT to become left side.

    int target_x;
    if (dist > 0)
      target_x = (LCD_WIDTH / 2) - (CENTER_WIDTH / 2) - GAP_OFFSET;
    else
      target_x = (LCD_WIDTH / 2) + (CENTER_WIDTH / 2) + GAP_OFFSET;

    int cur_center_x =
        center_pos_x + (int)((target_x - center_pos_x) * abs_dist);
    int x = cur_center_x - (w / 2);
    int y = Y_OFFSET + (CENTER_HEIGHT - h) / 2;

    render_album_geometry(center_idx, x, y, w, h);

    // Text Info
    if (abs_dist < 0.2f) { // Only show text when mostly centered
      draw_scrolling_text(Y_OFFSET + CENTER_HEIGHT + REFLECTION_HEIGHT + 10,
                          albums[center_idx].name);

      char count_str[32];
      rb->snprintf(count_str, sizeof(count_str), "%d of %d", center_idx + 1,
                   album_count);
      int tw, th;
      rb->lcd_getstringsize(count_str, &tw, &th);
      rb->lcd_putsxy((LCD_WIDTH - tw) / 2, LCD_HEIGHT - 20, count_str);
    }
  }

  rb->lcd_update();
}

// Restoring draw_menu
void draw_menu(void) {
  // Draw a box over center
  int w = 180;
  int h = 100;
  int x = (LCD_WIDTH - w) / 2;
  int y = (LCD_HEIGHT - h) / 2;

  // Draw semi-transparent background (simulated by simple fill for now)
  rb->lcd_set_foreground(LCD_BLACK);
  rb->lcd_fillrect(x, y, w, h);
  rb->lcd_set_foreground(LCD_WHITE);
  rb->lcd_drawrect(x, y, w, h);

  rb->lcd_putsxy(x + 20, y + 20, "Play Album?");

  if (menu_selection == 0) {
    rb->lcd_putsxy(x + 30, y + 50, "> Yes (Play)");
    rb->lcd_putsxy(x + 30, y + 70, "  No (Cancel)");
  } else {
    rb->lcd_putsxy(x + 30, y + 50, "  Yes (Play)");
    rb->lcd_putsxy(x + 30, y + 70, "> No (Cancel)");
  }
  rb->lcd_update();
}

enum plugin_status plugin_start(const void *parameter) {
  (void)parameter;

  /* Use black on white - clean modern look */
  rb->lcd_set_background(LCD_WHITE);
  rb->lcd_set_foreground(LCD_BLACK);
  rb->lcd_clear_display();

  plugin_buffer = (unsigned char *)rb->plugin_get_buffer(&plugin_buffer_size);
  scratch_buffer = plugin_buffer;
  bitmap_storage_start = plugin_buffer + (160 * 160 * 2);

  rb->splash(HZ, "Scanning 3D...");

  scan_recursive("/", 0);

  if (album_count == 0) {
    rb->splash(HZ * 2, "No albums found.");
  }

  while (true) {
    if (current_state == STATE_BROWSE) {
      // Browsing logic
      float target = (float)current_index;
      float diff = target - anim_pos;

      if (diff > 0.01f || diff < -0.01f) {
        anim_pos += diff * 0.2f;
      } else {
        anim_pos = target;
      }
      draw_frame();

      // Non-blocking input for animation
      int button = rb->button_get(false);

      if (button != BUTTON_NONE) {
        switch (button) {
        case BUTTON_LEFT:
        case BUTTON_LEFT | BUTTON_REPEAT:
          if (current_index > 0)
            current_index--;
          else
            current_index = album_count - 1; // Wrap
          break;
        case BUTTON_RIGHT:
        case BUTTON_RIGHT | BUTTON_REPEAT:
          if (current_index < album_count - 1)
            current_index++;
          else
            current_index = 0; // Wrap
          break;
        case BUTTON_SELECT:
        case BUTTON_PLAY:
          current_state = STATE_MENU;
          menu_selection = 0; // Reset to "Play"
          // Wait a bit to avoid bounce
          rb->sleep(HZ / 4);
          // Clear queue if possible or just wait
          break;
        case BUTTON_POWER:
#ifdef BUTTON_BACK
        case BUTTON_BACK:
#endif
#ifdef BUTTON_HOME
        case BUTTON_HOME:
#endif
          return PLUGIN_OK;
        }
      } else {
        rb->yield();
      }

    } else if (current_state == STATE_MENU) {
      draw_menu();

      // Blocking input for menu (no animation needed)
      // This prevents accidental double-presses
      int button = rb->button_get(true);

      switch (button) {
      case BUTTON_UP:
      case BUTTON_UP | BUTTON_REPEAT:
      case BUTTON_DOWN:
      case BUTTON_DOWN | BUTTON_REPEAT:
        menu_selection = !menu_selection; // Toggle 0/1
        // Redraw will happen next loop
        break;
      case BUTTON_SELECT:
      case BUTTON_PLAY:
        if (menu_selection == 0) {
          // Play Album
          // Manual playlist construction
          rb->splash(HZ / 2, "Building Playlist...");

          struct playlist_info *pl = rb->playlist_get_current();
          rb->playlist_remove_all_tracks(pl);

          // Insert directory with recursion=true
          int ret = rb->playlist_insert_directory(
              pl, albums[current_index].path, 0, false, true);

          if (ret < 0) {
            char buf[32];
            rb->snprintf(buf, sizeof(buf), "Err: %d", ret);
            rb->splash(HZ * 2, buf);
            current_state = STATE_BROWSE;
            break;
          }

          // Check actual amount
          int amount = rb->playlist_amount();
          if (amount <= 0) {
            rb->splash(HZ * 2, "Empty Playlist!");
            current_state = STATE_BROWSE;
            break;
          }

          char msg[32];
          rb->snprintf(msg, sizeof(msg), "Playing %d trks", amount);
          rb->splash(HZ, msg);

          rb->playlist_start(0, 0, 0);

          // Important delay for WPS transition
          rb->sleep(HZ);

          return PLUGIN_GOTO_WPS;
        } else {
          // Cancel
          current_state = STATE_BROWSE;
        }
        break;

      case BUTTON_POWER:
#ifdef BUTTON_BACK
      case BUTTON_BACK:
#endif
#ifdef BUTTON_HOME
      case BUTTON_HOME:
#endif
        current_state = STATE_BROWSE; // Cancel menu
        break;
      }
    }
  }
  return PLUGIN_OK;
}
