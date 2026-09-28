/*
 * MONSTER HOP for the desktop - the window, LVGL and the controls
 *
 * The game is the watch's own code (AmoledOS, apps/monsterhop), built with a
 * wider frame: DK_VIEW_W x DK_VIEW_H, the watch's height and twice its
 * columns. LVGL draws into one full frame that SDL scales to the window,
 * letterboxed; the game's panels keep the watch's column in the middle.
 *
 * Controls
 *   arrows / WASD      hop (held: keeps hopping); the menus
 *   Space / Enter      the watch's button: lever, crate, chest, super hop
 *   Esc / P            pause; on the menus, back
 *   Backspace          back
 *   F11, Alt+Enter     full screen
 *   a gamepad          d-pad or stick to hop, A the button, Start pause, B back
 * Two players on one screen: input.c.
 */
#include "desktop.h"

#include "mh_app.h"

#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;
static uint16_t *s_fb;
static bool s_dirty;

static aos_app_t s_app;
static app_t *s_game;

/* ---- LVGL's display: one whole frame, straight into the texture ---- */

static uint32_t tick_cb(void)
{
    return (uint32_t)SDL_GetTicks64();
}

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    if (lv_display_flush_is_last(d)) s_dirty = true;
    lv_display_flush_ready(d);
}

static int s_mx, s_my;
static bool s_mdown;

