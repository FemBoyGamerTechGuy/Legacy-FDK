#define _POSIX_C_SOURCE 199309L
/*
 * core.c — FDK initialisation, shutdown, and public API dispatch
 */
#include "core_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Globals ────────────────────────────────────────────────────────────── */
const FDK_PlatformVTable *fdk__platform  = NULL;
bool fdk__client_side_resize = false;
const FDK_RenderVTable   *fdk__render    = NULL;

/* ─── Build feature string ──────────────────────────────────────────────────
 *
 * Assembled at first call from the FDK_HAVE_* compile-time macros.
 * Tokens are space-separated; see fdk_get_features() docs in fdk.h.
 */
static char g_features[256] = {0};

static void build_features_string(void)
{
    if (g_features[0]) return;  /* already built */
    char *p = g_features;
    char *end = g_features + sizeof(g_features) - 1;
#define APPEND(tok) do { \
    int n = snprintf(p, (size_t)(end - p), "%s%s", (p == g_features) ? "" : " ", tok); \
    if (n < 0 || p + n >= end) break; \
    p += n; \
} while (0)
#ifdef FDK_HAVE_X11
    APPEND("x11");
#endif
#ifdef FDK_HAVE_WAYLAND
    APPEND("wayland");
#endif
#ifdef FDK_HAVE_OPENGL
    APPEND("opengl");
#endif
#ifdef FDK_HAVE_HARFBUZZ
    APPEND("harfbuzz");
#endif
#ifdef FDK_WITH_STB_IMAGE
    APPEND("stb_image");
#endif
#ifdef FDK_HAVE_TEXT_INPUT
    APPEND("text-input-v3");
#endif
#ifdef FDK_HAVE_XCURSOR
    APPEND("xcursor");
#endif
#ifdef FDK_HAVE_XDG_DECORATION
    APPEND("xdg-decoration");
#endif
#ifdef FDK_HAVE_TOPLEVEL_ICON
    APPEND("xdg-toplevel-icon");
#endif
#undef APPEND
    *end = '\0';
}

const char *fdk_get_features(void)
{
    build_features_string();
    return g_features;
}

bool fdk_has_feature(const char *name)
{
    if (!name || !*name) return false;
    build_features_string();
    /* Match whole tokens, case-sensitive. Skip the leading-space check
     * by always scanning with a space-or-start boundary. */
    const char *s = g_features;
    size_t name_len = strlen(name);
    while (*s) {
        /* skip leading spaces */
        while (*s == ' ') s++;
        if (!*s) break;
        /* find end of this token */
        const char *tok_end = s;
        while (*tok_end && *tok_end != ' ') tok_end++;
        size_t tok_len = (size_t)(tok_end - s);
        if (tok_len == name_len && strncmp(s, name, name_len) == 0)
            return true;
        s = tok_end;
    }
    return false;
}

/* ─── Platform detection ─────────────────────────────────────────────────── */
static bool env_set(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0';
}

static const FDK_PlatformVTable *detect_platform(FDK_PlatformBackend hint)
{
    /* Explicit hint always wins */
#ifdef FDK_HAVE_WAYLAND
    if (hint == FDK_PLATFORM_WAYLAND)
        return &fdk_platform_wayland;
#endif
#ifdef FDK_HAVE_X11
    if (hint == FDK_PLATFORM_X11)
        return &fdk_platform_x11;
#endif

    /* AUTO: prefer Wayland when WAYLAND_DISPLAY is set AND non-empty.
     * If WAYLAND_DISPLAY is unset or empty but DISPLAY is set, use X11.
     * This means WAYLAND_DISPLAY="" DISPLAY=:1 ./app correctly picks X11. */
    if (hint == FDK_PLATFORM_AUTO) {
#ifdef FDK_HAVE_WAYLAND
        if (env_set("WAYLAND_DISPLAY"))
            return &fdk_platform_wayland;
#endif
#ifdef FDK_HAVE_X11
        if (env_set("DISPLAY"))
            return &fdk_platform_x11;
#endif
        /* Last resort — try whatever is compiled in */
#ifdef FDK_HAVE_WAYLAND
        return &fdk_platform_wayland;
#endif
#ifdef FDK_HAVE_X11
        return &fdk_platform_x11;
#endif
    }

    return NULL;
}

/* ─── Render backend detection ───────────────────────────────────────────── */
static const FDK_RenderVTable *detect_render(FDK_RenderBackend hint)
{
    if (hint == FDK_RENDER_SOFTWARE)
        return &fdk_render_software;
#ifdef FDK_HAVE_OPENGL
    if (hint == FDK_RENDER_OPENGL || hint == FDK_RENDER_AUTO)
        return &fdk_render_opengl;
#endif
    return &fdk_render_software;
}

/* ─── fdk_init ───────────────────────────────────────────────────────────── */
static char g_app_name[128] = "";

const char *fdk__get_app_name(void) { return g_app_name; }

