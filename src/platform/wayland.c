#define _GNU_SOURCE
/*
 * wayland.c — FDK Wayland platform backend
 *
 * Keyboard translation uses xkbcommon (MIT licensed, separate from FDK's license) — loads the
 * compositor-provided keymap so every locale/layout works correctly.
 *
 * Event loop design:
 *   wl_wait_event  — blocks with wl_display_dispatch() (reads + dispatches)
 *   wl_poll_event  — non-blocking: dispatch_pending only, no socket read
 *   Both push translated events into the evq ring buffer.
 */
#include "platform_internal.h"
#include "../core/core_internal.h"

#ifdef FDK_HAVE_WAYLAND

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-client-protocol.h"

#include <xkbcommon/xkbcommon.h>
#include <wayland-cursor.h>

#ifdef FDK_HAVE_OPENGL
#  include <EGL/egl.h>
#  include <EGL/eglext.h>
#  include <wayland-egl.h>
#endif

#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

/* ─── Platform-private window ─────────────────────────────────────────────── */
struct FDK_PlatformWindow {
    struct wl_surface    *surface;
    struct xdg_surface   *xdg_surface;
    struct xdg_toplevel  *xdg_toplevel;
#ifdef FDK_HAVE_XDG_DECORATION
    /* Per-toplevel decoration object — created by the decoration
     * manager, used to tell the compositor "I draw my own title bar,
     * don't draw yours" (CLIENT_SIDE) or "you draw it" (SERVER_SIDE).
     * NULL when the compositor doesn't support xdg-decoration. */
    struct zxdg_toplevel_decoration_v1 *xdg_decoration;
#endif

    /* Double-buffered SHM — back is what we draw into,
     * front is what the compositor is currently displaying */
    struct wl_shm_pool   *shm_pool;       /* single pool covering both bufs */
    struct wl_buffer     *shm_buf[2];     /* [0] and [1] */
    uint32_t             *shm_pixels[2];  /* mapped pixel pointers          */
    int                   shm_back;       /* index of back buffer (0 or 1)  */
    bool                  shm_buf_busy[2];/* true while compositor holds it */
    int                   shm_fd;
    size_t                shm_size;       /* size of ONE buffer              */
    size_t                shm_pool_size;  /* total pool size (2x shm_size)  */

    int                   w, h;
    int                   stride_px;
    bool                  configured;
    bool                  close_requested;
    /* Last state reported by xdg_toplevel::configure's states array.
     * "activated" is the protocol's term for "this toplevel currently
     * has keyboard focus" — distinct from FDK's own widget-level
     * "focused" concept used elsewhere in the codebase. */
    bool                  maximized;
    bool                  fullscreen;
    bool                  activated;
#ifdef FDK_HAVE_OPENGL
    struct wl_egl_window *egl_win;
    EGLSurface            egl_surface;
    EGLContext            egl_ctx;
#endif
    /* Drag-and-drop callback (NULL = no drop handling) */
    FDK_DropCb            drop_cb;
    void                 *drop_ud;
};

/* ─── Global Wayland state ─────────────────────────────────────────────────── */
static struct wl_display    *s_dpy        = NULL;
static struct wl_registry   *s_registry   = NULL;
static struct wl_compositor *s_compositor = NULL;
static struct wl_shm        *s_shm        = NULL;
static struct xdg_wm_base   *s_xdg_wm    = NULL;
static struct wl_seat        *s_seat      = NULL;

#ifdef FDK_HAVE_TOPLEVEL_ICON
#include "xdg-toplevel-icon-v1-client-protocol.h"
static struct xdg_toplevel_icon_manager_v1 *s_icon_mgr = NULL;
#endif

#ifdef FDK_WITH_STB_IMAGE
/* stb_image implementation is compiled in src/platform/x11.c.
 * Declare the functions we need here without including the header
 * to avoid duplicate symbol issues. */
extern unsigned char *stbi_load(const char *filename, int *x, int *y,
                                int *channels_in_file, int desired_channels);
extern void stbi_image_free(void *retval_from_stbi_load);
#endif
#ifdef FDK_HAVE_XDG_DECORATION
#include "xdg-decoration-unstable-v1-client-protocol.h"
static struct zxdg_decoration_manager_v1 *s_decoration_mgr = NULL;
#endif
#ifdef FDK_HAVE_TEXT_INPUT
#include "text-input-unstable-v3-client-protocol.h"
static struct zwp_text_input_manager_v3 *s_text_input_mgr = NULL;
static struct zwp_text_input_v3          *s_text_input     = NULL;
#endif

/* ─── Output (for HiDPI scale) ────────────────────────────────────────────── */
static struct wl_output *s_output = NULL;
static int s_output_scale = 1;  /* compositor-reported scale factor */

static void output_geometry(void *d, struct wl_output *o,
    int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t subpixel,
    const char *make, const char *model, int32_t transform)
{ (void)d;(void)o;(void)x;(void)y;(void)pw;(void)ph;(void)subpixel;(void)make;(void)model;(void)transform; }

static void output_mode(void *d, struct wl_output *o, uint32_t flags,
    int32_t w, int32_t h, int32_t refresh)
{ (void)d;(void)o;(void)flags;(void)w;(void)h;(void)refresh; }

static void output_done(void *d, struct wl_output *o)
{ (void)d;(void)o; }

static void output_scale_cb(void *d, struct wl_output *o, int32_t scale)
{
    (void)d;(void)o;
    s_output_scale = scale > 0 ? scale : 1;
    fprintf(stderr, "[FDK/Wayland] Output scale: %d\n", s_output_scale);
}

static const struct wl_output_listener s_output_listener = {
    .geometry = output_geometry,
    .mode     = output_mode,
    .done     = output_done,
    .scale    = output_scale_cb,
};

static struct wl_pointer     *s_pointer   = NULL;
static struct wl_keyboard    *s_keyboard  = NULL;
/* Surface → FDK_Window lookup — supports up to 32 windows */
#define FDK_MAX_WINDOWS 32
typedef struct { struct wl_surface *surface; FDK_Window *fdkw; } WinEntry;
static WinEntry s_windows[FDK_MAX_WINDOWS];
static int      s_win_count = 0;
static FDK_Window *s_last_fdkwin = NULL; /* convenience: most recently created */

static void win_register(struct wl_surface *surf, FDK_Window *fdkw)
{
    if (s_win_count < FDK_MAX_WINDOWS) {
        s_windows[s_win_count].surface = surf;
        s_windows[s_win_count].fdkw    = fdkw;
        s_win_count++;
    }
    s_last_fdkwin = fdkw;
}

static void win_unregister(struct wl_surface *surf)
{
    for (int i = 0; i < s_win_count; i++) {
        if (s_windows[i].surface == surf) {
            s_windows[i] = s_windows[--s_win_count];
            return;
        }
    }
}

static FDK_Window *win_lookup(struct wl_surface *surf)
{
    for (int i = 0; i < s_win_count; i++)
        if (s_windows[i].surface == surf)
            return s_windows[i].fdkw;
    return s_last_fdkwin; /* fallback for events without surface context */
}

/* ─── XKB state ────────────────────────────────────────────────────────────── */
static struct xkb_context *s_xkb_ctx   = NULL;
static struct xkb_keymap  *s_xkb_map   = NULL;
static struct xkb_state   *s_xkb_state = NULL;

/* ─── Cursor ───────────────────────────────────────────────────────────────── */
static struct wl_cursor_theme  *s_cursor_theme  = NULL;
static struct wl_surface       *s_cursor_surface = NULL;

/* Load a named cursor and set it on the pointer */
static void set_cursor(struct wl_pointer *ptr, uint32_t serial,
                       const char *name)
{
    if (!s_cursor_theme || !ptr) return;
    struct wl_cursor *cur = wl_cursor_theme_get_cursor(s_cursor_theme, name);
    if (!cur || cur->image_count == 0) return;
    struct wl_cursor_image  *img = cur->images[0];
    struct wl_buffer        *buf = wl_cursor_image_get_buffer(img);
    if (!buf) return;
    wl_pointer_set_cursor(ptr, serial, s_cursor_surface,
                          img->hotspot_x, img->hotspot_y);
    wl_surface_attach(s_cursor_surface, buf, 0, 0);
    wl_surface_damage(s_cursor_surface, 0, 0, img->width, img->height);
    wl_surface_commit(s_cursor_surface);
}

/* ─── Clipboard ────────────────────────────────────────────────────────────── */
static struct wl_data_device_manager *s_data_mgr    = NULL;
static struct wl_data_device         *s_data_device = NULL;
static struct wl_data_source         *s_data_src    = NULL;
static char                          *s_clipboard    = NULL; /* owned copy  */

/* data_source listeners */
static void ds_target(void *d, struct wl_data_source *src, const char *mime) { (void)d;(void)src;(void)mime; }
static void ds_send(void *d, struct wl_data_source *src, const char *mime, int32_t fd)
{
    (void)d;(void)src;(void)mime;
    if (s_clipboard) {
        size_t len = strlen(s_clipboard);
        size_t written = 0;
        while (written < len) {
            ssize_t n = write(fd, s_clipboard + written, len - written);
            if (n <= 0) break;
            written += n;
        }
    }
    close(fd);
}
static void ds_cancelled(void *d, struct wl_data_source *src)
{
    (void)d;
    wl_data_source_destroy(src);
    if (s_data_src == src) s_data_src = NULL;
}
static void ds_dnd_drop(void *d, struct wl_data_source *s) { (void)d;(void)s; }
static void ds_dnd_finished(void *d, struct wl_data_source *s) { (void)d;(void)s; }
static void ds_action(void *d, struct wl_data_source *s, uint32_t a) { (void)d;(void)s;(void)a; }
static const struct wl_data_source_listener s_ds_listener = {
    .target        = ds_target,
    .send          = ds_send,
    .cancelled     = ds_cancelled,
    .dnd_drop_performed = ds_dnd_drop,
    .dnd_finished  = ds_dnd_finished,
    .action        = ds_action,
};

