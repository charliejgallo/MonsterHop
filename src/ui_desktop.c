/*
 * MONSTER HOP for the desktop - the watch's UI runtime, the part the game uses
 *
 * The runtime on the watch opens apps, shows toasts and turns a swipe right
 * into "back". Here there is one app: it registers itself (AOS_APP_ENTRY
 * with AOS_SIM_BUILTIN, as in the simulator) and main.c opens it.
 */
#include "desktop.h"

#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <stdio.h>

static bool (*s_init)(aos_app_t *);
static bool s_quit;

void aos_sim_register_app(bool (*init)(aos_app_t *app))
{
    s_init = init;
}

void aos_sim_register_app_many(uint32_t (*count)(void), bool (*init_at)(aos_app_t *app, uint32_t index))
{
    (void)count;
    (void)init_at;
}

bool (*dk_app_init(void))(aos_app_t *)
{
    return s_init;
}

/* the game leaves from its title: the window closes */
void aos_ui_back(void)
{
    s_quit = true;
}

bool dk_quit_asked(void)
{
    return s_quit;
}

/* the keys go straight to the game (mha_key_*): no gestures here */
int aos_ui_take_gesture(void)
{
    return AOS_TOUCH_GESTURE_NONE;
}

bool aos_icon_set_ops(const aos_app_t *app, const uint8_t *ops, size_t len)
{
    (void)app;
    (void)ops;
    (void)len;
    return true;
}

/* ---- toasts: a pill at the bottom of the screen ---- */

static lv_obj_t *s_toast;
static uint32_t s_toast_until;

void aos_ui_toast(const char *text, uint32_t ms)
{
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
        lv_obj_set_style_text_color(s_toast, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_toast, aos_font_body, 0);
        lv_obj_set_style_radius(s_toast, 18, 0);
        lv_obj_set_style_pad_hor(s_toast, 18, 0);
        lv_obj_set_style_pad_ver(s_toast, 8, 0);
        lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_label_set_text(s_toast, text ? text : "");
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    s_toast_until = lv_tick_get() + (ms ? ms : 1600);
}

void dk_toast_tick(void)
{
    if (s_toast && !lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN) && (int32_t)(lv_tick_get() - s_toast_until) >= 0)
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}
