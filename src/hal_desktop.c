/*
 * MONSTER HOP for the desktop - the watch's HAL, the part the game uses
 *
 * The game (apps/monsterhop in AmoledOS) talks to the watch through about
 * forty aos_hal_* calls. Here they are over SDL: a thread for the worker, an
 * audio queue for the speaker, a text file for the preferences. The radio
 * link has no desktop counterpart yet: every link call says "not here", so
 * the game never offers the watch-to-watch race.
 */
#include "desktop.h"

#include "aos_hal.h"

#include <SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- paths ---- */

static char s_res[1024];        /* monsterhop.pak, read only                   */
static char s_user[1024];       /* prefs.txt, log.txt                          */
static char s_lang[1100];

static void strip_slash(char *p)
{
    size_t n = strlen(p);
    while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\')) p[--n] = 0;
}

void dk_paths_init(void)
{
    const char *env = getenv("MH_DATA");
    char *base = SDL_GetBasePath();
    if (env && env[0]) snprintf(s_res, sizeof s_res, "%s", env);
    else if (base) snprintf(s_res, sizeof s_res, "%s", base);
    else snprintf(s_res, sizeof s_res, ".");
    SDL_free(base);
    strip_slash(s_res);
    /* MH_USER: another place for prefs.txt (screenshots, tests) */
    const char *uenv = getenv("MH_USER");
    char *pref = uenv && uenv[0] ? NULL : SDL_GetPrefPath("", "MonsterHop");
    if (uenv && uenv[0]) snprintf(s_user, sizeof s_user, "%s", uenv);
    else if (pref) snprintf(s_user, sizeof s_user, "%s", pref);
    else snprintf(s_user, sizeof s_user, "%s", s_res);
    SDL_free(pref);
    strip_slash(s_user);
    snprintf(s_lang, sizeof s_lang, "%s/lang", s_res);
}

const char *dk_user_dir(void) { return s_user; }
const char *aos_hal_path_apps(void) { return s_res; }
const char *aos_hal_path_lang(void) { return s_lang; }

/* ---- log ---- */

static FILE *s_logf;

void aos_hal_log(const char *tag, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    unsigned ms = (unsigned)SDL_GetTicks64();
    fprintf(stderr, "[%6u.%03u] %s: %s\n", ms / 1000, ms % 1000, tag, line);
    if (!s_logf && s_user[0]) {
        char path[1100];
        snprintf(path, sizeof path, "%s/log.txt", s_user);
        s_logf = fopen(path, "w");
    }
    if (s_logf) {
        fprintf(s_logf, "[%6u.%03u] %s: %s\n", ms / 1000, ms % 1000, tag, line);
        fflush(s_logf);
    }
}

/* ---- time and memory ---- */

uint64_t aos_hal_uptime_ms(void)
{
    return SDL_GetTicks64();
}

void aos_hal_heap_info(uint32_t *free_internal, uint32_t *free_psram)
{
    /* the game sizes its spare frame and its logs by this: plenty */
    if (free_internal) *free_internal = 256u * 1024u;
    if (free_psram) *free_psram = 64u * 1024u * 1024u;
}

/* ---- preferences: key=value lines in prefs.txt, written at most once a
 * second (a save sets dozens of keys) and when the game closes ---- */

#define PREF_MAX 512
typedef struct {
    char key[32];
    char val[96];
} pref_t;

static pref_t s_pref[PREF_MAX];
static int s_npref;
static bool s_pref_dirty;
static uint64_t s_pref_dirty_ms;
static SDL_mutex *s_pref_mx;

static void pref_path(char *out, size_t n)
{
    snprintf(out, n, "%s/prefs.txt", s_user);
}

void dk_prefs_load(void)
{
    if (!s_pref_mx) s_pref_mx = SDL_CreateMutex();
    char path[1100];
    pref_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f) && s_npref < PREF_MAX) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        snprintf(s_pref[s_npref].key, sizeof s_pref[0].key, "%s", line);
        snprintf(s_pref[s_npref].val, sizeof s_pref[0].val, "%s", v);
        s_npref++;
    }
    fclose(f);
}

void dk_prefs_flush(bool now)
{
    SDL_LockMutex(s_pref_mx);
    if (s_pref_dirty && (now || SDL_GetTicks64() - s_pref_dirty_ms > 1000)) {
        char path[1100], tmp[1110];
        pref_path(path, sizeof path);
        snprintf(tmp, sizeof tmp, "%s.new", path);
        FILE *f = fopen(tmp, "w");
        if (f) {
            for (int i = 0; i < s_npref; i++) fprintf(f, "%s=%s\n", s_pref[i].key, s_pref[i].val);
            bool ok = fclose(f) == 0;
            /* a whole file or the old one: never half of it */
            if (ok) {
                remove(path);
                ok = rename(tmp, path) == 0;
            }
            if (ok) s_pref_dirty = false;
        }
    }
    SDL_UnlockMutex(s_pref_mx);
}

static pref_t *pref_find(const char *key)
{
    for (int i = 0; i < s_npref; i++) {
        if (!strcmp(s_pref[i].key, key)) return &s_pref[i];
    }
    return NULL;
}