/* data_offer (incoming paste or DnD) */
static struct wl_data_offer *s_paste_offer = NULL;

/* Track whether the current DnD offer supports text/uri-list */
static bool s_dnd_has_uri_list = false;
static uint32_t s_dnd_enter_serial = 0;

static void do_offer(void *d, struct wl_data_offer *o, const char *mime)
{
    (void)d;
    /* Track if this offer supports text/uri-list (for DnD) */
    if (mime && strcmp(mime, "text/uri-list") == 0) {
        s_dnd_has_uri_list = true;
    }
}
static void do_source_actions(void *d, struct wl_data_offer *o, uint32_t a) { (void)d;(void)o;(void)a; }
static void do_action(void *d, struct wl_data_offer *o, uint32_t a) { (void)d;(void)o;(void)a; }
static const struct wl_data_offer_listener s_do_listener = {
    .offer          = do_offer,
    .source_actions = do_source_actions,
    .action         = do_action,
};

/* data_device listeners */
static void dd_data_offer(void *d, struct wl_data_device *dev,
                           struct wl_data_offer *offer)
{
    (void)d;(void)dev;
    /* A new data offer is created (for clipboard or DnD).
     * Reset the URI list flag — it will be set to true if the
     * source advertises text/uri-list via the do_offer callback. */
    s_dnd_has_uri_list = false;
    wl_data_offer_add_listener(offer, &s_do_listener, NULL);
}

/* ── DnD state ──
 * When a drag enters our surface, the compositor sends `enter` with
 * a wl_data_offer. We store it so we can accept the drop later.
 * On `drop`, we call wl_data_offer_receive() to get a fd, read the
 * URI list from it, and pass it to the window's drop callback. */
static struct wl_data_offer *s_dnd_offer = NULL;
static struct wl_surface   *s_dnd_surface = NULL;

static void dd_enter(void *d, struct wl_data_device *dev, uint32_t s,
                     struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
                     struct wl_data_offer *o)
{
    (void)d;(void)dev;(void)x;(void)y;
    s_dnd_offer   = o;
    s_dnd_surface = surf;
    s_dnd_enter_serial = s;

    if (o) {
        /* Set actions: we want copy */
        wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        /* Accept the offer with the serial from the enter event.
         * Without this, the compositor won't send the drop event. */
        if (s_dnd_has_uri_list) {
            wl_data_offer_accept(o, s, "text/uri-list");
        }
    }
}

static void dd_leave(void *d, struct wl_data_device *dev)
{
    (void)d;(void)dev;
    s_dnd_offer   = NULL;
    s_dnd_surface = NULL;
}

static void dd_motion(void *d, struct wl_data_device *dev, uint32_t t,
                      wl_fixed_t x, wl_fixed_t y)
{
    (void)d;(void)dev;(void)t;(void)x;(void)y;
    /* Nothing to do — we already accepted on enter */
}

static void dd_drop(void *d, struct wl_data_device *dev)
{
    (void)d;(void)dev;

    if (!s_dnd_offer || !s_dnd_surface) return;

    /* Find the FDK_Window for this surface */
    FDK_Window *fw = win_lookup(s_dnd_surface);
    if (!fw || !fw->pw->drop_cb) {
        /* No drop handler — reject */
        if (s_dnd_offer) wl_data_offer_destroy(s_dnd_offer);
        s_dnd_offer = NULL;
        return;
    }

    /* Request the data as text/uri-list. wl_data_offer_receive() gives
     * us a file descriptor; the compositor will write the data to it. */
    int fds[2];
    if (pipe(fds) != 0) {
        wl_data_offer_destroy(s_dnd_offer);
        s_dnd_offer = NULL;
        return;
    }

    wl_data_offer_receive(s_dnd_offer, "text/uri-list", fds[1]);
    close(fds[1]);  /* close write end — compositor has its own copy */
    wl_display_flush(s_dpy);

    /* Read the data from the read end. The compositor writes
     * asynchronously, so we may need to wait. */
    char buf[65536];
    ssize_t total = 0;
    /* Set a timeout so we don't block forever if the compositor
     * never sends data. 1 second is plenty. */
    struct pollfd pfd = { fds[0], POLLIN, 0 };
    int pr = poll(&pfd, 1, 1000);
    if (pr > 0 && (pfd.revents & POLLIN)) {
        total = read(fds[0], buf, sizeof(buf) - 1);
    }
    close(fds[0]);

    if (total > 0) {
        buf[total] = '\0';
        /* Notify the compositor that the drop finished */
        wl_data_offer_finish(s_dnd_offer);
        /* Call the app's drop callback */
        fw->pw->drop_cb(fw, buf, fw->pw->drop_ud);
    } else {
        /* No data — just destroy the offer */
    }

    /* wl_data_offer_finish already destroys the offer in most cases,
     * but call destroy to be safe (double-destroy is a no-op in
     * wlroots-based compositors). */
    if (s_dnd_offer) {
        wl_data_offer_destroy(s_dnd_offer);
        s_dnd_offer = NULL;
    }
    s_dnd_surface = NULL;
}

static void dd_selection(void *d, struct wl_data_device *dev,
                          struct wl_data_offer *offer) { (void)d;(void)dev; s_paste_offer = offer; }
static const struct wl_data_device_listener s_dd_listener = {
    .data_offer = dd_data_offer,
    .enter      = dd_enter,
    .leave      = dd_leave,
    .motion     = dd_motion,
    .drop       = dd_drop,
    .selection  = dd_selection,
};

/* Current modifier state */
static FDK_Modifier s_mods = FDK_MOD_NONE;

/* Current pointer position */
static int32_t s_ptr_x = 0, s_ptr_y = 0;

#ifdef FDK_HAVE_OPENGL
static EGLDisplay s_egl_dpy    = EGL_NO_DISPLAY;
static EGLConfig  s_egl_cfg    = NULL;
static EGLContext s_egl_shared = EGL_NO_CONTEXT; /* one context shared by all windows */
#endif

/* ─── Event queue ──────────────────────────────────────────────────────────── */
#define EVQ_SIZE 256
static FDK_Event s_evq[EVQ_SIZE];
static int s_evq_head = 0, s_evq_tail = 0;

static void evq_push_ev(const FDK_Event *ev)
{
    int next = (s_evq_tail + 1) % EVQ_SIZE;
    if (next == s_evq_head) return; /* drop on overflow */
    s_evq[s_evq_tail] = *ev;
    s_evq_tail = next;
}

static bool evq_pop(FDK_Event *out)
{
    if (s_evq_head == s_evq_tail) return false;
    *out = s_evq[s_evq_head];
    s_evq_head = (s_evq_head + 1) % EVQ_SIZE;
    return true;
}

/* ─── xdg_wm_base ping ─────────────────────────────────────────────────────── */
static void xdg_wm_ping(void *d, struct xdg_wm_base *wm, uint32_t serial)
{ (void)d; xdg_wm_base_pong(wm, serial); }

static const struct xdg_wm_base_listener s_xdg_wm_listener = { .ping = xdg_wm_ping };

