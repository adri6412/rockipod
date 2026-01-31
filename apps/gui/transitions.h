/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
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

#ifndef _TRANSITIONS_H_
#define _TRANSITIONS_H_

#include "config.h"
#include "screen_access.h"

/* Transition types */
enum transition_type {
  TRANSITION_NONE,
  TRANSITION_FADE,
};

/* Transition state */
struct transition_state {
  enum transition_type type;
  bool active;
  int current_step;
  int total_steps;
  unsigned long start_time;
  void *prev_screen_buffer;
};

/* Initialize transition system */
void transition_init(void);

/* Cleanup transition system */
void transition_cleanup(void);

/* Capture current screen as "previous" for transition */
void transition_capture_screen(struct screen *display);

/* Start a transition animation */
void transition_start(enum transition_type type, struct screen *display,
                      int duration_ms);

/* Update transition animation (call each frame) */
bool transition_update(struct screen *display);

/* Check if transition is active */
bool transition_is_active(void);

/* Get transition progress (0-100) */
int transition_get_progress(void);

#endif /* _TRANSITIONS_H_ */
