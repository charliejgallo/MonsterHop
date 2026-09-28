/*
 * MONSTER HOP for the desktop - keys and gamepads, one player or two
 *
 * Alone, every key and every gamepad is the one player. In a two-player
 * race (the split screen) they are shared out, with no setting to choose:
 *
 *   keyboard only       player 1 WASD + Space (or Left Shift)
 *                       player 2 arrows + Enter (or Right Shift)
 *   one gamepad         it is player 2's; player 1 keeps WASD
 *   two gamepads        the first connected is player 1's, the second
 *                       player 2's; the keys above still work
 *
 * Esc / P / Start pause for either. On the menus anything moves the
 * highlight (nav.c).
 */
#include "desktop.h"

#include "mh_app.h"

#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>

#define REPEAT_FIRST_MS 200
#define REPEAT_MS       120
#define STICK_ON        16000
#define STICK_OFF       9000
#define MAX_PADS        4

static app_t *s_game;

/* a held direction per player: it keeps hopping */
typedef struct {
    int dir, src;
    uint64_t next;
} held_t;
static held_t s_held[2] = { { -1, 0, 0 }, { -1, 0, 0 } };

static SDL_GameController *s_pads[MAX_PADS];
static int s_stick[MAX_PADS] = { -1, -1, -1, -1 };

void dk_input_init(app_t *game)
{
    s_game = game;
}

static bool split(void)
{
    return s_game && s_game->split;
}

static bool racing2(void)
{
    return split() && s_game->state == ST_PLAY;
}

static int pad_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PADS; i++) n += s_pads[i] != NULL;
    return n;
}

/* ---- what the players do ---- */

static void dir_press(int player, int dir)
{
    if (!s_game) return;
    if (player == 1 && racing2()) {
        mha_key_hop2(s_game, dir);
        return;
    }
    if (!mha_key_hop(s_game, dir)) dk_nav_dir(dir);
}

void dk_input_dir(int player, int dir)
{
    dir_press(player, dir);
}

void dk_input_action(int player)
{
    if (!s_game) return;
    if (player == 1 && racing2()) {
        mha_key_action2(s_game);
        return;
    }
    if (!mha_key_action(s_game)) dk_nav_enter();
}

void dk_input_back(void)
{
    if (s_game && !mha_key_back(s_game)) aos_ui_back();
}

void dk_input_pause(void)
{
    if (s_game && !mha_key_pause(s_game)) dk_input_back();
}

static void hold(int player, int dir, int src)
{
    dir_press(player, dir);
    s_held[player].dir = dir;
    s_held[player].src = src;
    s_held[player].next = SDL_GetTicks64() + REPEAT_FIRST_MS;
}

static void release(int player, int dir, int src)
{
    if (s_held[player].dir == dir && s_held[player].src == src) s_held[player].dir = -1;
}

/* ---- the keyboard ---- */

/* the player a key belongs to (0 alone), its direction or -1 */
static int key_dir(SDL_Keycode k, int *player)
{
    bool two = split();
    switch (k) {
    case SDLK_w: *player = 0; return DIR_N;
    case SDLK_d: *player = 0; return DIR_E;
    case SDLK_s: *player = 0; return DIR_S;
    case SDLK_a: *player = 0; return DIR_W;
    case SDLK_UP: *player = two; return DIR_N;
    case SDLK_RIGHT: *player = two; return DIR_E;
    case SDLK_DOWN: *player = two; return DIR_S;
    case SDLK_LEFT: *player = two; return DIR_W;
    default: return -1;
    }
}

/* the button's key, and whose: -1 if it is not one */
static int key_action(SDL_Keycode k)
{
    bool two = split();
    switch (k) {
    case SDLK_SPACE: case SDLK_LSHIFT: case SDLK_e: case SDLK_q:
        return 0;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_RSHIFT: case SDLK_RCTRL: case SDLK_KP_0:
        return two;
    case SDLK_z: case SDLK_j:
        return 0;
    default:
        return -1;
    }
}

void dk_input_key(const SDL_KeyboardEvent *e, bool down)
{
    SDL_Keycode k = e->keysym.sym;
    int player = 0;
    int dir = key_dir(k, &player);
    if (!down) {
        if (dir >= 0) release(player, dir, 1);
        return;
    }
    if (e->repeat) return;
    if (dir >= 0) {
        hold(player, dir, 1);
        return;
    }
    int act = key_action(k);
    if (act >= 0) {
        dk_input_action(act);
        return;
    }
    switch (k) {
    case SDLK_ESCAPE: case SDLK_p: dk_input_pause(); break;
    case SDLK_BACKSPACE: dk_input_back(); break;
    default: break;
    }
}