/* ─── SHM helpers ──────────────────────────────────────────────────────────── */
static int shm_create_anon(size_t size)
{
    int fd = memfd_create("fdk-shm", MFD_CLOEXEC);
    if (fd < 0) { perror("[FDK/Wayland] memfd_create"); return -1; }
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

/* Buffer-release callback — marks the buffer as free for reuse */
static void shm_buffer_release(void *data, struct wl_buffer *buf)
{
    FDK_PlatformWindow *pw = data;
    for (int i = 0; i < 2; i++)
        if (pw->shm_buf[i] == buf)
            pw->shm_buf_busy[i] = false;
}
static const struct wl_buffer_listener s_shm_buf_listener = {
    .release = shm_buffer_release,
};

static bool create_shm_buffers(FDK_PlatformWindow *pw, int w, int h)
{
    pw->stride_px     = w;
    pw->shm_size      = (size_t)w * h * 4;
    pw->shm_pool_size = pw->shm_size * 2;

    pw->shm_fd = shm_create_anon(pw->shm_pool_size);
    if (pw->shm_fd < 0) return false;

    /* Map the entire pool — both buffers are contiguous in this mapping */
    uint8_t *base = mmap(NULL, pw->shm_pool_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED, pw->shm_fd, 0);
    if (base == MAP_FAILED) { close(pw->shm_fd); pw->shm_fd = -1; return false; }

    pw->shm_pixels[0] = (uint32_t *)base;
    pw->shm_pixels[1] = (uint32_t *)(base + pw->shm_size);

    pw->shm_pool = wl_shm_create_pool(s_shm, pw->shm_fd,
                                       (int32_t)pw->shm_pool_size);

    for (int i = 0; i < 2; i++) {
        pw->shm_buf[i] = wl_shm_pool_create_buffer(
            pw->shm_pool,
            (int32_t)(i * pw->shm_size), /* offset into pool */
            w, h, w * 4,
            WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(pw->shm_buf[i], &s_shm_buf_listener, pw);
        pw->shm_buf_busy[i] = false;
    }

    pw->shm_back = 0;
    return true;
}

static void shm_destroy_buffers(FDK_PlatformWindow *pw)
{
    for (int i = 0; i < 2; i++) {
        if (pw->shm_buf[i]) {
            wl_buffer_destroy(pw->shm_buf[i]);
            pw->shm_buf[i] = NULL;
        }
    }
    if (pw->shm_pool) { wl_shm_pool_destroy(pw->shm_pool); pw->shm_pool = NULL; }
    if (pw->shm_pixels[0]) {
        munmap(pw->shm_pixels[0], pw->shm_pool_size);
        pw->shm_pixels[0] = pw->shm_pixels[1] = NULL;
    }
    if (pw->shm_fd >= 0) { close(pw->shm_fd); pw->shm_fd = -1; }
}

static void wl_shm_resize(FDK_PlatformWindow *pw, int w, int h)
{
    if (pw->w == w && pw->h == h) return;
    if (w <= 0 || h <= 0) return;

    pw->w = w;
    pw->h = h;

#ifdef FDK_HAVE_OPENGL
    if (pw->egl_win) {
        wl_egl_window_resize(pw->egl_win, w, h, 0, 0);
        return;
    }
#endif

    shm_destroy_buffers(pw);
    create_shm_buffers(pw, w, h);
}

/* ─── xdg_surface / toplevel listeners ────────────────────────────────────── */
static void xdg_surface_configure(void *data, struct xdg_surface *xs,
                                   uint32_t serial)
{
    FDK_PlatformWindow *pw = data;
    xdg_surface_ack_configure(xs, serial);
    pw->configured = true;
    /* Synthetic expose so the app knows the window is ready to paint */
    FDK_Event ev = {0};
    ev.type   = FDK_EVENT_EXPOSE;
    ev.window = win_lookup(pw->surface);
    evq_push_ev(&ev);
}

static const struct xdg_surface_listener s_xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *top,
                                int32_t w, int32_t h, struct wl_array *states)
{
    (void)top;
    FDK_PlatformWindow *pw = data;

    bool was_max = pw->maximized, was_full = pw->fullscreen, was_act = pw->activated;
    bool now_max = false, now_full = false, now_act = false;
    uint32_t *st;
    wl_array_for_each(st, states) {
        if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED)  now_max  = true;
        if (*st == XDG_TOPLEVEL_STATE_FULLSCREEN) now_full = true;
        if (*st == XDG_TOPLEVEL_STATE_ACTIVATED)  now_act  = true;
    }
    pw->maximized  = now_max;
    pw->fullscreen = now_full;
    pw->activated  = now_act;

    if (w > 0 && h > 0) {
        wl_shm_resize(pw, w, h);
        FDK_Event ev = {0};
        ev.type     = FDK_EVENT_RESIZE;
        ev.window   = win_lookup(pw->surface);
        ev.resize.w = pw->w;
        ev.resize.h = pw->h;
        evq_push_ev(&ev);
    }

    /* configure can re-fire with identical state (e.g. a plain resize
     * while already maximized) — only tell the app something changed
     * when it actually did. */
    if (now_max != was_max || now_full != was_full || now_act != was_act) {
        FDK_Event sev = {0};
        sev.type            = FDK_EVENT_STATE_CHANGE;
        sev.window           = win_lookup(pw->surface);
        sev.state.maximized  = now_max;
        sev.state.fullscreen = now_full;
        sev.state.activated  = now_act;
        evq_push_ev(&sev);
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *top)
{
    (void)top;
    ((FDK_PlatformWindow*)data)->close_requested = true;
}

static const struct xdg_toplevel_listener s_toplevel_listener = {
    .configure = toplevel_configure,
    .close     = toplevel_close,
};

/* ─── Pointer events ───────────────────────────────────────────────────────── */
static struct wl_surface *s_ptr_surface = NULL;
static uint32_t           s_ptr_serial  = 0;

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)d;
    s_ptr_surface = surf;
    s_ptr_serial  = serial;
    s_ptr_x = wl_fixed_to_int(sx);
    s_ptr_y = wl_fixed_to_int(sy);
    set_cursor(p, serial, "default");

    /* Generate a MOUSE_MOVE event so the widget layer updates
     * ui->mouse_x/mouse_y immediately. Without this, if the user
     * scrolls right after entering the window (without moving the
     * mouse first), the scroll handler uses stale (0,0) coordinates. */
    FDK_Event ev = {0};
    ev.type    = FDK_EVENT_MOUSE_MOVE;
    ev.window  = win_lookup(surf);
    ev.motion.x = s_ptr_x;
    ev.motion.y = s_ptr_y;
    evq_push_ev(&ev);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s,
                      struct wl_surface *surf)
{ (void)d;(void)p;(void)s;(void)surf; s_ptr_surface = NULL; }

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t,
                       wl_fixed_t sx, wl_fixed_t sy)
{
    (void)d;(void)p;(void)t;
    s_ptr_x = wl_fixed_to_int(sx);
    s_ptr_y = wl_fixed_to_int(sy);
    FDK_Event ev = {0};
    ev.type      = FDK_EVENT_MOUSE_MOVE;
    ev.window    = win_lookup(s_ptr_surface);
    ev.motion.x  = s_ptr_x;
    ev.motion.y  = s_ptr_y;
    ev.motion.mods = s_mods;
    evq_push_ev(&ev);
}

static uint32_t s_last_serial = 0;
static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial,
                       uint32_t t, uint32_t button, uint32_t state)
{
    (void)d;(void)p;(void)serial;(void)t;
    s_last_serial = serial;
    FDK_MouseButton btn;
    switch (button) {
    case 0x110: btn = FDK_BUTTON_LEFT;   break;
    case 0x111: btn = FDK_BUTTON_RIGHT;  break;
    case 0x112: btn = FDK_BUTTON_MIDDLE; break;
    default: return;
    }
    FDK_Event ev = {0};
    ev.type         = (state == WL_POINTER_BUTTON_STATE_PRESSED)
                          ? FDK_EVENT_MOUSE_DOWN : FDK_EVENT_MOUSE_UP;
    ev.window       = win_lookup(s_ptr_surface);
    ev.mouse.x      = s_ptr_x;
    ev.mouse.y      = s_ptr_y;
    ev.mouse.button = btn;
    ev.mouse.mods   = s_mods;
    evq_push_ev(&ev);
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t,
                     uint32_t axis, wl_fixed_t value)
{
    (void)d;(void)p;(void)t;
    FDK_Event ev = {0};
    ev.type   = FDK_EVENT_MOUSE_SCROLL;
    ev.window = win_lookup(s_ptr_surface);
    /* Don't set ev.mouse.x/y — the FDK_Event union overlaps mouse.x/y
     * with scroll.dx/dy. Setting scroll.dy would corrupt mouse.y.
     * The widget layer uses ui->mouse_x/mouse_y (tracked from
     * MOUSE_MOVE) instead of ev->mouse for scroll events. */
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        ev.scroll.dy = -wl_fixed_to_double(value) / 10.0f;
    else
        ev.scroll.dx = -wl_fixed_to_double(value) / 10.0f;
    evq_push_ev(&ev);
}

static void ptr_frame(void *d, struct wl_pointer *p) { (void)d;(void)p; }
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d;(void)p;(void)s; }
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d;(void)p;(void)t;(void)a; }
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d;(void)p;(void)a;(void)v; }

static const struct wl_pointer_listener s_pointer_listener = {
    .enter         = ptr_enter,
    .leave         = ptr_leave,
    .motion        = ptr_motion,
    .button        = ptr_button,
    .axis          = ptr_axis,
    .frame         = ptr_frame,
    .axis_source   = ptr_axis_source,
    .axis_stop     = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

/* ─── Keyboard events (xkbcommon) ──────────────────────────────────────────── */
static void kbd_keymap(void *d, struct wl_keyboard *k,
                       uint32_t fmt, int32_t fd, uint32_t size)
{
    (void)d;(void)k;
    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }

    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_str == MAP_FAILED) return;

    /* Rebuild xkb state from compositor keymap — handles all layouts/locales */
    if (s_xkb_state) { xkb_state_unref(s_xkb_state); s_xkb_state = NULL; }
    if (s_xkb_map)   { xkb_keymap_unref(s_xkb_map);  s_xkb_map   = NULL; }

    s_xkb_map = xkb_keymap_new_from_string(s_xkb_ctx, map_str,
                    XKB_KEYMAP_FORMAT_TEXT_V1,
                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    if (!s_xkb_map) return;

    s_xkb_state = xkb_state_new(s_xkb_map);
}

static struct wl_surface *s_kbd_surface = NULL;

/* ─── Key repeat ───────────────────────────────────────────────────────────
 * Wayland deliberately leaves repeat timing to the client (unlike X11,
 * where the server can resend KeyPress on its own). The compositor's
 * wl_keyboard::repeat_info gives us a rate (chars/sec) and delay (ms
 * before the first repeat); we track the currently-held key here and
 * a wait_event* call synthesizes additional FDK_EVENT_KEY_DOWN events
 * once the deadline has passed. Driven from wl_wait_event_timeout(),
 * which every app's frame loop already calls regularly. */
static uint32_t           s_repeat_keycode   = 0;      /* evdev code; 0 = none held — the actual sentinel */
static struct wl_surface *s_repeat_surface   = NULL;
static xkb_keysym_t       s_repeat_sym       = 0;       /* only meaningful while s_repeat_keycode != 0 */
static uint32_t           s_repeat_codepoint = 0;
static FDK_Modifier       s_repeat_mods      = FDK_MOD_NONE;
static int32_t            s_repeat_rate_ms   = 0;       /* ms between repeats; 0 = repeat disabled by compositor */
static int32_t            s_repeat_delay_ms  = 0;       /* ms before first repeat */
static uint64_t           s_repeat_next_ms   = 0;       /* fdk_time_ms() deadline for next synthesized press */

/* True for keys that should never themselves auto-repeat (pure modifiers).
 * Holding Shift alone shouldn't spam KEY_DOWN events for Shift. */
static bool key_is_modifier(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) return true; /* 0xffe1..0xffee */
    if (sym == XKB_KEY_Num_Lock)         return true; /* 0xff7f */
    if (sym == XKB_KEY_ISO_Level3_Shift) return true; /* 0xfe03, AltGr */
    return false;
}

static void repeat_cancel(void)
{
    /* Clearing the keycode is sufficient — it's the sole sentinel every
     * other function checks. s_repeat_sym is left as-is; it's dead data
     * until the next real press sets it again. */
    s_repeat_keycode = 0;
    s_repeat_surface = NULL;
}

static void kbd_enter(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *surf, struct wl_array *keys)
{ (void)d;(void)k;(void)s;(void)keys; s_kbd_surface = surf; }