bool aos_hal_pref_get_str(const char *key, char *out, size_t out_len)
{
    SDL_LockMutex(s_pref_mx);
    pref_t *p = pref_find(key);
    if (p && out && out_len) snprintf(out, out_len, "%s", p->val);
    SDL_UnlockMutex(s_pref_mx);
    return p != NULL;
}

bool aos_hal_pref_set_str(const char *key, const char *value)
{
    if (!key || strlen(key) >= sizeof s_pref[0].key) return false;
    SDL_LockMutex(s_pref_mx);
    pref_t *p = pref_find(key);
    if (!p && s_npref < PREF_MAX) {
        p = &s_pref[s_npref++];
        snprintf(p->key, sizeof p->key, "%s", key);
        p->val[0] = 0;
    }
    if (p && strcmp(p->val, value ? value : "")) {
        snprintf(p->val, sizeof p->val, "%s", value ? value : "");
        if (!s_pref_dirty) s_pref_dirty_ms = SDL_GetTicks64();
        s_pref_dirty = true;
    }
    SDL_UnlockMutex(s_pref_mx);
    return p != NULL;
}

bool aos_hal_pref_get_i32(const char *key, int32_t *out)
{
    char b[32];
    if (!aos_hal_pref_get_str(key, b, sizeof b)) return false;
    if (out) *out = (int32_t)strtol(b, NULL, 10);
    return true;
}

bool aos_hal_pref_set_i32(const char *key, int32_t value)
{
    char b[16];
    snprintf(b, sizeof b, "%ld", (long)value);
    return aos_hal_pref_set_str(key, b);
}

/* ---- the worker: a thread ---- */

static SDL_Thread *s_worker;
static SDL_atomic_t s_stop;
static aos_worker_fn_t s_fn;
static void *s_arg;

static int worker_main(void *arg)
{
    (void)arg;
    s_fn(s_arg);
    return 0;
}

bool aos_hal_worker_start_on(const char *name, aos_worker_fn_t fn, void *arg, uint32_t stack_bytes, int core,
                             int prio)
{
    (void)core;
    (void)prio;
    if (s_worker) return false;
    SDL_AtomicSet(&s_stop, 0);
    s_fn = fn;
    s_arg = arg;
    /* the watch's 12 KB is the S3's budget: a desktop stack is not */
    s_worker = SDL_CreateThreadWithStackSize(worker_main, name, stack_bytes < (1u << 20) ? (1u << 20) : stack_bytes,
                                             NULL);
    return s_worker != NULL;
}

bool aos_hal_worker_start(const char *name, aos_worker_fn_t fn, void *arg, uint32_t stack_bytes)
{
    return aos_hal_worker_start_on(name, fn, arg, stack_bytes, -1, -1);
}

void aos_hal_worker_stop(void)
{
    if (!s_worker) return;
    SDL_AtomicSet(&s_stop, 1);
    SDL_WaitThread(s_worker, NULL);
    s_worker = NULL;
}

bool aos_hal_worker_running(void) { return s_worker != NULL; }
bool aos_hal_worker_should_stop(void) { return SDL_AtomicGet(&s_stop) != 0; }
void aos_hal_worker_sleep(uint32_t ms) { SDL_Delay(ms); }

/* ---- sound ---- */

static SDL_AudioDeviceID s_dev;

bool aos_hal_spk_open(uint32_t sample_rate)
{
    if (s_dev) return true;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_dev) {
        aos_hal_log("audio", "no device: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

int aos_hal_spk_write(const int16_t *pcm, int n)
{
    if (!s_dev || n <= 0) return 0;
    return SDL_QueueAudio(s_dev, pcm, (Uint32)n * 2) == 0 ? n : 0;
}

int aos_hal_spk_queued(void)
{
    return s_dev ? (int)(SDL_GetQueuedAudioSize(s_dev) / 2) : 0;
}

void aos_hal_spk_close(void)
{
    if (!s_dev) return;
    SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
}

/* only used when there is no speaker at all */
void aos_hal_beep(int freq_hz, int ms)
{
    (void)freq_hz;
    (void)ms;
}

/* ---- the panel: there is none, the game draws through LVGL's canvas ---- */

bool aos_hal_display_blit(int x, int y, int w, int h, const void *rgb565_be)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)rgb565_be;
    return false;
}

/* ---- the radio link: not on the desktop ---- */

bool aos_hal_link_start(void) { return false; }
void aos_hal_link_stop(void) {}
bool aos_hal_link_running(void) { return false; }
int aos_hal_link_recv(aos_link_frame_t *out) { (void)out; return 0; }
bool aos_hal_link_stats(aos_link_stats_t *out) { if (out) memset(out, 0, sizeof *out); return false; }
void aos_hal_link_offer(const char *app) { (void)app; }
int aos_hal_link_neighbours(aos_link_neighbour_t *out, int max) { (void)out; (void)max; return 0; }
bool aos_hal_link_partner(aos_link_partner_t *out) { if (out) memset(out, 0, sizeof *out); return false; }
bool aos_hal_link_send_partner(const void *data, size_t len) { (void)data; (void)len; return false; }
bool aos_hal_link_send_reliable(const void *data, size_t len) { (void)data; (void)len; return false; }
int aos_hal_link_recv_reliable(aos_link_frame_t *out) { (void)out; return 0; }
bool aos_hal_link_reliable_lost(void) { return false; }
void aos_hal_link_reliable_reset(void) {}
