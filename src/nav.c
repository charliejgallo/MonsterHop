/*
 * MONSTER HOP for the desktop - the menus with keys and a gamepad
 *
 * The watch's menus are made for a finger: every button, level pad, shop
 * item and album card is an LVGL object with a CLICKED callback. Here the
 * arrows move a highlight to the nearest of those in that direction, on
 * whatever panel is showing, and Enter clicks it. Nothing in the game knows.
 */
#include "desktop.h"

#include "lvgl.h"

#include <stdlib.h>

#define MAX_CAND 128

static lv_obj_t *s_focus;
static lv_obj_t *s_ring;
static bool s_active;           /* the keys were used last (not the mouse)   */

static bool shown(lv_obj_t *o)
{
    for (; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    }
    return true;
}

static void collect(lv_obj_t *o, lv_obj_t **out, int *n)
{
    uint32_t cnt = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < cnt && *n < MAX_CAND; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        /* something to press: clickable, listening, and not a whole panel */
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_event_count(c) > 0 &&
            lv_obj_get_width(c) < DK_VIEW_W - 40 && lv_obj_get_height(c) < DK_VIEW_H - 40) {
            out[(*n)++] = c;
        }
        collect(c, out, n);
    }
}

static int gather(lv_obj_t **out)
{
    int n = 0;
    collect(lv_screen_active(), out, &n);
    return n;
}

static void centre(lv_obj_t *o, int *x, int *y)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
}

static bool valid(lv_obj_t *o, lv_obj_t **cand, int n)
{
    if (!o) return false;
    for (int i = 0; i < n; i++) {
        if (cand[i] == o) return shown(o);
    }
    return false;
}

/* the nearest to the middle of the screen, the first in reading order on a tie */
static lv_obj_t *pick_default(lv_obj_t **cand, int n)
{
    lv_obj_t *best = NULL;
    long bd = 0;
    for (int i = 0; i < n; i++) {
        int x, y;
        centre(cand[i], &x, &y);
        long dx = x - DK_VIEW_W / 2, dy = y - DK_VIEW_H / 2;
        long d = dx * dx + dy * dy;
        if (!best || d < bd - 64) {
            best = cand[i];
            bd = d;
        }
    }
    return best;
}

static void focus(lv_obj_t *o)
{
    s_focus = o;
    if (o) lv_obj_scroll_to_view_recursive(o, LV_ANIM_ON);
}

/* dir: 0 up, 1 right, 2 down, 3 left (the screen's) */
bool dk_nav_dir(int dir)
{
    lv_obj_t *cand[MAX_CAND];
    int n = gather(cand);
    if (!n) return false;
    s_active = true;
    if (!valid(s_focus, cand, n)) {
        focus(pick_default(cand, n));
        return true;
    }
    int fx, fy;
    centre(s_focus, &fx, &fy);
    lv_obj_t *best = NULL;
    long bs = 0;
    for (int i = 0; i < n; i++) {
        if (cand[i] == s_focus) continue;
        int x, y;
        centre(cand[i], &x, &y);
        long dx = x - fx, dy = y - fy;
        long along = dir == 0 ? -dy : dir == 1 ? dx : dir == 2 ? dy : -dx;
        long across = dir == 0 || dir == 2 ? labs(dx) : labs(dy);
        if (along <= 4) continue;
        long score = along + across * 5 / 2;
        if (!best || score < bs) {
            best = cand[i];
            bs = score;
        }
    }
    if (best) focus(best);
    return true;
}

bool dk_nav_enter(void)
{
    lv_obj_t *cand[MAX_CAND];
    int n = gather(cand);
    if (!n) return false;
    if (!valid(s_focus, cand, n)) {
        s_active = true;
        focus(pick_default(cand, n));
        return true;
    }
    lv_obj_t *o = s_focus;
    lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    return true;
}

void dk_nav_mouse(void)
{
    s_active = false;
}

/* the ring follows its object (the map scrolls) and a new panel gets a
 * focus of its own */
void dk_nav_tick(void)
{
    if (!s_ring) {
        s_ring = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_ring);
        lv_obj_set_style_border_color(s_ring, lv_color_hex(0xFFE070), 0);
        lv_obj_set_style_border_width(s_ring, 3, 0);
        lv_obj_set_style_border_opa(s_ring, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(s_ring, lv_color_hex(0xFFC83A), 0);
        lv_obj_set_style_shadow_width(s_ring, 14, 0);
        lv_obj_set_style_shadow_opa(s_ring, LV_OPA_60, 0);
        lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *cand[MAX_CAND];
    int n = s_active ? gather(cand) : 0;
    if (s_active && n && !valid(s_focus, cand, n)) focus(pick_default(cand, n));
    if (!s_active || !n || !valid(s_focus, cand, n)) {
        if (!lv_obj_has_flag(s_ring, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(s_focus, &a);
    int r = lv_obj_get_style_radius(s_focus, 0);
    int x = a.x1 - 4, y = a.y1 - 4, w = lv_area_get_width(&a) + 8, h = lv_area_get_height(&a) + 8;
    if (lv_obj_get_x(s_ring) != x || lv_obj_get_y(s_ring) != y || lv_obj_get_width(s_ring) != w ||
        lv_obj_get_height(s_ring) != h) {
        lv_obj_set_pos(s_ring, x, y);
        lv_obj_set_size(s_ring, w, h);
        lv_obj_set_style_radius(s_ring, r > 0 ? r + 4 : 4, 0);
    }
    if (lv_obj_has_flag(s_ring, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
}