static void kbd_leave(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *surf)
{
    (void)d;(void)k;(void)s;(void)surf;
    s_kbd_surface = NULL;
    repeat_cancel(); /* losing keyboard focus must stop any in-flight repeat */
}

/* Map xkb keysym to FDK_Key for special keys */
static FDK_Key keysym_to_fdk(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Escape:    return FDK_KEY_ESCAPE;
    case XKB_KEY_Return:    return FDK_KEY_RETURN;
    case XKB_KEY_space:     return FDK_KEY_SPACE;
    case XKB_KEY_BackSpace: return FDK_KEY_BACKSPACE;
    case XKB_KEY_Tab:       return FDK_KEY_TAB;
    case XKB_KEY_Delete:    return FDK_KEY_DELETE;
    case XKB_KEY_Left:      return FDK_KEY_LEFT;
    case XKB_KEY_Right:     return FDK_KEY_RIGHT;
    case XKB_KEY_Up:        return FDK_KEY_UP;
    case XKB_KEY_Down:      return FDK_KEY_DOWN;
    case XKB_KEY_Home:      return FDK_KEY_HOME;
    case XKB_KEY_End:       return FDK_KEY_END;
    case XKB_KEY_Page_Up:   return FDK_KEY_PAGEUP;
    case XKB_KEY_Page_Down: return FDK_KEY_PAGEDOWN;
    case XKB_KEY_F1:        return FDK_KEY_F1;
    case XKB_KEY_F2:        return FDK_KEY_F2;
    case XKB_KEY_F3:        return FDK_KEY_F3;
    case XKB_KEY_F4:        return FDK_KEY_F4;
    case XKB_KEY_F5:        return FDK_KEY_F5;
    case XKB_KEY_F6:        return FDK_KEY_F6;
    case XKB_KEY_F7:        return FDK_KEY_F7;
    case XKB_KEY_F8:        return FDK_KEY_F8;
    case XKB_KEY_F9:        return FDK_KEY_F9;
    case XKB_KEY_F10:       return FDK_KEY_F10;
    case XKB_KEY_F11:       return FDK_KEY_F11;
    case XKB_KEY_F12:       return FDK_KEY_F12;
    default:
        if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) return (FDK_Key)sym;
        if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) return (FDK_Key)(sym - XKB_KEY_A + XKB_KEY_a);
        return FDK_KEY_UNKNOWN;
    }
}

/* Returns true and fills *out with a synthesized KEY_DOWN if a held key's
 * repeat deadline has passed, rescheduling for the next tick at the
 * compositor-provided repeat rate. No-op (returns false) if no key is
 * currently armed for repeat or the deadline hasn't arrived yet.
 *
 * Defined here (not down near its callers in the wait_event functions)
 * because it needs keysym_to_fdk() and win_lookup() already declared —
 * C has no forward declarations by default, so this has to sit after
 * both of those and before anything that calls it, including
 * wl_poll_event() further down. */
static bool repeat_tick(FDK_Event *out)
{
    if (s_repeat_keycode == 0) return false;
    if (fdk_time_ms() < s_repeat_next_ms) return false;

    *out = (FDK_Event){0};
    out->type          = FDK_EVENT_KEY_DOWN;
    out->window        = win_lookup(s_repeat_surface);
    out->key.key       = keysym_to_fdk(s_repeat_sym);
    out->key.mods      = s_repeat_mods;
    out->key.codepoint = s_repeat_codepoint;

    /* Schedule the next tick off the deadline we just hit, not off
     * "now" — keeps a steady cadence even if this call ran a little
     * late, instead of drifting later with every repeat. */
    s_repeat_next_ms += (uint64_t)s_repeat_rate_ms;
    return true;
}

static void kbd_key(void *d, struct wl_keyboard *k, uint32_t serial,
                    uint32_t t, uint32_t key, uint32_t state)
{
    (void)d;(void)k;(void)t;
    s_last_serial = serial;   /* keep fresh for clipboard set_selection */
    if (!s_xkb_state) return;

    /* Wayland key codes are evdev codes; xkb uses keycode+8 */
    xkb_keycode_t  keycode = key + 8;
    xkb_keysym_t   sym     = xkb_state_key_get_one_sym(s_xkb_state, keycode);

    FDK_Event ev = {0};
    ev.type    = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
                     ? FDK_EVENT_KEY_DOWN : FDK_EVENT_KEY_UP;
    ev.window  = win_lookup(s_kbd_surface);
    ev.key.key = keysym_to_fdk(sym);
    ev.key.mods = s_mods;

    /* Get the Unicode codepoint for printable characters.
     * xkb_state_key_get_utf32 returns 0 for non-printable keys. */
    uint32_t cp = xkb_state_key_get_utf32(s_xkb_state, keycode);
    ev.key.codepoint = cp;

    evq_push_ev(&ev);

    /* Arm/disarm key repeat. Real presses/releases only — synthesized
     * repeat ticks are pushed straight to evq_push_ev() and never come
     * through here, so this can't re-arm itself off its own output. */
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (s_repeat_rate_ms > 0 && !key_is_modifier(sym)) {
            s_repeat_keycode   = key;
            s_repeat_surface   = s_kbd_surface;
            s_repeat_sym       = sym;
            s_repeat_codepoint = cp;
            s_repeat_mods      = s_mods;
            s_repeat_next_ms   = fdk_time_ms() + (uint64_t)s_repeat_delay_ms;
        } else {
            repeat_cancel();
        }
    } else if (key == s_repeat_keycode) {
        /* Releasing the key currently repeating stops it. Releasing some
         * other key (e.g. letting go of a modifier held alongside it)
         * must not disturb an unrelated repeat in progress. */
        repeat_cancel();
    }
}

static void kbd_modifiers(void *d, struct wl_keyboard *k, uint32_t serial,
                          uint32_t mods_depressed, uint32_t mods_latched,
                          uint32_t mods_locked, uint32_t group)
{
    (void)d;(void)k;(void)serial;
    if (!s_xkb_state) return;
    xkb_state_update_mask(s_xkb_state,
                          mods_depressed, mods_latched, mods_locked,
                          0, 0, group);

    /* Update our cached modifier flags */
    s_mods = FDK_MOD_NONE;
    if (xkb_state_mod_name_is_active(s_xkb_state, XKB_MOD_NAME_SHIFT,
                                      XKB_STATE_MODS_EFFECTIVE) > 0)
        s_mods |= FDK_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(s_xkb_state, XKB_MOD_NAME_CTRL,
                                      XKB_STATE_MODS_EFFECTIVE) > 0)
        s_mods |= FDK_MOD_CTRL;
    if (xkb_state_mod_name_is_active(s_xkb_state, XKB_MOD_NAME_ALT,
                                      XKB_STATE_MODS_EFFECTIVE) > 0)
        s_mods |= FDK_MOD_ALT;
    if (xkb_state_mod_name_is_active(s_xkb_state, XKB_MOD_NAME_LOGO,
                                      XKB_STATE_MODS_EFFECTIVE) > 0)
        s_mods |= FDK_MOD_SUPER;
}

static void kbd_repeat_info(void *d, struct wl_keyboard *k,
                            int32_t rate, int32_t delay)
{
    (void)d;(void)k;
    /* Protocol: rate is in characters per second, 0 means the compositor
     * wants repeat disabled entirely. delay is already milliseconds. */
    s_repeat_rate_ms  = (rate > 0) ? (1000 / rate) : 0;
    s_repeat_delay_ms = delay;
    if (rate <= 0) repeat_cancel();
}

static const struct wl_keyboard_listener s_keyboard_listener = {
    .keymap      = kbd_keymap,
    .enter       = kbd_enter,
    .leave       = kbd_leave,
    .key         = kbd_key,
    .modifiers   = kbd_modifiers,
    .repeat_info = kbd_repeat_info,
};

/* ─── Text input (IME) ────────────────────────────────────────────────────── */
#ifdef FDK_HAVE_TEXT_INPUT

/* Buffer for IME committed text */
static char s_ime_commit_buf[512];

static void ti_enter(void *d, struct zwp_text_input_v3 *ti,
                     struct wl_surface *surf)
{
    (void)d;(void)surf;
    /* Enable text input when our surface gains focus */
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_content_type(ti,
        ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
        ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_commit(ti);
}

static void ti_leave(void *d, struct zwp_text_input_v3 *ti,
                     struct wl_surface *surf)
{
    (void)d;(void)surf;
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);
}

static void ti_preedit_string(void *d, struct zwp_text_input_v3 *ti,
                               const char *text, int32_t cursor_begin,
                               int32_t cursor_end)
{
    (void)d;(void)ti;(void)text;(void)cursor_begin;(void)cursor_end;
    /* Preedit handling deferred — the IME shows its own preedit window
     * on most compositors. We just need commit_string for v0.2. */
}

static void ti_commit_string(void *d, struct zwp_text_input_v3 *ti,
                              const char *text)
{
    (void)d;(void)ti;
    if (!text || !text[0]) return;

    /* Push an FDK_EVENT_IME_COMMIT with the committed text */
    FDK_Event ev = {0};
    ev.type = FDK_EVENT_IME_COMMIT;
    ev.window = s_last_fdkwin;

    /* Copy text to our static buffer (the text pointer is owned by
     * the compositor and may be freed after this callback returns) */
    strncpy(s_ime_commit_buf, text, sizeof(s_ime_commit_buf) - 1);
    s_ime_commit_buf[sizeof(s_ime_commit_buf) - 1] = '\0';
    ev.ime_commit.text = s_ime_commit_buf;

    evq_push_ev(&ev);
}

static void ti_delete_surrounding_text(void *d, struct zwp_text_input_v3 *ti,
                                        uint32_t before, uint32_t after)
{
    (void)d;(void)ti;(void)before;(void)after;
    /* Surrounding text deletion deferred — not needed for basic IME */
}

static void ti_done(void *d, struct zwp_text_input_v3 *ti, uint32_t serial)
{
    (void)d;(void)ti;(void)serial;
    /* Called after a batch of preedit/commit/delete operations */
}