bool fdk_init(const FDK_InitInfo *info)
{
    FDK_InitInfo defaults = {
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_AUTO,
        .app_name = "FDK Application",
    };
    if (!info) info = &defaults;

    snprintf(g_app_name, sizeof g_app_name, "%s",
             info->app_name ? info->app_name : "");

    fdk__platform = detect_platform(info->platform);
    if (!fdk__platform) {
        fprintf(stderr, "[FDK] No platform backend available.\n");
        return false;
    }
    /* X11 supports client-side resize (XResizeWindow gives live updates).
     * Wayland does not — compositor controls resize via xdg_toplevel_resize. */
    fdk__client_side_resize = (strcmp(fdk__platform->name, "X11") == 0);
    if (!fdk__platform->init()) {
        fprintf(stderr, "[FDK] Platform '%s' init failed.\n", fdk__platform->name);
        return false;
    }

    fdk__render = detect_render(info->render);
    fprintf(stderr, "[FDK] Platform: %s  Render: %s\n",
            fdk__platform->name, fdk__render->name);
    return true;
}

/* ─── fdk_shutdown ───────────────────────────────────────────────────────── */
void fdk_shutdown(void)
{
    if (fdk__platform) fdk__platform->shutdown();
    fdk__platform = NULL;
    fdk__render   = NULL;
}

/* ─── Window ─────────────────────────────────────────────────────────────── */
FDK_Window *fdk_window_create(const FDK_WindowDesc *desc)
{
    if (!fdk__platform || !fdk__render) return NULL;

    FDK_Window *win = calloc(1, sizeof *win);
    if (!win) return NULL;

    win->pw = fdk__platform->window_create(desc);
    if (!win->pw) { free(win); return NULL; }

    win->rctx    = fdk__render->ctx_create(win);

    if (fdk__platform->window_register)
        fdk__platform->window_register(win->pw, win);

    /* Apply CSD opt-in if requested — ask the WM/compositor not to draw
     * its own decorations. Done after register so backends that need
     * win->pw (all of them) are set up. */
    if (desc->csd && fdk__platform->window_set_decorated)
        fdk__platform->window_set_decorated(win->pw, false);

    return win;
}

void fdk_window_destroy(FDK_Window *win)
{
    if (!win) return;
    if (win->rctx) fdk__render->ctx_destroy(win->rctx);
    if (win->pw)   fdk__platform->window_destroy(win->pw);
    free(win);
}

void     fdk_window_show(FDK_Window *win)         { if (win) fdk__platform->window_show(win->pw); }
void     fdk_window_hide(FDK_Window *win)         { if (win) fdk__platform->window_hide(win->pw); }
void     fdk_window_set_title(FDK_Window *win, const char *t) { if(win) fdk__platform->window_set_title(win->pw,t); }
void     fdk_window_request_redraw(FDK_Window *w) { if (w)   fdk__platform->window_request_redraw(w->pw); }

void fdk_window_set_maximized(FDK_Window *win, bool m)  { if (win) fdk__platform->window_set_maximized(win->pw, m); }
void fdk_window_set_fullscreen(FDK_Window *win, bool f) { if (win) fdk__platform->window_set_fullscreen(win->pw, f); }
void fdk_window_set_icon_name(FDK_Window *win, const char *n) { if (win) fdk__platform->window_set_icon_name(win->pw, n); }
void fdk_window_set_icon_from_file(FDK_Window *win, const char *path)
{
    if (win && fdk__platform->window_set_icon_file)
        fdk__platform->window_set_icon_file(win->pw, path);
}

void fdk_window_minimize(FDK_Window *win)
{
    if (win && fdk__platform->window_minimize)
        fdk__platform->window_minimize(win->pw);
}

void fdk_window_begin_move(FDK_Window *win, const FDK_Event *ev)
{
    if (win && ev && fdk__platform->window_begin_move)
        fdk__platform->window_begin_move(win->pw, ev);
}

void fdk_window_begin_resize(FDK_Window *win, const FDK_Event *ev, int edge)
{
    if (win && ev && fdk__platform->window_begin_resize)
        fdk__platform->window_begin_resize(win->pw, ev, edge);
}

void fdk_window_resize(FDK_Window *win, int w, int h)
{
    if (win && fdk__platform->window_resize)
        fdk__platform->window_resize(win->pw, w, h);
}

void fdk_window_move_resize(FDK_Window *win, int x, int y, int w, int h)
{
    if (win && fdk__platform->window_move_resize)
        fdk__platform->window_move_resize(win->pw, x, y, w, h);
}

void fdk__get_root_mouse(int *rx, int *ry)
{
    if (fdk__platform && fdk__platform->get_root_mouse)
        fdk__platform->get_root_mouse(rx, ry);
    else { *rx = 0; *ry = 0; }
}

void fdk_window_set_drop_handler(FDK_Window *win, FDK_DropCb cb, void *ud)
{
    if (win && fdk__platform->window_set_drop_handler)
        fdk__platform->window_set_drop_handler(win->pw, cb, ud);
}