/* ---- gamepads ---- */

static int pad_slot(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pads[i])) == id) return i;
    }
    return -1;
}

/* whose a gamepad is: the rule at the top */
static int pad_player(int slot)
{
    if (!split()) return 0;
    if (pad_count() < 2) return 1;
    int order = 0;
    for (int i = 0; i < slot; i++) order += s_pads[i] != NULL;
    return order == 0 ? 0 : 1;
}

void dk_input_pad_added(int device)
{
    if (!SDL_IsGameController(device)) return;
    SDL_GameController *p = SDL_GameControllerOpen(device);
    if (!p) return;
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p));
    if (pad_slot(id) >= 0) {
        SDL_GameControllerClose(p);
        return;
    }
    for (int i = 0; i < MAX_PADS; i++) {
        if (!s_pads[i]) {
            s_pads[i] = p;
            s_stick[i] = -1;
            aos_hal_log("pad", "%d: %s", i, SDL_GameControllerName(p));
            return;
        }
    }
    SDL_GameControllerClose(p);
}

void dk_input_pad_removed(SDL_JoystickID id)
{
    int i = pad_slot(id);
    if (i < 0) return;
    SDL_GameControllerClose(s_pads[i]);
    s_pads[i] = NULL;
    for (int p = 0; p < 2; p++) {
        if (s_held[p].src == 10 + i || s_held[p].src == 20 + i) s_held[p].dir = -1;
    }
}

static int pad_dir(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return DIR_N;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return DIR_E;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return DIR_S;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return DIR_W;
    default: return -1;
    }
}

void dk_input_pad_button(SDL_JoystickID id, int b, bool down)
{
    int slot = pad_slot(id);
    if (slot < 0) return;
    int player = pad_player(slot);
    int dir = pad_dir(b);
    if (dir >= 0) {
        if (down) hold(player, dir, 10 + slot);
        else release(player, dir, 10 + slot);
        return;
    }
    if (!down) return;
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_X: dk_input_action(player); break;
    case SDL_CONTROLLER_BUTTON_START: dk_input_pause(); break;
    case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: dk_input_back(); break;
    default: break;
    }
}

/* the left sticks as d-pads, with some hysteresis; the held directions */
void dk_input_tick(void)
{
    for (int i = 0; i < MAX_PADS; i++) {
        SDL_GameController *p = s_pads[i];
        if (!p) continue;
        int x = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        int ax = abs(x), ay = abs(y);
        int dir = -1;
        int lim = s_stick[i] >= 0 ? STICK_OFF : STICK_ON;
        if (ax > lim || ay > lim) dir = ax > ay ? (x > 0 ? DIR_E : DIR_W) : (y > 0 ? DIR_S : DIR_N);
        if (dir == s_stick[i]) continue;
        int player = pad_player(i);
        if (s_stick[i] >= 0) release(player, s_stick[i], 20 + i);
        s_stick[i] = dir;
        if (dir >= 0) hold(player, dir, 20 + i);
    }
    uint64_t now = SDL_GetTicks64();
    for (int p = 0; p < 2; p++) {
        if (s_held[p].dir < 0 || now < s_held[p].next) continue;
        dir_press(p, s_held[p].dir);
        s_held[p].next = now + REPEAT_MS;
    }
}

void dk_input_close(void)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i]) SDL_GameControllerClose(s_pads[i]);
        s_pads[i] = NULL;
    }
}

/* ---- for the lobby: how each plays, in the game's language ---- */

const char *mh_desktop_controls(void)
{
    static char buf[160];
    const char *lang = aos_i18n_current();
    int lg = !strcmp(lang, "es") ? 0 : !strcmp(lang, "de") ? 2 : 1;
    int n = pad_count();
    static const char *const none[3] = {
        "J1: WASD + Espacio\nJ2: flechas + Enter",
        "P1: WASD + Space\nP2: arrows + Enter",
        "S1: WASD + Leertaste\nS2: Pfeiltasten + Enter",
    };
    static const char *const one[3] = {
        "J1: WASD + Espacio\nJ2: joystick",
        "P1: WASD + Space\nP2: gamepad",
        "S1: WASD + Leertaste\nS2: Gamepad",
    };
    static const char *const two[3] = {
        "J1: joystick 1\nJ2: joystick 2",
        "P1: gamepad 1\nP2: gamepad 2",
        "S1: Gamepad 1\nS2: Gamepad 2",
    };
    snprintf(buf, sizeof buf, "%s", (n >= 2 ? two : n == 1 ? one : none)[lg]);
    return buf;
}