static const struct zwp_text_input_v3_listener s_text_input_listener = {
    .enter                   = ti_enter,
    .leave                   = ti_leave,
    .preedit_string          = ti_preedit_string,
    .commit_string           = ti_commit_string,
    .delete_surrounding_text = ti_delete_surrounding_text,
    .done                    = ti_done,
};

/* Create the text_input object after seat is available */
static void wl_text_input_init(void)
{
    if (!s_text_input_mgr || !s_seat) return;
    s_text_input = zwp_text_input_manager_v3_get_text_input(
                       s_text_input_mgr, s_seat);
    if (s_text_input) {
        zwp_text_input_v3_add_listener(s_text_input,
                                        &s_text_input_listener, NULL);
        fprintf(stderr, "[FDK/Wayland] Text input v3 enabled — IME supported\n");
    }
}

#endif /* FDK_HAVE_TEXT_INPUT */

/* ─── Seat ─────────────────────────────────────────────────────────────────── */
static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps)
{
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s_pointer) {
        s_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(s_pointer, &s_pointer_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s_keyboard) {
        s_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s_keyboard, &s_keyboard_listener, NULL);
    }
    if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s_pointer) {
        wl_pointer_destroy(s_pointer); s_pointer = NULL;
    }
    if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && s_keyboard) {
        wl_keyboard_destroy(s_keyboard); s_keyboard = NULL;
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *n)
{ (void)d;(void)s;(void)n; }

static const struct wl_seat_listener s_seat_listener = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

/* ─── Registry ─────────────────────────────────────────────────────────────── */
static void registry_global(void *d, struct wl_registry *reg,
                             uint32_t name, const char *iface, uint32_t version)
{
    (void)d;(void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        s_compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        s_shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        s_xdg_wm = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(s_xdg_wm, &s_xdg_wm_listener, NULL);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        s_seat = wl_registry_bind(reg, name, &wl_seat_interface, 4);
        wl_seat_add_listener(s_seat, &s_seat_listener, NULL);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
        s_data_mgr = wl_registry_bind(reg, name,
                         &wl_data_device_manager_interface, 3);
    } else if (!strcmp(iface, wl_output_interface.name)) {
        s_output = wl_registry_bind(reg, name, &wl_output_interface, 2);
        wl_output_add_listener(s_output, &s_output_listener, NULL);
    }
#ifdef FDK_HAVE_TEXT_INPUT
    else if (!strcmp(iface, zwp_text_input_manager_v3_interface.name)) {
        s_text_input_mgr = wl_registry_bind(reg, name,
                         &zwp_text_input_manager_v3_interface, 1);
    }
#endif
#ifdef FDK_HAVE_TOPLEVEL_ICON
    else if (!strcmp(iface, xdg_toplevel_icon_manager_v1_interface.name)) {
        s_icon_mgr = wl_registry_bind(reg, name,
                         &xdg_toplevel_icon_manager_v1_interface, 1);
    }
#endif
#ifdef FDK_HAVE_XDG_DECORATION
    else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
        s_decoration_mgr = wl_registry_bind(reg, name,
                         &zxdg_decoration_manager_v1_interface, 1);
    }
#endif
}

static void registry_global_remove(void *d, struct wl_registry *r, uint32_t n)
{ (void)d;(void)r;(void)n; }

static const struct wl_registry_listener s_registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* ─── Init / shutdown ──────────────────────────────────────────────────────── */
static bool wl_init(void)
{
    s_dpy = wl_display_connect(NULL);
    if (!s_dpy) {
        fprintf(stderr, "[FDK/Wayland] Cannot connect to display.\n");
        return false;
    }

    /* xkbcommon context — needed for keymap parsing */
    s_xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!s_xkb_ctx) {
        fprintf(stderr, "[FDK/Wayland] xkb_context_new failed.\n");
        return false;
    }

    s_registry = wl_display_get_registry(s_dpy);
    wl_registry_add_listener(s_registry, &s_registry_listener, NULL);
    wl_display_roundtrip(s_dpy); /* collect globals */
    wl_display_roundtrip(s_dpy); /* collect seat capabilities + keymap */
    wl_display_roundtrip(s_dpy); /* collect output scale */

    if (!s_compositor || !s_shm || !s_xdg_wm) {
        fprintf(stderr, "[FDK/Wayland] Missing globals.\n");
        return false;
    }

    /* Initialize text input (IME) if the compositor supports it */
#ifdef FDK_HAVE_TEXT_INPUT
    wl_text_input_init();
#endif

    /* Cursor theme — 24px, inherits from the environment */
    s_cursor_theme   = wl_cursor_theme_load(NULL, 24, s_shm);
    s_cursor_surface = wl_compositor_create_surface(s_compositor);

    /* Data device for clipboard */
    if (s_data_mgr && s_seat) {
        s_data_device = wl_data_device_manager_get_data_device(
                            s_data_mgr, s_seat);
        wl_data_device_add_listener(s_data_device, &s_dd_listener, NULL);
    }

#ifdef FDK_HAVE_OPENGL
    /* Initialise EGL against the Wayland display */
    s_egl_dpy = eglGetDisplay((EGLNativeDisplayType)s_dpy);
    if (s_egl_dpy != EGL_NO_DISPLAY) {
        EGLint major, minor;
        if (eglInitialize(s_egl_dpy, &major, &minor)) {
            eglBindAPI(EGL_OPENGL_API);
            EGLint attrs[] = {
                EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_RED_SIZE,   8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE,  8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 0,
                EGL_NONE
            };
            EGLint n = 0;
            if (eglChooseConfig(s_egl_dpy, attrs, &s_egl_cfg, 1, &n) && n > 0) {
                EGLint ctx_attrs[] = {
                    EGL_CONTEXT_MAJOR_VERSION, 3,
                    EGL_CONTEXT_MINOR_VERSION, 3,
                    EGL_NONE
                };
                s_egl_shared = eglCreateContext(s_egl_dpy, s_egl_cfg,
                                                EGL_NO_CONTEXT, ctx_attrs);
                if (s_egl_shared == EGL_NO_CONTEXT)
                    fprintf(stderr, "[FDK/Wayland] EGL: GL 3.3 context failed, OpenGL unavailable\n");
            }
        }
    }
#endif

    return true;
}

static void wl_backend_shutdown(void)
{
#ifdef FDK_HAVE_OPENGL
    if (s_egl_shared != EGL_NO_CONTEXT) {
        eglDestroyContext(s_egl_dpy, s_egl_shared);
        s_egl_shared = EGL_NO_CONTEXT;
    }
    if (s_egl_dpy != EGL_NO_DISPLAY) {
        eglTerminate(s_egl_dpy);
        s_egl_dpy = EGL_NO_DISPLAY;
    }
#endif
    free(s_clipboard); s_clipboard = NULL;
    if (s_data_src)    { wl_data_source_destroy(s_data_src); s_data_src = NULL; }
    if (s_data_device) { wl_data_device_destroy(s_data_device); s_data_device = NULL; }
    if (s_data_mgr)    { wl_data_device_manager_destroy(s_data_mgr); s_data_mgr = NULL; }
    if (s_cursor_surface) { wl_surface_destroy(s_cursor_surface); s_cursor_surface = NULL; }
    if (s_cursor_theme)   { wl_cursor_theme_destroy(s_cursor_theme); s_cursor_theme = NULL; }
    if (s_xkb_state) { xkb_state_unref(s_xkb_state);   s_xkb_state = NULL; }
    if (s_xkb_map)   { xkb_keymap_unref(s_xkb_map);    s_xkb_map   = NULL; }
    if (s_xkb_ctx)   { xkb_context_unref(s_xkb_ctx);   s_xkb_ctx   = NULL; }
    if (s_pointer)   { wl_pointer_destroy(s_pointer);   s_pointer   = NULL; }
    if (s_keyboard)  { wl_keyboard_destroy(s_keyboard); s_keyboard  = NULL; }
    if (s_seat)      { wl_seat_destroy(s_seat);         s_seat      = NULL; }
    if (s_xdg_wm)    { xdg_wm_base_destroy(s_xdg_wm);  s_xdg_wm    = NULL; }
#ifdef FDK_HAVE_TOPLEVEL_ICON
    if (s_icon_mgr)  { xdg_toplevel_icon_manager_v1_destroy(s_icon_mgr); s_icon_mgr = NULL; }
#endif
#ifdef FDK_HAVE_XDG_DECORATION
    if (s_decoration_mgr) { zxdg_decoration_manager_v1_destroy(s_decoration_mgr); s_decoration_mgr = NULL; }
#endif
#ifdef FDK_HAVE_TEXT_INPUT
    if (s_text_input)     { zwp_text_input_v3_destroy(s_text_input); s_text_input = NULL; }
    if (s_text_input_mgr) { zwp_text_input_manager_v3_destroy(s_text_input_mgr); s_text_input_mgr = NULL; }
#endif
    if (s_output)     { wl_output_destroy(s_output); s_output = NULL; }
    if (s_shm)       { wl_shm_destroy(s_shm);           s_shm       = NULL; }
    if (s_compositor){ wl_compositor_destroy(s_compositor); s_compositor = NULL; }
    if (s_registry)  { wl_registry_destroy(s_registry); s_registry  = NULL; }
    if (s_dpy)       { wl_display_disconnect(s_dpy);    s_dpy       = NULL; }
}

