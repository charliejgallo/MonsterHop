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

/* nav.c: the menus with keys (dir 0 up, 1 right, 2 down, 3 left) */
bool dk_nav_dir(int dir);
bool dk_nav_enter(void);
void dk_nav_mouse(void);
void dk_nav_tick(void);

/* input.c: keys and gamepads, one player or two */
typedef struct app app_t;
typedef struct SDL_KeyboardEvent SDL_KeyboardEvent;
void dk_input_init(app_t *game);
void dk_input_key(const SDL_KeyboardEvent *e, bool down);
void dk_input_pad_added(int device);
void dk_input_pad_removed(int32_t id);
void dk_input_pad_button(int32_t id, int button, bool down);
void dk_input_tick(void);
void dk_input_close(void);
void dk_input_dir(int player, int dir);
void dk_input_action(int player);
void dk_input_pause(void);
void dk_input_back(void);
