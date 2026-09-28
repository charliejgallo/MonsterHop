/*
 * MONSTER HOP for the desktop - what the host's files share
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* the frame the game draws: 16:9 with the watch's height, so as many rows
 * ahead as on the watch and twice the columns */
#define DK_VIEW_W   800
#define DK_VIEW_H   450

/* hal_desktop.c */
void dk_paths_init(void);
const char *dk_user_dir(void);
void dk_prefs_load(void);
void dk_prefs_flush(bool now);

/* ui_desktop.c */
typedef struct aos_app_s aos_app_t;
bool (*dk_app_init(void))(aos_app_t *);
bool dk_quit_asked(void);
void dk_toast_tick(void);