static void mouse_read(lv_indev_t *in, lv_indev_data_t *data)
{
    (void)in;
    data->point.x = s_mx;
    data->point.y = s_my;
    data->state = s_mdown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ---- development: MH_SHOT=<prefix> MH_SHOT_AT=ms,ms,... saves the frame
 * as <prefix>_<n>.bmp at those times and quits after the last;
 * MH_KEYS=ms:key,ms:key... presses up/down/left/right/act/pause/back ---- */

static void dev_shots(void)
{
    static int n = -1;
    static char at[256];
    const char *pre = getenv("MH_SHOT");
    if (!pre || !pre[0]) return;
    if (n < 0) {
        const char *e = getenv("MH_SHOT_AT");
        snprintf(at, sizeof at, "%s", e && e[0] ? e : "4000");
        n = 0;
    }
    /* the n-th time in the list */
    const char *p = at;
    for (int i = 0; i < n && p; i++) {
        p = strchr(p, ',');
        if (p) p++;
    }
    if (!p || !*p) {
        aos_ui_back();
        return;
    }
    if (SDL_GetTicks64() < (uint64_t)atol(p)) return;
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(s_fb, DK_VIEW_W, DK_VIEW_H, 16, DK_VIEW_W * 2,
                                                         SDL_PIXELFORMAT_RGB565);
    char path[512];
    snprintf(path, sizeof path, "%s_%d.bmp", pre, n);
    if (sf) {
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    aos_hal_log("dev", "shot %s", path);
    n++;
}

/* ---- the window ---- */

static void present(void)
{
    static uint32_t n, t0;
    n++;
    if (SDL_GetTicks() - t0 > 2000) {
        if (getenv("MH_FPS")) aos_hal_log("dk", "%u presents", (unsigned)n);
        n = 0;
        t0 = SDL_GetTicks();
    }
    SDL_UpdateTexture(s_tex, NULL, s_fb, DK_VIEW_W * 2);
    /* whole multiples stay sharp; anything else is filtered */
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(s_ren, &ow, &oh);
    int kx = ow / DK_VIEW_W, ky = oh / DK_VIEW_H;
    int k = kx < ky ? kx : ky;
    bool exact = k >= 1 && (ow == k * DK_VIEW_W || oh == k * DK_VIEW_H);
    SDL_SetTextureScaleMode(s_tex, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

static void toggle_fullscreen(void)
{
    bool fs = (SDL_GetWindowFlags(s_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(s_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    aos_hal_pref_set_i32("dk_full", fs ? 0 : 1);
}

/* for the game's settings */
bool mh_desktop_fullscreen(bool toggle)
{
    if (toggle) toggle_fullscreen();
    return (SDL_GetWindowFlags(s_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

/* full screen, whatever the game is doing */
static bool hotkey(const SDL_KeyboardEvent *e)
{
    SDL_Keycode k = e->keysym.sym;
    bool alt = (e->keysym.mod & KMOD_ALT) != 0, gui = (e->keysym.mod & KMOD_GUI) != 0;
    if (k == SDLK_F11 || ((alt || gui) && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) ||
        (gui && (e->keysym.mod & KMOD_CTRL) && k == SDLK_f)) {
        if (!e->repeat) toggle_fullscreen();
        return true;
    }
    return false;
}

/* the biggest whole multiple of the frame that fits the screen, else the
 * biggest 16:9 that does */
static void window_size(int *w, int *h)
{
    SDL_Rect r = { 0, 0, 1280, 720 };
    SDL_GetDisplayUsableBounds(0, &r);
    int mw = r.w * 9 / 10, mh = r.h * 9 / 10;
    int k = 1;
    while ((k + 1) * DK_VIEW_W <= mw && (k + 1) * DK_VIEW_H <= mh) k++;
    *w = k * DK_VIEW_W;
    *h = k * DK_VIEW_H;
    if (*w > mw || *h > mh) {
        *w = mw;
        *h = mw * 9 / 16;
        if (*h > mh) {
            *h = mh;
            *w = mh * 16 / 9;
        }
    }
}

/* ---- language: what was chosen, else the system's ---- */

static void language(void)
{
    char saved[16];
    if (aos_hal_pref_get_str("lang", saved, sizeof saved) && saved[0]) {
        aos_i18n_init();
    } else {
        const char *pick = "en";
        SDL_Locale *loc = SDL_GetPreferredLocales();
        if (loc && loc[0].language) {
            if (!strcmp(loc[0].language, "es")) pick = "es";
            else if (!strcmp(loc[0].language, "de")) pick = "de";
        }
        SDL_free(loc);
        aos_i18n_set(pick);
    }
    aos_i18n_app_load("demo.monsterhop");
}

static void dev_keys(void)
{
    static const char *p;
    static bool started;
    if (!started) {
        started = true;
        p = getenv("MH_KEYS");
    }
    if (!p || !*p || !s_game) return;
    if (SDL_GetTicks64() < (uint64_t)atol(p)) return;
    const char *k = strchr(p, ':');
    if (!k) {
        p = NULL;
        return;
    }
    k++;
    /* "2" before a key: player two's */
    int pl = 0;
    if (*k == '2') {
        pl = 1;
        k++;
    }
    if (!strncmp(k, "up", 2)) dk_input_dir(pl, DIR_N);
    else if (!strncmp(k, "down", 4)) dk_input_dir(pl, DIR_S);
    else if (!strncmp(k, "left", 4)) dk_input_dir(pl, DIR_W);
    else if (!strncmp(k, "right", 5)) dk_input_dir(pl, DIR_E);
    else if (!strncmp(k, "act", 3)) dk_input_action(pl);
    else if (!strncmp(k, "pause", 5)) dk_input_pause();
    else if (!strncmp(k, "back", 4)) dk_input_back();
    p = strchr(k, ',');
    if (p) p++;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    dk_paths_init();
    dk_prefs_load();
    aos_hal_log("mh", "data %s | user %s", aos_hal_path_apps(), dk_user_dir());

    mh_view_w = DK_VIEW_W;
    mh_view_h = DK_VIEW_H;

    int ww, wh;
    window_size(&ww, &wh);
    s_win = SDL_CreateWindow("Monster Hop", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_win) {
        fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(s_win, DK_VIEW_W / 2, DK_VIEW_H / 2);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren) s_ren = SDL_CreateRenderer(s_win, -1, 0);
    SDL_RenderSetLogicalSize(s_ren, DK_VIEW_W, DK_VIEW_H);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, DK_VIEW_W, DK_VIEW_H);
    int32_t full = 0;
    if (aos_hal_pref_get_i32("dk_full", &full) && full) SDL_SetWindowFullscreen(s_win, SDL_WINDOW_FULLSCREEN_DESKTOP);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(DK_VIEW_W, DK_VIEW_H);
    size_t fb_bytes = (size_t)DK_VIEW_W * DK_VIEW_H * 2;
    s_fb = (uint16_t *)calloc(1, fb_bytes);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, (uint32_t)fb_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, mouse_read);

    aos_theme_init();
    language();

    /* the watch-sized column the game's panels are laid out in */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    /* the whole frame, its padding making the watch's column the origin of
     * the game's coordinates (the frame's canvas and the panels step back out
     * of it by the same amount) */
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, DK_VIEW_W, DK_VIEW_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_pad_left(root, (DK_VIEW_W - AOS_SCREEN_W) / 2, 0);
    lv_obj_set_style_pad_top(root, (DK_VIEW_H - AOS_SCREEN_H) / 2, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    bool (*init)(aos_app_t *) = dk_app_init();
    if (!init || !init(&s_app) || !s_app.create) {
        fprintf(stderr, "the game did not register\n");
        return 1;
    }
    s_game = (app_t *)s_app.create(&s_app, root);
    if (!s_game) {
        fprintf(stderr, "the game did not open\n");
        return 1;
    }
    dk_input_init(s_game);

    bool run = true;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: run = false; break;
            case SDL_KEYDOWN:
                if (!hotkey(&e.key)) dk_input_key(&e.key, true);
                break;
            case SDL_KEYUP: dk_input_key(&e.key, false); break;
            case SDL_MOUSEMOTION:
                s_mx = e.motion.x;
                s_my = e.motion.y;
                dk_nav_mouse();
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    s_mx = e.button.x;
                    s_my = e.button.y;
                    s_mdown = e.type == SDL_MOUSEBUTTONDOWN;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED: dk_input_pad_added(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: dk_input_pad_removed(e.cdevice.which); break;
            case SDL_CONTROLLERBUTTONDOWN: dk_input_pad_button(e.cbutton.which, e.cbutton.button, true); break;
            case SDL_CONTROLLERBUTTONUP: dk_input_pad_button(e.cbutton.which, e.cbutton.button, false); break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && s_app.hide) s_app.hide(&s_app, s_game);
                s_dirty = true;
                break;
            default: break;
            }
        }
        dk_input_tick();
        dev_keys();
        uint32_t idle = lv_timer_handler();
        dk_toast_tick();
        dk_nav_tick();
        dev_shots();
        dk_prefs_flush(false);
        if (dk_quit_asked()) run = false;
        if (s_dirty) {
            s_dirty = false;
            present();
        } else {
            SDL_Delay(idle > 4 ? 4 : idle ? idle : 1);
        }
    }

    s_app.destroy(&s_app, s_game);
    dk_prefs_flush(true);
    dk_input_close();
    SDL_DestroyTexture(s_tex);
    SDL_DestroyRenderer(s_ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