/* ─── Window ───────────────────────────────────────────────────────────────── */
static FDK_PlatformWindow *wl_window_create(const FDK_WindowDesc *desc)
{
    FDK_PlatformWindow *pw = calloc(1, sizeof *pw);
    if (!pw) return NULL;
    pw->shm_fd = -1;
    pw->w = desc->w;
    pw->h = desc->h;

    pw->surface      = wl_compositor_create_surface(s_compositor);
    pw->xdg_surface  = xdg_wm_base_get_xdg_surface(s_xdg_wm, pw->surface);
    pw->xdg_toplevel = xdg_surface_get_toplevel(pw->xdg_surface);

    xdg_surface_add_listener(pw->xdg_surface,   &s_xdg_surface_listener, pw);
    xdg_toplevel_add_listener(pw->xdg_toplevel, &s_toplevel_listener,    pw);

    if (desc->title)
        xdg_toplevel_set_title(pw->xdg_toplevel, desc->title);
    if (!desc->resizable) {
        xdg_toplevel_set_min_size(pw->xdg_toplevel, desc->w, desc->h);
        xdg_toplevel_set_max_size(pw->xdg_toplevel, desc->w, desc->h);
    }

#ifdef FDK_HAVE_OPENGL
    bool use_gl = (desc->render == FDK_RENDER_OPENGL ||
                   desc->render == FDK_RENDER_AUTO) &&
                  s_egl_shared != EGL_NO_CONTEXT;
    if (use_gl) {
        /* EGL window surface — no SHM needed */
        pw->egl_win = wl_egl_window_create(pw->surface, desc->w, desc->h);
        pw->egl_surface = eglCreateWindowSurface(s_egl_dpy, s_egl_cfg,
                              (EGLNativeWindowType)pw->egl_win, NULL);
        pw->egl_ctx = s_egl_shared; /* share the global context */
        eglMakeCurrent(s_egl_dpy, pw->egl_surface, pw->egl_surface, pw->egl_ctx);
        eglSwapInterval(s_egl_dpy, 1); /* vsync on */
    } else {
        create_shm_buffers(pw, desc->w, desc->h);
    }
#else
    create_shm_buffers(pw, desc->w, desc->h);
#endif

    wl_surface_commit(pw->surface);
    wl_display_roundtrip(s_dpy);
    return pw;
}

static void wl_window_destroy(FDK_PlatformWindow *pw)
{
    if (!pw) return;
#ifdef FDK_HAVE_OPENGL
    if (pw->egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(s_egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(s_egl_dpy, pw->egl_surface);
    }
    if (pw->egl_win) wl_egl_window_destroy(pw->egl_win);
#endif
    shm_destroy_buffers(pw);
#ifdef FDK_HAVE_XDG_DECORATION
    /* Destroy the decoration object BEFORE the toplevel it refers to,
     * otherwise the compositor may log a protocol error. */
    if (pw->xdg_decoration) {
        zxdg_toplevel_decoration_v1_destroy(pw->xdg_decoration);
        pw->xdg_decoration = NULL;
    }
#endif
    if (pw->xdg_toplevel) xdg_toplevel_destroy(pw->xdg_toplevel);
    if (pw->xdg_surface)  xdg_surface_destroy(pw->xdg_surface);
    if (pw->surface) {
        win_unregister(pw->surface);
        wl_surface_destroy(pw->surface);
    }
    free(pw);
}

static void wl_window_show(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->egl_surface != EGL_NO_SURFACE) {
        /* For GL windows just commit the surface — EGL handles presentation */
        wl_surface_commit(pw->surface);
        wl_display_flush(s_dpy);
        return;
    }
#endif
    if (!pw->shm_buf[pw->shm_back]) return;
    wl_surface_attach(pw->surface, pw->shm_buf[pw->shm_back], 0, 0);
    wl_surface_damage(pw->surface, 0, 0, pw->w, pw->h);
    wl_surface_commit(pw->surface);
    wl_display_flush(s_dpy);
}

static void wl_window_hide(FDK_PlatformWindow *pw)
{
    wl_surface_attach(pw->surface, NULL, 0, 0);
    wl_surface_commit(pw->surface);
}

static void wl_window_set_title(FDK_PlatformWindow *pw, const char *t)
{
    xdg_toplevel_set_title(pw->xdg_toplevel, t);
    wl_display_flush(s_dpy);
}

static FDK_Size wl_window_get_size(FDK_PlatformWindow *pw)
{
    return (FDK_Size){ pw->w, pw->h };
}

/* Return the window's current DPI scale.
 * Uses the wl_output scale reported by the compositor. */
static float wl_window_get_scale(FDK_PlatformWindow *pw)
{
    (void)pw;
    /* FDK_SCALE env var overrides everything (works on both X11 and Wayland).
     * Useful for testing or forcing a scale on compositors that report 1×. */
    const char *env = getenv("FDK_SCALE");
    if (env && env[0]) {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (end != env && v >= 1 && v <= 8)
            return (float)v;
    }
    /* s_output_scale is set by the output_scale callback during
     * registry roundtrip. It's an integer (1, 2, 3) per the
     * wl_output protocol. */
    return (float)s_output_scale;
}

static void wl_window_request_redraw(FDK_PlatformWindow *pw)
{
    wl_surface_damage(pw->surface, 0, 0, pw->w, pw->h);
    wl_surface_commit(pw->surface);
    wl_display_flush(s_dpy);
    /* wl_surface_damage/commit alone only tell the COMPOSITOR this
     * surface needs recompositing -- they generate nothing an FDK app
     * would see via fdk_wait_event()/fdk_poll_event(), unlike X11's
     * XSendEvent-based request_redraw. Without this, an app whose
     * event loop is only driven by fdk_wait_event_timeout() would
     * never notice a redraw request that didn't originate from real
     * input, e.g. fdk_ui_set_theme() being called from a
     * fdk_theme_watch() background thread while the window is
     * otherwise idle. Push the same synthetic EXPOSE
     * xdg_surface_configure already uses once at startup, so a
     * redraw request reliably wakes the app's loop on this backend
     * too, consistent with X11's behavior. */
    FDK_Event ev = {0};
    ev.type   = FDK_EVENT_EXPOSE;
    ev.window = win_lookup(pw->surface);
    evq_push_ev(&ev);
}

static void wl_window_set_maximized(FDK_PlatformWindow *pw, bool m)
{
    /* Fire-and-forget per protocol — the compositor replies with a
     * configure event (handled in toplevel_configure) reporting what
     * actually happened, which may not match what was requested. */
    if (m) xdg_toplevel_set_maximized(pw->xdg_toplevel);
    else   xdg_toplevel_unset_maximized(pw->xdg_toplevel);
    wl_display_flush(s_dpy);
}

static void wl_window_set_fullscreen(FDK_PlatformWindow *pw, bool f)
{
    /* NULL output = let the compositor choose which display. */
    if (f) xdg_toplevel_set_fullscreen(pw->xdg_toplevel, NULL);
    else   xdg_toplevel_unset_fullscreen(pw->xdg_toplevel);
    wl_display_flush(s_dpy);
}

static bool wl_window_is_maximized(FDK_PlatformWindow *pw)
{
    return pw->maximized;
}

static bool wl_window_is_fullscreen(FDK_PlatformWindow *pw)
{
    return pw->fullscreen;
}

/* Minimize (iconify) — xdg_toplevel_set_minimized is a one-way request;
 * the compositor is not obligated to actually minimize it, and there's
 * no unset_minimized — the user restores via the taskbar/compositor. */
static void wl_window_minimize(FDK_PlatformWindow *pw)
{
    if (pw->xdg_toplevel) {
        xdg_toplevel_set_minimized(pw->xdg_toplevel);
        wl_display_flush(s_dpy);
    }
}

/* Begin interactive move. The compositor drives the move via its own
 * pointer grab; we just hand off the input serial from the button press
 * that triggered the move. Requires a seat; the serial is captured in
 * ptr_button() (s_last_serial) and is valid for the most recent press
 * only — the spec requires the serial to be from the originating input
 * event, so callers must invoke this synchronously from their
 * MOUSE_DOWN handler. */
static void wl_window_begin_move(FDK_PlatformWindow *pw, const FDK_Event *ev)
{
    (void)ev; /* FDK_Event carries no serial; we use s_last_serial */
    if (!pw->xdg_toplevel || !s_seat) return;
    if (s_last_serial == 0) return;
    xdg_toplevel_move(pw->xdg_toplevel, s_seat, s_last_serial);
    wl_display_flush(s_dpy);
}

/* Interactive resize — Wayland uses xdg_toplevel_resize() with the
 * seat + serial, same pattern as begin_move. */
static void wl_window_begin_resize(FDK_PlatformWindow *pw,
                                    const FDK_Event *ev, int edge)
{
    (void)ev; (void)edge;
    if (!pw->xdg_toplevel || !s_seat) return;
    if (s_last_serial == 0) return;
    /* Wayland xdg_toplevel_resize takes a 'edges' bitmask:
     *   XDG_TOPLEVEL_RESIZE_EDGE_TOP = 1, BOTTOM = 2, LEFT = 4, RIGHT = 8
     * Our public API uses 1-8 (1=top, 2=bottom, 3=left, 4=right,
     * 5=tl, 6=tr, 7=bl, 8=br). Map them: */
    uint32_t wl_edges = 0;
    switch (edge) {
        case 1: wl_edges = 1; break; /* top */
        case 2: wl_edges = 2; break; /* bottom */
        case 3: wl_edges = 4; break; /* left */
        case 4: wl_edges = 8; break; /* right */
        case 5: wl_edges = 1|4; break; /* top-left */
        case 6: wl_edges = 1|8; break; /* top-right */
        case 7: wl_edges = 2|4; break; /* bottom-left */
        case 8: wl_edges = 2|8; break; /* bottom-right */
    }
    xdg_toplevel_resize(pw->xdg_toplevel, s_seat, s_last_serial, wl_edges);
    wl_display_flush(s_dpy);
}

/* Direct resize — Wayland clients can't directly set window size (the
 * compositor has final say), but xdg_toplevel_set_size lets us REQUEST
 * a specific size. The compositor will then send a configure event with
 * the actual size, which triggers layout + paint.
 *
 * For CSD client-side resize tracking (edge drag), this is called on
 * every mouse move. The compositor may throttle configure events, but
 * most wlroots-based compositors (labwc, Sway, Hyprland) send them
 * immediately, giving live updates. */
