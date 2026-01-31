#include "custom_db_browser.h"
#include "action.h"
#include "custom_db.h"
#include "debug.h"
#include "icons.h"
#include "kernel.h"
#include "lang.h"
#include "list.h"
#include "misc.h"
#include "playlist.h"
#include "root_menu.h"
#include "screens.h"
#include "splash.h"
#include "string.h"
#include "system.h"

/* Browser State */
enum browser_view {
  VIEW_MAIN_MENU = 0,
  VIEW_ARTIST_LIST,
  VIEW_ALBUM_LIST,          /* Albums for a specific artist */
  VIEW_TRACK_LIST,          /* Tracks for a specific album */
  VIEW_ALL_ALBUMS,          /* Global list of albums */
  VIEW_ALL_TRACKS,          /* Global list of tracks */
  VIEW_ALBUM_CONTEXT,       /* Context menu for an album */
  VIEW_GLOBAL_ALBUM_CONTEXT /* Context menu for a global album */
};

struct browser_context {
  enum browser_view view;
  int artist_idx;
  int album_idx_rel; /* Relative album index within artist */
  int selected_item;

  /* Cache for Album View (Artist) */
  int current_artist_start_entry;
  int current_artist_end_entry;

  /* Cache for Track View */
  int current_album_start_entry;
  int current_album_end_entry;
};

static struct browser_context ctx;
static struct gui_synclist db_list;

/* Helper: Find Album Range */
static void find_album_range(int artist_start, int artist_end,
                             int album_rel_idx, int *out_start, int *out_end) {
  struct db_entry entry;
  int current_entry = artist_start;
  int album_count = 0;

  *out_start = -1;
  *out_end = -1;

  uint32_t last_album_idx = (uint32_t)-1;

  while (current_entry < artist_end) {
    if (!custom_db_get_entry(current_entry, &entry))
      break;

    if (entry.album_idx != last_album_idx) {
      if (album_count == album_rel_idx) {
        *out_start = current_entry;
      } else if (album_count == album_rel_idx + 1) {
        /* Found start of next album, so prev album ended at current_entry */
        *out_end = current_entry;
        return;
      }
      album_count++;
      last_album_idx = entry.album_idx;
    }
    current_entry++;
  }

  if (*out_start != -1 && *out_end == -1) {
    *out_end = artist_end;
  }
}

/* Helper: Get Album Name */
static const char *get_album_name_str(int artist_start, int artist_end,
                                      int album_rel_idx) {
  struct db_entry entry;
  int current_entry = artist_start;
  int album_count = 0;

  uint32_t last_album_idx = (uint32_t)-1;

  while (current_entry < artist_end) {
    if (!custom_db_get_entry(current_entry, &entry))
      break;

    if (entry.album_idx != last_album_idx) {
      if (album_count == album_rel_idx) {
        return custom_db_get_string(entry.album_idx);
      }
      album_count++;
      last_album_idx = entry.album_idx;
    }
    current_entry++;
  }
  return "<Unknown Album>";
}

/* Main Menu Options */
enum { MENU_ARTIST = 0, MENU_ALBUM, MENU_TRACK, MENU_COUNT };

static const char *main_menu_items[] = {"Artists", "Albums", "Tracks"};

/* Album Context Menu Options */
enum { ALBUM_CTX_PLAY = 0, ALBUM_CTX_VIEW, ALBUM_CTX_COUNT };

static const char *album_ctx_items[] = {"Play Album", "View Tracks"};

/* List Callbacks */
static const char *db_browser_get_name(int selected_item, void *data,
                                       char *buffer, size_t buffer_len) {
  (void)data;
  (void)buffer;
  (void)buffer_len; /* Use static buffer from custom_db or copy */

  if (ctx.view == VIEW_MAIN_MENU) {
    if (selected_item >= 0 && selected_item < MENU_COUNT)
      return main_menu_items[selected_item];
    return "";
  } else if (ctx.view == VIEW_ALBUM_CONTEXT ||
             ctx.view == VIEW_GLOBAL_ALBUM_CONTEXT) {
    if (selected_item >= 0 && selected_item < ALBUM_CTX_COUNT)
      return album_ctx_items[selected_item];
    return "";
  } else if (ctx.view == VIEW_ARTIST_LIST) {
    int start_entry = custom_db_get_artist_start_index(selected_item);
    if (start_entry < 0)
      return "<Error>";

    struct db_entry entry;
    if (!custom_db_get_entry(start_entry, &entry))
      return "<Entry Error>";

    return custom_db_get_string(entry.artist_idx);
  } else if (ctx.view == VIEW_ALBUM_LIST) {
    return get_album_name_str(ctx.current_artist_start_entry,
                              ctx.current_artist_end_entry, selected_item);
  } else if (ctx.view == VIEW_TRACK_LIST) {
    int entry_idx = ctx.current_album_start_entry + selected_item;
    struct db_entry entry;
    if (!custom_db_get_entry(entry_idx, &entry))
      return "<Entry Error>";
    return custom_db_get_string(entry.title_idx);
  } else if (ctx.view == VIEW_ALL_ALBUMS) {
    int start_entry = custom_db_get_album_start_index(selected_item);
    struct db_entry entry;
    if (!custom_db_get_entry(start_entry, &entry))
      return "<Entry Error>";
    return custom_db_get_string(entry.album_idx);
  } else if (ctx.view == VIEW_ALL_TRACKS) {
    struct db_entry entry;
    if (!custom_db_get_entry(selected_item, &entry))
      return "<Entry Error>";
    return custom_db_get_string(entry.title_idx);
  }
  return "";
}

