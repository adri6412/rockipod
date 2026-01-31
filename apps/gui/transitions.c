/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2024 Custom iPod-like transitions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "transitions.h"
#include "kernel.h"
#include "lcd.h"
#include "system.h"
#include <string.h>

/* Global state */
static bool initialized = false;

/* Static screen buffers */
static fb_data screen_buffer[LCD_WIDTH * LCD_HEIGHT];
static fb_data next_screen_buffer[LCD_WIDTH * LCD_HEIGHT];

void transition_init(void) {
  if (initialized)
    return;

  initialized = true;
  memset(screen_buffer, 0, sizeof(screen_buffer));
  memset(next_screen_buffer, 0, sizeof(next_screen_buffer));
}

void transition_cleanup(void) { initialized = false; }

void transition_capture_screen(struct screen *display) {
  if (!initialized)
    return;

  struct viewport *vp = lcd_current_viewport;

  /* Capture screen using FBADDR relative to current viewport */
  for (int y = 0; y < vp->height; y++) {
    for (int x = 0; x < vp->width; x++) {
      fb_data *pixel = FBADDR(x, y);
      screen_buffer[y * LCD_WIDTH + x] = *pixel;
    }
  }
}

void transition_start(enum transition_type type, struct screen *display,
                      int duration_ms) {
  if (!initialized)
    return;

  /* Capture the NEW menu (currently on screen) into next_screen_buffer */
  struct viewport *vp = lcd_current_viewport;
  int width = vp->width;
  int height = vp->height;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      fb_data *pixel = FBADDR(x, y);
      next_screen_buffer[y * LCD_WIDTH + x] = *pixel;
    }
  }

  /* iPod-style Push Left Animation */
  int frames = 12;

  for (int frame = 0; frame <= frames; frame++) {
    int offset = (frame * width) / frames;

    /* 1. Draw Old Screen (sliding Left)
       Visible part: from src_x=offset, to dst_x=0. Width = width-offset.
    */
    if (width > offset) {
      display->bitmap_part(screen_buffer, offset, 0, LCD_WIDTH, 0, 0,
                           width - offset, height);
    }

    /* 2. Draw New Screen (sliding in from Right)
       Visible part: from src_x=0, to dst_x=width-offset. Width = offset.
    */
    if (offset > 0) {
      display->bitmap_part(next_screen_buffer, 0, 0, LCD_WIDTH, width - offset,
                           0, offset, height);
    }

    display->update();
    yield();
  }

  (void)type;
  (void)duration_ms;
}

bool transition_update(struct screen *display) {
  (void)display;
  return false;
}

bool transition_is_active(void) { return false; }

int transition_get_progress(void) { return 100; }