static void wl_window_resize(FDK_PlatformWindow *pw, int w, int h)
{
    if (!pw->xdg_toplevel || !pw->xdg_surface) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* xdg_surface_set_window_geometry tells the compositor the actual
     * window geometry (excluding shadows etc). This is the Wayland
     * equivalent of XResizeWindow. The compositor will send a configure
     * event with the actual size, triggering layout + paint. */
    xdg_surface_set_window_geometry(pw->xdg_surface, 0, 0, w, h);
    wl_surface_commit(pw->surface);
    wl_display_flush(s_dpy);
}

/* Move + resize — on Wayland, clients can't set window position (the
 * compositor controls placement). The x/y parameters are ignored; only
 * the width/height are passed to xdg_toplevel_set_size. For left/top
 * edge drags, the window won't move on Wayland (compositor decides),
 * but the size will still change correctly. */
static void wl_window_move_resize(FDK_PlatformWindow *pw, int x, int y, int w, int h)
{
    (void)x; (void)y;  /* Wayland: compositor controls position */
    wl_window_resize(pw, w, h);
}

/* Drag-and-drop: set the drop callback. The actual DnD handling is
 * done by the wl_data_device listeners (dd_enter/dd_drop/dd_leave
 * above) which call this callback when a file is dropped on the
 * window's surface. */
static void wl_window_set_drop_handler(FDK_PlatformWindow *pw,
                                        FDK_DropCb cb, void *ud)
{
    if (!pw) return;
    pw->drop_cb = cb;
    pw->drop_ud = ud;
}

/* CSD/SSD negotiation via the xdg-decoration-unstable-v1 protocol.
 *
 * When the compositor advertises the zxdg_decoration_manager_v1
 * global, we use it to tell the compositor which side draws the
 * titlebar:
 *   decorated=true  → request SERVER_SIDE (compositor draws its own)
 *   decorated=false → request CLIENT_SIDE (we draw our own via
 *                     fdk_titlebar(), compositor stays out of the way)
 *
 * The protocol uses "request" terminology because the compositor has
 * the final say — it may ignore our preference (e.g. GNOME always
 * forces server-side decorations on Wayland). The .configure event
 * tells us what the compositor actually chose; we don't currently
 * listen for it because FDK's titlebar widget draws regardless.
 *
 * If the compositor doesn't support xdg-decoration, this is a no-op
 * — the compositor will draw its own decorations (SSD) or not (CSD)
 * based on its own defaults. Labwc/Hyprland default to SSD without
 * this negotiation; Sway defaults to CSD; GNOME forces SSD. */
static void wl_window_set_decorated(FDK_PlatformWindow *pw, bool decorated)
{
#ifdef FDK_HAVE_XDG_DECORATION
    if (!s_decoration_mgr || !pw->xdg_toplevel) return;

    /* Create the per-toplevel decoration object if we don't have one
     * yet. This is what we send set_mode() requests on. */
    if (!pw->xdg_decoration) {
        pw->xdg_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            s_decoration_mgr, pw->xdg_toplevel);
        if (!pw->xdg_decoration) return;
    }

    /* set_mode is a request, not a guarantee — the compositor may
     * reply with a different mode via .configure. We send our
     * preference and trust the compositor to honor it (most do). */
    if (decorated)
        zxdg_toplevel_decoration_v1_set_mode(pw->xdg_decoration,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    else
        zxdg_toplevel_decoration_v1_set_mode(pw->xdg_decoration,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    wl_display_flush(s_dpy);
#endif
}

static void wl_window_set_icon_name(FDK_PlatformWindow *pw, const char *name)
{
#ifdef FDK_HAVE_TOPLEVEL_ICON
    if (!s_icon_mgr) return; /* compositor doesn't support the protocol */
    struct xdg_toplevel_icon_v1 *icon =
        xdg_toplevel_icon_manager_v1_create_icon(s_icon_mgr);
    if (!icon) return;
    if (name)
        xdg_toplevel_icon_v1_set_name(icon, name);
    xdg_toplevel_icon_manager_v1_set_icon(s_icon_mgr, pw->xdg_toplevel, icon);
    wl_surface_commit(pw->surface);
    /* The icon is now immutable and may be destroyed — the toplevel keeps it. */
    xdg_toplevel_icon_v1_destroy(icon);
    wl_display_flush(s_dpy);
#else
    (void)pw; (void)name;
#endif
}

/* Set icon from a PNG file using wl_shm + xdg-toplevel-icon-v1.
 *
 * Decodes the PNG with stb_image (if available), creates a wl_shm
 * pool + buffer with the raw ARGB pixel data, then creates an
 * xdg_toplevel_icon_v1 and sets the buffer as the icon. The
 * compositor reads the buffer and displays it in the taskbar. */
static void wl_window_set_icon_file(FDK_PlatformWindow *pw, const char *path)
{
#if defined(FDK_HAVE_TOPLEVEL_ICON) && defined(FDK_WITH_STB_IMAGE)
    if (!s_icon_mgr || !pw->xdg_toplevel || !path) return;
    if (access(path, R_OK) != 0) return;

    /* Decode PNG to RGBA */
    int w, h, channels;
    unsigned char *rgba = stbi_load(path, &w, &h, &channels, 4);
    if (!rgba) return;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) { stbi_image_free(rgba); return; }

    /* Convert RGBA → ARGB (Wayland SHM format for icons is ARGB8888) */
    size_t buf_size = (size_t)w * h * 4;
    uint32_t *argb = malloc(buf_size);
    if (!argb) { stbi_image_free(rgba); return; }
    for (int i = 0; i < w * h; i++) {
        unsigned char *px = &rgba[i * 4];
        argb[i] = ((uint32_t)px[3] << 24) |  /* A */
                  ((uint32_t)px[0] << 16) |  /* R */
                  ((uint32_t)px[1] <<  8) |  /* G */
                   (uint32_t)px[2];          /* B */
    }
    stbi_image_free(rgba);

    /* Create a shared memory file for the pixel data */
    char shm_name[] = "/fdk-icon-XXXXXX";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
    if (fd < 0) { free(argb); return; }
    shm_unlink(shm_name);  /* name can be unlinked immediately */
    if (ftruncate(fd, (off_t)buf_size) != 0) { close(fd); free(argb); return; }

    /* Map and copy pixel data */
    void *shm_data = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) { close(fd); free(argb); return; }
    memcpy(shm_data, argb, buf_size);
    munmap(shm_data, buf_size);
    free(argb);

    /* Create wl_shm pool and buffer */
    struct wl_shm_pool *pool = wl_shm_create_pool(s_shm, fd, (int32_t)buf_size);
    close(fd);
    if (!pool) return;

    /* WL_SHM_FORMAT_ARGB8888 = 0 (Wayland pixel format) */
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
        w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer) return;

    /* Create icon from the buffer and set it on the toplevel */
    struct xdg_toplevel_icon_v1 *icon =
        xdg_toplevel_icon_manager_v1_create_icon(s_icon_mgr);
    if (!icon) { wl_buffer_destroy(buffer); return; }

    xdg_toplevel_icon_v1_add_buffer(icon, buffer, 1);  /* scale = 1 */
    xdg_toplevel_icon_manager_v1_set_icon(s_icon_mgr, pw->xdg_toplevel, icon);

    /* The icon object and buffer are now owned by the compositor.
     * We can destroy our references. */
    xdg_toplevel_icon_v1_destroy(icon);
    wl_buffer_destroy(buffer);
    wl_surface_commit(pw->surface);
    wl_display_flush(s_dpy);
#else
    (void)pw; (void)path;
#endif
}

static uint32_t *wl_get_framebuffer(FDK_PlatformWindow *pw, int *stride_px)
{
    *stride_px = pw->stride_px;
    return pw->shm_pixels[pw->shm_back];
}

static void wl_present(FDK_PlatformWindow *pw)
{
    if (!pw->shm_buf[pw->shm_back]) return;

    int front = pw->shm_back;
    /* Swap: next draw goes to the other buffer */
    pw->shm_back = 1 - front;
    pw->shm_buf_busy[front] = true;

    wl_surface_attach(pw->surface, pw->shm_buf[front], 0, 0);
    wl_surface_damage(pw->surface, 0, 0, pw->w, pw->h);
    wl_surface_commit(pw->surface);
    wl_display_flush(s_dpy);
}

static bool wl_gl_make_current(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->egl_surface == EGL_NO_SURFACE) return false;
    return eglMakeCurrent(s_egl_dpy, pw->egl_surface,
                          pw->egl_surface, pw->egl_ctx) == EGL_TRUE;
#else
    (void)pw; return false;
#endif
}

static void wl_gl_swap_buffers(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->egl_surface != EGL_NO_SURFACE)
        eglSwapBuffers(s_egl_dpy, pw->egl_surface);
#else
    (void)pw;
#endif
}

/* Write text to the Wayland clipboard */
static void wl_clipboard_set(const char *text)
{
    if (!s_data_mgr || !s_seat || !s_data_device || !text) return;
    if (s_last_serial == 0) return; /* no valid serial yet — ignore */

    /* Store locally — ds_send() will serve it when compositor asks */
    free(s_clipboard);
    s_clipboard = strdup(text);

    /* Replace old source */
    if (s_data_src) {
        wl_data_source_destroy(s_data_src);
        s_data_src = NULL;
    }

    s_data_src = wl_data_device_manager_create_data_source(s_data_mgr);
    if (!s_data_src) return;
    wl_data_source_add_listener(s_data_src, &s_ds_listener, NULL);
    wl_data_source_offer(s_data_src, "text/plain;charset=utf-8");
    wl_data_source_offer(s_data_src, "text/plain");
    wl_data_device_set_selection(s_data_device, s_data_src, s_last_serial);
    wl_display_flush(s_dpy);
}