/* Helper: Play Tracks */
static int play_tracks(int start_entry, int end_entry,
                       int start_index_relative) {
  /* Create new dynamic playlist (Refreshes current playlist completely) */
  playlist_create(NULL, NULL);

  struct playlist_info *pl = playlist_get_current();

  struct db_entry entry;
  for (int i = start_entry; i < end_entry; i++) {
    if (custom_db_get_entry(i, &entry)) {
      const char *path = custom_db_get_string(entry.path_idx);
      /* Important: queue=false, sync=false to build playlist first */
      playlist_insert_track(pl, path, PLAYLIST_INSERT_LAST, false, false);
    }
  }

  if (playlist_amount() > 0) {
    playlist_start(start_index_relative, 0, 0);
    return GO_TO_WPS;
  }

  return GO_TO_ROOT;
}

int custom_db_browser_main(void *param) {
  (void)param;
  bool exit_browser = false;
  int ret_val = GO_TO_ROOT;

  if (!custom_db_init()) {
    splash(HZ * 2, "DB Init Failed");
    return GO_TO_ROOT;
  }

  /* Start at Main Menu */
  ctx.view = VIEW_MAIN_MENU;
  ctx.selected_item = 0;

  gui_synclist_init(&db_list, db_browser_get_name, NULL, false, 1, NULL);

  while (!exit_browser) {
    int count = 0;
    const char *title = "Database";

    if (ctx.view == VIEW_MAIN_MENU) {
      count = MENU_COUNT;
      title = "Database";
    } else if (ctx.view == VIEW_ALBUM_CONTEXT ||
               ctx.view == VIEW_GLOBAL_ALBUM_CONTEXT) {
      count = ALBUM_CTX_COUNT;
      title = "Album Options";
    } else if (ctx.view == VIEW_ARTIST_LIST) {
      count = custom_db_get_artist_count();
      title = "Artists";
    } else if (ctx.view == VIEW_ALL_ALBUMS) {
      count = custom_db_get_album_count();
      title = "All Albums";
    } else if (ctx.view == VIEW_ALL_TRACKS) {
      count = custom_db_get_entry_count();
      title = "All Tracks";
    } else if (ctx.view == VIEW_ALBUM_LIST) {
      /* Count albums for artist */
      ctx.current_artist_start_entry =
          custom_db_get_artist_start_index(ctx.artist_idx);
      /* Find end entry */
      if (ctx.artist_idx + 1 < custom_db_get_artist_count())
        ctx.current_artist_end_entry =
            custom_db_get_artist_start_index(ctx.artist_idx + 1);
      else
        ctx.current_artist_end_entry = custom_db_get_entry_count();

      /* Iterate to count albums */
      struct db_entry entry;
      int curr = ctx.current_artist_start_entry;
      uint32_t last_album = (uint32_t)-1;
      count = 0;
      while (curr < ctx.current_artist_end_entry) {
        if (custom_db_get_entry(curr, &entry)) {
          if (entry.album_idx != last_album) {
            count++;
            last_album = entry.album_idx;
          }
        }
        curr++;
      }

      title = "Albums";
    } else if (ctx.view == VIEW_TRACK_LIST) {
      if (ctx.artist_idx == -1) {
        /* Global Album Mode: range already set in VIEW_ALL_ALBUMS/CTX logic, do
         * nothing */
      } else {
        /* Artist Mode: Recalculate range just in case */
        find_album_range(ctx.current_artist_start_entry,
                         ctx.current_artist_end_entry, ctx.album_idx_rel,
                         &ctx.current_album_start_entry,
                         &ctx.current_album_end_entry);
      }
      count = ctx.current_album_end_entry - ctx.current_album_start_entry;
      title = "Tracks";
    }

    gui_synclist_set_title(&db_list, title, Icon_Audio);
    gui_synclist_set_nb_items(&db_list, count);
    gui_synclist_select_item(&db_list, ctx.selected_item);
    gui_synclist_draw(&db_list);

    int button = get_action(CONTEXT_TREE, HZ / 2);

    if (gui_synclist_do_button(&db_list, &button)) {
      ctx.selected_item = gui_synclist_get_sel_pos(&db_list);
      continue;
    }

    switch (button) {
    case ACTION_STD_OK:
      ctx.selected_item = gui_synclist_get_sel_pos(&db_list);

      if (ctx.view == VIEW_MAIN_MENU) {
        if (ctx.selected_item == MENU_ARTIST) {
          ctx.view = VIEW_ARTIST_LIST;
          ctx.selected_item = 0;
        } else if (ctx.selected_item == MENU_ALBUM) {
          ctx.view = VIEW_ALL_ALBUMS;
          ctx.selected_item = 0;
        } else if (ctx.selected_item == MENU_TRACK) {
          ctx.view = VIEW_ALL_TRACKS;
          ctx.selected_item = 0;
        }
      } else if (ctx.view == VIEW_ARTIST_LIST) {
        ctx.artist_idx = ctx.selected_item;
        ctx.view = VIEW_ALBUM_LIST;
        ctx.selected_item = 0;
      } else if (ctx.view == VIEW_ALBUM_LIST) {
        ctx.album_idx_rel = ctx.selected_item;
        ctx.view = VIEW_ALBUM_CONTEXT;
        ctx.selected_item = 0;
      } else if (ctx.view == VIEW_ALBUM_CONTEXT ||
                 ctx.view == VIEW_GLOBAL_ALBUM_CONTEXT) {
        if (ctx.selected_item == ALBUM_CTX_PLAY) {
          /* Play Album */

          /* If Artist Context, we need to find range. If Global, range is
           * already valid in cache. */
          if (ctx.view == VIEW_ALBUM_CONTEXT) {
            find_album_range(ctx.current_artist_start_entry,
                             ctx.current_artist_end_entry, ctx.album_idx_rel,
                             &ctx.current_album_start_entry,
                             &ctx.current_album_end_entry);
          }

          ret_val = play_tracks(ctx.current_album_start_entry,
                                ctx.current_album_end_entry, 0);
          exit_browser = true;

        } else {
          /* View Tracks */
          ctx.view = VIEW_TRACK_LIST;
          ctx.selected_item = 0;
        }
      } else if (ctx.view == VIEW_ALL_ALBUMS) {
        /* Global Album Selected -> Go to Global Context */
        int start_entry = custom_db_get_album_start_index(ctx.selected_item);
        int end_entry;
        if (ctx.selected_item + 1 < custom_db_get_album_count())
          end_entry = custom_db_get_album_start_index(ctx.selected_item + 1);
        else
          end_entry = custom_db_get_entry_count();

        ctx.current_album_start_entry = start_entry;
        ctx.current_album_end_entry = end_entry;

        /* Mark as global context by setting artist_idx -1 and switching view */
        ctx.artist_idx = -1;
        ctx.view = VIEW_GLOBAL_ALBUM_CONTEXT;
        ctx.selected_item = 0;
      } else if (ctx.view == VIEW_TRACK_LIST) {
        /* Play Album starting from Track */
        /* ctx.selected_item is the offset within the album */
        ret_val = play_tracks(ctx.current_album_start_entry,
                              ctx.current_album_end_entry, ctx.selected_item);
        exit_browser = true;
      } else if (ctx.view == VIEW_ALL_TRACKS) {
        /* Play single track */
        ret_val = play_tracks(ctx.selected_item, ctx.selected_item + 1, 0);
        exit_browser = true;
      }
      break;

    case ACTION_STD_CANCEL:
      if (ctx.view == VIEW_TRACK_LIST) {
        if (ctx.artist_idx == -1) {
          /* Back to Global Context */
          ctx.view = VIEW_GLOBAL_ALBUM_CONTEXT;
          ctx.selected_item = 0;
        } else {
          /* Back to Artist Context */
          ctx.view = VIEW_ALBUM_CONTEXT;
          ctx.selected_item = 0;
        }
      } else if (ctx.view == VIEW_ALBUM_CONTEXT) {
        ctx.view = VIEW_ALBUM_LIST;
        ctx.selected_item = ctx.album_idx_rel;
      } else if (ctx.view == VIEW_GLOBAL_ALBUM_CONTEXT) {
        ctx.view = VIEW_ALL_ALBUMS;
        /* Recover global album index? We don't track cached global album
         * selection well. */
        /* Need to store global album idx somewhere or infer? */
        /* Let's reset to 0 for now. */
        ctx.selected_item = 0;
      } else if (ctx.view == VIEW_ALBUM_LIST) {
        ctx.view = VIEW_ARTIST_LIST;
        ctx.selected_item = ctx.artist_idx;
      } else if (ctx.view == VIEW_ARTIST_LIST || ctx.view == VIEW_ALL_ALBUMS ||
                 ctx.view == VIEW_ALL_TRACKS) {
        ctx.view = VIEW_MAIN_MENU;
        ctx.selected_item = 0;
      } else if (ctx.view == VIEW_MAIN_MENU) {
        exit_browser = true;
        ret_val = GO_TO_ROOT;
      }
      break;
    }
  }

  return ret_val;
}