bool fdk_window_is_maximized(FDK_Window *win)
{
    if (!win) return false;
    return fdk__platform->window_is_maximized(win->pw);
}
bool fdk_window_is_fullscreen(FDK_Window *win)
{
    if (!win) return false;
    return fdk__platform->window_is_fullscreen(win->pw);
}

FDK_Size fdk_window_get_size(FDK_Window *win)
{
    if (!win) return (FDK_Size){0,0};
    return fdk__platform->window_get_size(win->pw);
}

float fdk_window_get_scale(FDK_Window *win)
{
    if (!win) return 1.0f;
    if (fdk__platform->window_get_scale)
        return fdk__platform->window_get_scale(win->pw);
    return 1.0f;
}

int fdk_window_get_dpi(FDK_Window *win)
{
    return (int)(fdk_window_get_scale(win) * 96.0f + 0.5f);
}

/* Accessors used by render backends */
FDK_PlatformWindow *fdk_window_get_pw(FDK_Window *win)   { return win->pw; }

/* ─── Events ─────────────────────────────────────────────────────────────── */
bool fdk_poll_event(FDK_Event *out)
{
    if (!fdk__platform) return false;
    return fdk__platform->poll_event(out);
}

void fdk_wait_event(FDK_Event *out)
{
    if (fdk__platform) fdk__platform->wait_event(out);
}

bool fdk_wait_event_timeout(FDK_Event *out, int timeout_ms)
{
    if (!fdk__platform) return false;
    if (fdk__platform->wait_event_timeout)
        return fdk__platform->wait_event_timeout(out, timeout_ms);
    /* Fallback: poll with sleep */
    if (fdk__platform->poll_event(out)) return true;
    if (timeout_ms == 0) return false;
    fdk_sleep_ms(timeout_ms < 0 ? 16 : (uint32_t)timeout_ms);
    return fdk__platform->poll_event(out);
}

/* ─── Frame / draw dispatch ──────────────────────────────────────────────── */
static FDK_Window *s_current_win = NULL;

void fdk_begin_frame(FDK_Window *win)
{
    s_current_win = win;
    FDK_Size sz = fdk_window_get_size(win);
    win->w = sz.w; win->h = sz.h;
    fdk__render->begin_frame(win->rctx, sz.w, sz.h);
}

void fdk_end_frame(FDK_Window *win)
{
    fdk__render->end_frame(win->rctx);
    s_current_win = NULL;
}

#define CTX (s_current_win ? s_current_win->rctx : NULL)

void fdk_clear(FDK_Color c)                                { if(CTX) fdk__render->clear(CTX,c); }
void fdk_fill_rect(FDK_Rect r, FDK_Color c)                { if(CTX) fdk__render->fill_rect(CTX,r,c); }
void fdk_stroke_rect(FDK_Rect r, FDK_Color c, int t)       { if(CTX) fdk__render->stroke_rect(CTX,r,c,t); }
void fdk_fill_rect_rounded(FDK_Rect r, int rd, FDK_Color c){ if(CTX) fdk__render->fill_rect_rounded(CTX,r,rd,c); }
void fdk_fill_rect_gradient(FDK_Rect r, int rd, const FDK_Gradient *g)
{
    if (!CTX) return;
    if (fdk__render->fill_rect_gradient)
        fdk__render->fill_rect_gradient(CTX, r, rd, g);
    else if (g && g->stop_count > 0)
        fdk__render->fill_rect_rounded(CTX, r, rd, g->stops[0].color);
}
void fdk_draw_shadow(FDK_Rect r, int rd, const FDK_Shadow *s)
{
    if (!CTX || !s || !s->enabled) return;
    if (fdk__render->draw_shadow)
        fdk__render->draw_shadow(CTX, r, rd, s);
}
void fdk_fill_circle(int cx,int cy,int rad,FDK_Color c)    { if(CTX) fdk__render->fill_circle(CTX,cx,cy,rad,c); }
void fdk_draw_line(int x0,int y0,int x1,int y1,FDK_Color c,int t){ if(CTX) fdk__render->draw_line(CTX,x0,y0,x1,y1,c,t); }
void fdk_draw_text(FDK_Font *f,const char *s,int x,int y,FDK_Color c){ if(CTX) fdk__render->draw_text(CTX,f,s,x,y,c); }
void fdk_push_clip(FDK_Rect r)                             { if(CTX) fdk__render->push_clip(CTX,r); }
void fdk_pop_clip(void)                                    { if(CTX) fdk__render->pop_clip(CTX); }

/* ─── Utilities ──────────────────────────────────────────────────────────── */
uint64_t fdk_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void fdk_set_cursor(FDK_Cursor cursor)
{
    if (fdk__platform && fdk__platform->set_cursor)
        fdk__platform->set_cursor(cursor);
}

void fdk_clipboard_set(const char *text)
{
    if (fdk__platform && fdk__platform->clipboard_set)
        fdk__platform->clipboard_set(text);
}

char *fdk_clipboard_get(void)
{
    if (fdk__platform && fdk__platform->clipboard_get)
        return fdk__platform->clipboard_get();
    return NULL;
}

void fdk_sleep_ms(uint32_t ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
}