/* Read text from the Wayland clipboard.
 *
 * Protocol flow:
 *   1. We call wl_data_offer_receive() — tells the compositor which fd to
 *      write into and which mime type we want.
 *   2. We flush our request to the compositor.
 *   3. The compositor calls ds_send() on the data source owner (could be us
 *      or another app) which writes into the write-end of our pipe.
 *   4. We read from the read-end.
 *
 * The blocking read() deadlock is avoided by:
 *   - Setting the read-end non-blocking so we can poll.
 *   - Calling wl_display_roundtrip() to let ds_send fire before we read.
 *   - Reading with a small retry loop.
 */
static char *wl_clipboard_get(void)
{
    if (!s_paste_offer) return NULL;

    const char *mimes[] = {
        "text/plain;charset=utf-8",
        "text/plain",
        NULL
    };

    for (int mi = 0; mimes[mi]; mi++) {
        int fds[2];
        if (pipe(fds) < 0) return NULL;

        /* Make read end non-blocking */
        fcntl(fds[0], F_SETFL, O_NONBLOCK);

        wl_data_offer_receive(s_paste_offer, mimes[mi], fds[1]);
        wl_display_flush(s_dpy);
        close(fds[1]);  /* close write end — compositor has its own copy */

        /* Give the compositor / data-source owner a chance to write.
         * wl_display_roundtrip sends our pending requests and waits for
         * the server to process them, which triggers ds_send(). */
        wl_display_roundtrip(s_dpy);

        /* Read with a short retry loop in case write is slightly delayed */
        char buf[65536];
        ssize_t total = 0;
        for (int attempts = 0; attempts < 10; attempts++) {
            ssize_t n = read(fds[0], buf + total,
                             sizeof(buf) - 1 - total);
            if (n > 0) {
                total += n;
                if (total >= (ssize_t)(sizeof(buf) - 1)) break;
            } else if (n == 0) {
                break; /* EOF */
            } else {
                /* EAGAIN — no data yet, yield briefly */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    struct timespec ts = {0, 2000000}; /* 2ms */
                    nanosleep(&ts, NULL);
                } else {
                    break; /* real error */
                }
            }
        }
        close(fds[0]);

        if (total > 0) {
            buf[total] = '\0';
            return strdup(buf);
        }
    }
    return NULL;
}

static void wl_set_cursor(FDK_Cursor cursor)
{
    if (!s_pointer) return;
    const char *name;
    switch (cursor) {
    case FDK_CURSOR_POINTER:    name = "pointer";           break;
    case FDK_CURSOR_TEXT:       name = "text";              break;
    case FDK_CURSOR_CROSSHAIR:  name = "crosshair";         break;
    case FDK_CURSOR_MOVE:       name = "move";              break;
    case FDK_CURSOR_RESIZE_H:   name = "ew-resize";         break;
    case FDK_CURSOR_RESIZE_V:   name = "ns-resize";         break;
    case FDK_CURSOR_RESIZE_TL:  name = "nwse-resize";       break;
    case FDK_CURSOR_RESIZE_TR:  name = "nesw-resize";       break;
    case FDK_CURSOR_NOT_ALLOWED:name = "not-allowed";       break;
    default:                    name = "default";           break;
    }
    set_cursor(s_pointer, s_ptr_serial, name);
}

static void wl_get_root_mouse(int *root_x, int *root_y)
{
    /* Wayland doesn't have a global root coordinate system.
     * Use the last known pointer position (surface-relative). */
    *root_x = s_ptr_x;
    *root_y = s_ptr_y;
}

static void wl_window_register(FDK_PlatformWindow *pw, FDK_Window *fdkw)
{
    win_register(pw->surface, fdkw);
}

/* ─── Events ───────────────────────────────────────────────────────────────── */

/*
 * Non-blocking poll: dispatch whatever is already in the local Wayland
 * buffer without reading the socket. Returns one event from the evq or false.
 *
 * IMPORTANT: never call wl_display_prepare_read() here — that would
 * conflict with wl_wait_event's use of wl_display_dispatch().
 */
static bool wl_poll_event(FDK_Event *out)
{
    wl_display_dispatch_pending(s_dpy);
    wl_display_flush(s_dpy);

    for (int _i = 0; _i < s_win_count; _i++) {
        FDK_Window *_fw = s_windows[_i].fdkw;
        if (!_fw) continue;
        FDK_PlatformWindow *pw = fdk_window_get_pw(_fw);
        if (pw && pw->close_requested) {
            pw->close_requested = false;
            out->type   = FDK_EVENT_CLOSE;
            out->window = _fw;
            return true;
        }
    }
    /* Dispatch any buffer release events so busy flags get cleared */
    wl_display_dispatch_pending(s_dpy);

    if (evq_pop(out)) return true;
    return repeat_tick(out);
}

/*
 * Blocking wait: wl_display_dispatch() reads the socket AND dispatches,
 * then we try to pop from the evq. Loop until we get something.
 */
static bool check_close_requests(FDK_Event *out)
{
    for (int i = 0; i < s_win_count; i++) {
        FDK_Window *fw = s_windows[i].fdkw;
        if (!fw) continue;
        FDK_PlatformWindow *pw = fdk_window_get_pw(fw);
        if (pw && pw->close_requested) {
            pw->close_requested = false;
            out->type   = FDK_EVENT_CLOSE;
            out->window = fw;
            return true;
        }
    }
    return false;
}

static bool wl_wait_event_timeout(FDK_Event *out, int timeout_ms)
{
    for (;;) {
        wl_display_flush(s_dpy);
        wl_display_dispatch_pending(s_dpy);
        if (check_close_requests(out)) return true;
        if (evq_pop(out)) return true;
        if (repeat_tick(out)) return true;

        /* Prepare to read socket */
        while (wl_display_prepare_read(s_dpy) != 0)
            wl_display_dispatch_pending(s_dpy);

        wl_display_flush(s_dpy);

        /* Clamp the poll timeout so a held key's repeat deadline can't
         * be slept through. -1 (infinite) and 0 (don't block) both pass
         * through untouched; only a positive, longer timeout gets
         * shortened. */
        int poll_ms = timeout_ms;
        if (s_repeat_keycode != 0 && (timeout_ms < 0 || timeout_ms > 0)) {
            uint64_t now = fdk_time_ms();
            int64_t  until = (int64_t)s_repeat_next_ms - (int64_t)now;
            int      until_ms = (until < 0) ? 0 : (until > INT32_MAX ? INT32_MAX : (int)until);
            if (timeout_ms < 0 || until_ms < timeout_ms)
                poll_ms = until_ms;
        }

        struct pollfd pfd = { wl_display_get_fd(s_dpy), POLLIN, 0 };
        int ret = poll(&pfd, 1, poll_ms);

        if (ret > 0) {
            wl_display_read_events(s_dpy);
            wl_display_dispatch_pending(s_dpy);
            if (check_close_requests(out)) return true;
            if (evq_pop(out)) return true;
            if (repeat_tick(out)) return true;
            /* For timed waits return after one socket read even if no FDK event */
            if (timeout_ms >= 0) return false;
        } else {
            wl_display_cancel_read(s_dpy);
            /* poll_ms may be shorter than the caller's timeout_ms (we
             * clamped it to the repeat deadline) — check repeat before
             * concluding this was a real timeout. */
            if (repeat_tick(out)) return true;
            /* Not actually due yet (e.g. repeat got cancelled the instant
             * poll() returned): loop and let the top of the loop recompute
             * poll_ms from current state, rather than reporting a timeout
             * the caller never asked for. */
            if (poll_ms != timeout_ms) continue;
            return false; /* genuine timeout or error */
        }
    }
}

/* wl_wait_event blocks indefinitely until an FDK event is available.
 * Implemented on top of wl_wait_event_timeout(out, -1) in a loop so it
 * shares one code path with the timed variant — including repeat-key
 * handling, which a plain wl_display_dispatch() block could never see,
 * since nothing would wake it up while the socket stays quiet. */
static void wl_wait_event(FDK_Event *out)
{
    while (!wl_wait_event_timeout(out, -1)) { /* spin only on spurious wakeups */ }
}

/* ─── VTable ───────────────────────────────────────────────────────────────── */
const FDK_PlatformVTable fdk_platform_wayland = {
    .name                  = "Wayland",
    .init                  = wl_init,
    .shutdown              = wl_backend_shutdown,
    .window_create         = wl_window_create,
    .window_destroy        = wl_window_destroy,
    .window_show           = wl_window_show,
    .window_hide           = wl_window_hide,
    .window_set_title      = wl_window_set_title,
    .window_get_size       = wl_window_get_size,
    .window_get_scale      = wl_window_get_scale,
    .window_request_redraw = wl_window_request_redraw,
    .window_set_maximized  = wl_window_set_maximized,
    .window_set_fullscreen = wl_window_set_fullscreen,
    .window_is_maximized   = wl_window_is_maximized,
    .window_is_fullscreen  = wl_window_is_fullscreen,
    .window_minimize       = wl_window_minimize,
    .window_begin_move     = wl_window_begin_move,
    .window_begin_resize   = wl_window_begin_resize,
    .window_resize         = wl_window_resize,
    .window_move_resize    = wl_window_move_resize,
    .window_set_decorated  = wl_window_set_decorated,
    .window_set_drop_handler = wl_window_set_drop_handler,
    .window_set_icon_name  = wl_window_set_icon_name,
    .window_set_icon_file  = wl_window_set_icon_file,
    .window_get_framebuffer= wl_get_framebuffer,
    .window_present        = wl_present,
    .gl_make_current       = wl_gl_make_current,
    .gl_swap_buffers       = wl_gl_swap_buffers,
    .poll_event            = wl_poll_event,
    .wait_event            = wl_wait_event,
    .wait_event_timeout    = wl_wait_event_timeout,
    .window_register       = wl_window_register,
    .set_cursor            = wl_set_cursor,
    .get_root_mouse        = wl_get_root_mouse,
    .clipboard_set         = wl_clipboard_set,
    .clipboard_get         = wl_clipboard_get,
};

#endif /* FDK_HAVE_WAYLAND */
