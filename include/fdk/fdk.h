/*
 * fdk.h — Faded Dream Kit
 * Master public header. Include this in your application.
 *
 * License: FDK Proprietary License — see LICENSE in the project root
 * Dependencies: none (this header only)
 */
#ifndef FDK_H
#define FDK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─── Version ────────────────────────────────────────────────────────────── */
#define FDK_VERSION_MAJOR 0
#define FDK_VERSION_MINOR 1
#define FDK_VERSION_PATCH 0

/* ─── Forward declarations ───────────────────────────────────────────────── */
typedef struct FDK_Window   FDK_Window;
typedef struct FDK_Surface  FDK_Surface;
typedef struct FDK_Font     FDK_Font;

/* ─── Colour (RGBA, 0‥255) ───────────────────────────────────────────────── */
typedef struct {
    uint8_t r, g, b, a;
} FDK_Color;

#define FDK_RGBA(r,g,b,a)  ((FDK_Color){(r),(g),(b),(a)})
#define FDK_RGB(r,g,b)     FDK_RGBA((r),(g),(b),255)
#define FDK_BLACK          FDK_RGB(0,0,0)
#define FDK_WHITE          FDK_RGB(255,255,255)
#define FDK_TRANSPARENT    FDK_RGBA(0,0,0,0)

/* ─── Geometry ───────────────────────────────────────────────────────────── */
typedef struct { int x, y; }          FDK_Point;
typedef struct { int w, h; }          FDK_Size;
typedef struct { int x, y, w, h; }    FDK_Rect;

/* ─── Events ─────────────────────────────────────────────────────────────── */
typedef enum {
    FDK_EVENT_NONE = 0,
    FDK_EVENT_QUIT,
    FDK_EVENT_CLOSE,        /* window close requested  */
    FDK_EVENT_RESIZE,
    FDK_EVENT_EXPOSE,       /* window needs redraw     */
    FDK_EVENT_STATE_CHANGE, /* maximized/fullscreen/activated changed by
                              * the compositor or window manager — fires
                              * independently of FDK_EVENT_RESIZE, which
                              * may also fire for the same transition */
    FDK_EVENT_SCALE_CHANGE, /* window DPI/scale factor changed (monitor
                              * hotplug, dragged to a different-DPI monitor,
                              * user changed display scale mid-session).
                              * Read the new scale via
                              * fdk_window_get_scale(). */
    FDK_EVENT_KEY_DOWN,
    FDK_EVENT_KEY_UP,
    FDK_EVENT_MOUSE_MOVE,
    FDK_EVENT_MOUSE_DOWN,
    FDK_EVENT_MOUSE_UP,
    FDK_EVENT_MOUSE_SCROLL,
    /* IME committed text. The IME (IBus/Fcitx on Wayland, XIM on X11)
     * has finalized a string of one or more characters and delivered
     * them to the focused widget. The widget should insert the string
     * at its cursor.
     *
     * Unlike FDK_EVENT_KEY_DOWN (which carries a single codepoint),
     * FDK_EVENT_IME_COMMIT can carry a multi-codepoint UTF-8 string
     * (e.g., a Japanese kanji lookup might commit 2-3 chars at once).
     *
     * The text pointer is valid only for the duration of the event
     * dispatch — copy it if you need to keep it. */
    FDK_EVENT_IME_COMMIT,
} FDK_EventType;

/* Keyboard key codes — minimal portable set */
typedef enum {
    FDK_KEY_UNKNOWN = 0,
    FDK_KEY_ESCAPE, FDK_KEY_RETURN, FDK_KEY_SPACE, FDK_KEY_BACKSPACE,
    FDK_KEY_TAB, FDK_KEY_DELETE,
    FDK_KEY_LEFT, FDK_KEY_RIGHT, FDK_KEY_UP, FDK_KEY_DOWN,
    FDK_KEY_HOME, FDK_KEY_END, FDK_KEY_PAGEUP, FDK_KEY_PAGEDOWN,
    FDK_KEY_F1,  FDK_KEY_F2,  FDK_KEY_F3,  FDK_KEY_F4,
    FDK_KEY_F5,  FDK_KEY_F6,  FDK_KEY_F7,  FDK_KEY_F8,
    FDK_KEY_F9,  FDK_KEY_F10, FDK_KEY_F11, FDK_KEY_F12,
    FDK_KEY_A = 'a', /* printable ASCII keys map directly */
} FDK_Key;

typedef enum {
    FDK_MOD_NONE  = 0,
    FDK_MOD_SHIFT = 1 << 0,
    FDK_MOD_CTRL  = 1 << 1,
    FDK_MOD_ALT   = 1 << 2,
    FDK_MOD_SUPER = 1 << 3,
} FDK_Modifier;

typedef enum {
    FDK_BUTTON_LEFT = 1,
    FDK_BUTTON_MIDDLE,
    FDK_BUTTON_RIGHT,
} FDK_MouseButton;

typedef struct {
    FDK_EventType   type;
    FDK_Window     *window;
    union {
        struct { int w, h; }                           resize;
        struct { bool maximized, fullscreen,
                      activated; }                     state;
        struct { FDK_Key key; FDK_Modifier mods;
                 uint32_t codepoint; }                 key;
        struct { int x, y; FDK_Modifier mods; }        motion;
        struct { int x, y; FDK_MouseButton button;
                 FDK_Modifier mods; }                  mouse;
        struct { float dx, dy; }                       scroll;
        /* IME committed text — see FDK_EVENT_IME_COMMIT docs above.
         * `text` is a NUL-terminated UTF-8 string owned by FDK; do not
         * free. Valid only during event dispatch. */
        struct { const char *text; }                   ime_commit;
    };
} FDK_Event;

/* ─── Render backend selection ───────────────────────────────────────────── */
typedef enum {
    FDK_RENDER_SOFTWARE = 0,   /* CPU pixel buffer — always available  */
    FDK_RENDER_OPENGL,         /* OpenGL 3.3 core profile              */
    FDK_RENDER_VULKAN,         /* Vulkan (future)                      */
    FDK_RENDER_AUTO,           /* pick best available                  */
} FDK_RenderBackend;

/* ─── Platform backend selection ─────────────────────────────────────────── */
typedef enum {
    FDK_PLATFORM_AUTO = 0,     /* detect at runtime                    */
    FDK_PLATFORM_X11,
    FDK_PLATFORM_WAYLAND,
} FDK_PlatformBackend;

/* ─── Init / shutdown ────────────────────────────────────────────────────── */
typedef struct {
    FDK_PlatformBackend platform;
    FDK_RenderBackend   render;
    const char         *app_name;
} FDK_InitInfo;

bool  fdk_init(const FDK_InitInfo *info);
void  fdk_shutdown(void);

/* ─── Build feature query ───────────────────────────────────────────────────
 *
 * Returns a space-separated string naming the optional features this
 * libfdk was built with. Always-present features (freetype) are not
 * listed; only the auto-detected ones. As of v0.2 the possible tokens
 * are:
 *   x11 wayland opengl harfbuzz fribidi stb_image xcursor
 *         xdg-decoration xdg-toplevel-icon
 *
 * Useful for `--version` output, diagnostic logs, and tests that need
 * to know which code path is active. The pointer is static for the
 * lifetime of the process — do not free. */
const char *fdk_get_features(void);

/* Returns true if the named feature is present in fdk_get_features().
 * Comparison is case-sensitive and matches whole tokens only, so
 * fdk_has_feature("harfbuzz") matches "harfbuzz" but not
 * "harfbuzz-subset". */
bool fdk_has_feature(const char *name);

/* ─── Window ─────────────────────────────────────────────────────────────── */
typedef struct {
    const char         *title;
    int                 x, y;        /* FDK_WINDOW_POS_CENTER = -1  */
    int                 w, h;
    bool                resizable;
    FDK_RenderBackend   render;      /* override per-window         */
    /* Minimum window size (0 = no minimum). Prevents the user from
     * shrinking the window so small that content becomes unusable.
     * On X11: sets PMinSize in the window's WMNormalHints.
     * On Wayland: compositors generally don't honor minimum sizes,
     *   but the value is still useful as a layout floor. */
    int                 min_w, min_h;
    /* Client-side decorations opt-in. When true, FDK requests that the
     * compositor/WM NOT draw its own title bar and window borders, and
     * the app is expected to draw a title bar widget (fdk_titlebar())
     * at the top of its root container. The platform backend asks the
     * compositor to suppress server-side decorations via:
     *   - Wayland: zxdg_decoration_manager_v1 (when available)
     *   - X11:     _MOTIF_WM_HINTS MWM_DECOR_NONE
     * If the compositor/WM ignores the request (some do), it will draw
     * its decorations on top of the app's title bar widget — visually
     * redundant but not broken. Default is false (server-side decs). */
    bool                csd;
} FDK_WindowDesc;

#define FDK_WINDOW_POS_CENTER (-1)

FDK_Window *fdk_window_create(const FDK_WindowDesc *desc);
void        fdk_window_destroy(FDK_Window *win);
void        fdk_window_show(FDK_Window *win);
void        fdk_window_hide(FDK_Window *win);
void        fdk_window_set_title(FDK_Window *win, const char *title);
FDK_Size    fdk_window_get_size(FDK_Window *win);
void        fdk_window_request_redraw(FDK_Window *win);

/* Returns the window's current DPI scale factor (1.0 = 96 DPI, 2.0 =
 * 192 DPI, 1.5 = 144 DPI, etc.). Multiplied by 96 to get DPI.
 *
 * On X11: reads Xft.dpi from the X resource database at init time.
 * Overridden by the GDK_SCALE environment variable if set (matching
 * GDK/Qt behavior). All windows in the same X display share one
 * scale — per-monitor DPI on X11 is a future item.
 *
 * On Wayland: uses the wl_output scale of the output the window is
 * primarily on. With wp_fractional_scale_v1, the scale can be any
 * multiple of 1/120 (e.g. 1.25, 1.5, 1.75). Without that protocol,
 * only integer scales (1.0, 2.0, 3.0) are reported.
 *
 * When the scale changes mid-session (monitor hotplug, drag to a
 * different-DPI monitor, user changes display scale in GNOME/KDE
 * settings), FDK fires FDK_EVENT_SCALE_CHANGE. Apps should multiply
 * their font sizes and widget sizes by this value.
 *
 * Auto-scaling — where FDK internally multiplies all dimensions by
 * the scale so apps can stay in "logical pixels" — is planned for
 * v0.3. For now, apps that care about HiDPI must query this and
 * scale themselves.
 *
 * Returns 1.0 for a NULL window. */
float fdk_window_get_scale(FDK_Window *win);

/* Returns the DPI implied by the scale factor (scale * 96). */
int   fdk_window_get_dpi(FDK_Window *win);

/* Request the compositor/WM maximize, restore, fullscreen, or unfullscreen
 * this window. These are requests, not guarantees — the compositor may
 * ignore them, and the actual outcome (plus any other state change it
 * initiates on its own, e.g. the user maximizing via a WM keybind) is
 * reported back via FDK_EVENT_STATE_CHANGE, not by these calls returning.
 * A no-op on NULL win. */
void fdk_window_set_maximized(FDK_Window *win, bool maximized);
void fdk_window_set_fullscreen(FDK_Window *win, bool fullscreen);

/* Request that the window be iconified (minimized to the taskbar).
 * A no-op on NULL win. The window is not actually unmapped until the
 * WM/compositor processes the request; once it does, no further
 * events arrive for that window until the user restores it. */
void fdk_window_minimize(FDK_Window *win);

/* Begin an interactive move of the window, driven by the compositor/WM
 * itself (so you don't have to track mouse motion and reposition
 * manually). Call this from a MOUSE_DOWN handler on the titlebar
 * widget, passing the same FDK_Event you received — the platform
 * backend needs the underlying input serial/button to hand off to the
 * compositor. A no-op on NULL win or NULL ev. */
void fdk_window_begin_move(FDK_Window *win, const FDK_Event *ev);

/* Begin an interactive resize of the window, driven by the compositor/WM.
 * Call this from a MOUSE_DOWN handler when the user clicks on a window
 * edge/corner. 'edge' specifies which edge(s) to resize:
 *   1=top, 2=bottom, 3=left, 4=right,
 *   5=top-left, 6=top-right, 7=bottom-left, 8=bottom-right
 * Matches the _NET_WM_MOVERESIZE direction constants (EWMH spec). */
void fdk_window_begin_resize(FDK_Window *win, const FDK_Event *ev, int edge);

/* Directly resize the window to the given dimensions. Used internally
 * by the CSD resize tracker for live updates during edge drag. Can also
 * be called by apps that want to programmatically resize. */
void fdk_window_resize(FDK_Window *win, int w, int h);

/* Move and resize in one call. Used by the CSD resize tracker for
 * left/top edge drags where the window position also changes. */
void fdk_window_move_resize(FDK_Window *win, int x, int y, int w, int h);

/* ─── Drag and Drop ──────────────────────────────────────────────────────────
 * Register a callback to receive files dragged from outside the app
 * (e.g. from a file manager). The callback receives a URI list — a
 * newline-separated string of "file:///path/to/file" URIs. Parse with
 * fdk_uri_list_to_paths() or handle manually.
 *
 * On X11: implements the XDND protocol (XdndEnter/Position/Drop/Leave).
 * On Wayland: implements wl_data_device drop handling.
 * If the backend doesn't support DnD, this is a no-op.
 *
 * Example:
 *   void on_drop(FDK_Window *w, const char *uri_list, void *ud) {
 *       printf("Dropped: %s\n", uri_list);
 *   }
 *   fdk_window_set_drop_handler(win, on_drop, NULL);
 */
typedef void (*FDK_DropCb)(FDK_Window *win, const char *uri_list, void *ud);
void fdk_window_set_drop_handler(FDK_Window *win, FDK_DropCb cb, void *ud);

/* Current known state. Reflects the last FDK_EVENT_STATE_CHANGE seen for
 * this window (or the compositor's initial configure on Wayland / the
 * current _NET_WM_STATE property on X11 if queried before any change has
 * occurred). Returns false for a NULL win. */
bool fdk_window_is_maximized(FDK_Window *win);
bool fdk_window_is_fullscreen(FDK_Window *win);

/* Set the icon shown for this window in taskbars, window switchers, etc.
 * icon_name is an XDG icon theme name (e.g. "firefox", "text-editor") —
 * the same string that would appear in an app's .desktop file Icon= field.
 * On Wayland: uses xdg-toplevel-icon-v1 (no-op if compositor lacks support).
 * On X11: sets _NET_WM_ICON by scanning the system icon theme for the name.
 * NULL clears the icon, reverting to compositor/WM default. */
void fdk_window_set_icon_name(FDK_Window *win, const char *icon_name);

/* Set the window icon from a PNG file path (absolute or relative).
 * Unlike fdk_window_set_icon_name() which looks up the icon by name
 * in the XDG icon theme, this loads a specific PNG file directly.
 * Useful for apps that ship their own icon assets.
 * On Wayland: loads the PNG into an xdg_toplevel_icon (requires
 *   xdg-toplevel-icon-v1 protocol; no-op if unsupported).
 * On X11: decodes the PNG via stb_image and sets _NET_WM_ICON.
 * Requires FDK_WITH_STB_IMAGE (auto-enabled when stb_image.h is
 * vendored in third_party/stb/).
 * NULL path clears the icon. */
void fdk_window_set_icon_from_file(FDK_Window *win, const char *path);

/* ─── Event loop ─────────────────────────────────────────────────────────── */
/* Returns false when the application should quit */
bool fdk_poll_event(FDK_Event *out_event);
void fdk_wait_event(FDK_Event *out_event);
/* Returns false on timeout, true if event was produced. timeout_ms<0 = block forever */
bool fdk_wait_event_timeout(FDK_Event *out_event, int timeout_ms);

/* ─── Drawing (immediate‑mode, called between begin/end) ─────────────────── */
void fdk_begin_frame(FDK_Window *win);
void fdk_end_frame(FDK_Window *win);

void fdk_clear(FDK_Color color);
void fdk_fill_rect(FDK_Rect r, FDK_Color color);
void fdk_stroke_rect(FDK_Rect r, FDK_Color color, int thickness);
void fdk_fill_rect_rounded(FDK_Rect r, int radius, FDK_Color color);

/* ─── Gradient / shadow types (used by draw API and theme system) ──────── */
typedef struct {
    FDK_Color color;
    float     pos;      /* 0.0 … 1.0 */
} FDK_GradStop;

typedef enum {
    FDK_GRAD_NONE = 0,
    FDK_GRAD_LINEAR_V,
    FDK_GRAD_LINEAR_H,
} FDK_GradType;

typedef struct {
    FDK_GradType  type;
    FDK_GradStop  stops[4];
    int           stop_count;
} FDK_Gradient;

typedef struct {
    int       offset_x, offset_y;
    int       blur;
    FDK_Color color;
    bool      enabled;
} FDK_Shadow;

/* Forward declaration — full definition in fdk_widget.h */
typedef struct FDK_ContextMenu FDK_ContextMenu;

/* Gradient-filled rounded rect — falls back to flat fill if backend lacks support */
void fdk_fill_rect_gradient(FDK_Rect r, int radius, const FDK_Gradient *gradient);
/* Draw a soft drop shadow behind r — renders before the widget fill */
void fdk_draw_shadow(FDK_Rect r, int radius, const FDK_Shadow *shadow);
void fdk_fill_circle(int cx, int cy, int radius, FDK_Color color);
void fdk_draw_line(int x0, int y0, int x1, int y1, FDK_Color color, int thickness);

/* ─── Text ───────────────────────────────────────────────────────────────── */
FDK_Font *fdk_font_load(const char *path, float size_px);
FDK_Font *fdk_font_load_memory(const uint8_t *data, size_t len, float size_px);
void      fdk_font_destroy(FDK_Font *font);

void      fdk_draw_text(FDK_Font *font, const char *utf8,
                        int x, int y, FDK_Color color);
FDK_Size  fdk_measure_text(FDK_Font *font, const char *utf8);

/* ─── Scissor / clipping ─────────────────────────────────────────────────── */
void fdk_push_clip(FDK_Rect r);
void fdk_pop_clip(void);

/* ─── Cursor ─────────────────────────────────────────────────────────────── */
typedef enum {
    FDK_CURSOR_DEFAULT = 0,
    FDK_CURSOR_POINTER,     /* hand — for buttons/links  */
    FDK_CURSOR_TEXT,        /* I-beam — for text inputs  */
    FDK_CURSOR_CROSSHAIR,
    FDK_CURSOR_MOVE,
    FDK_CURSOR_RESIZE_H,
    FDK_CURSOR_RESIZE_V,
    FDK_CURSOR_RESIZE_TL,   /* ↖↘ top-left ↔ bottom-right */
    FDK_CURSOR_RESIZE_TR,   /* ↗↙ top-right ↔ bottom-left */
    FDK_CURSOR_NOT_ALLOWED,
} FDK_Cursor;

void fdk_set_cursor(FDK_Cursor cursor);

/* ─── Clipboard ──────────────────────────────────────────────────────────── */
void  fdk_clipboard_set(const char *text);
char *fdk_clipboard_get(void);   /* returns malloc'd string — caller must free() */

/* ─── Utilities ──────────────────────────────────────────────────────────── */
uint64_t fdk_time_ms(void);   /* monotonic milliseconds */
void     fdk_sleep_ms(uint32_t ms);

/* ─── Animation / tween system ───────────────────────────────────────────── */
typedef enum {
    FDK_EASE_LINEAR = 0,
    FDK_EASE_IN_QUAD,
    FDK_EASE_OUT_QUAD,
    FDK_EASE_IN_OUT_QUAD,
    FDK_EASE_IN_CUBIC,
    FDK_EASE_OUT_CUBIC,
    FDK_EASE_IN_OUT_CUBIC,
    FDK_EASE_OUT_ELASTIC,
    FDK_EASE_OUT_BOUNCE,
} FDK_Easing;

typedef struct FDK_Tween FDK_Tween;

/* Create a tween from `from` to `to` over `duration_ms`.
 * on_update(value, ud) is called each tick. on_done(ud) called on completion.
 * Returns a handle valid until the tween completes or fdk_tween_cancel(). */
typedef void (*FDK_TweenUpdateCb)(float value, void *ud);
typedef void (*FDK_TweenDoneCb)(void *ud);

FDK_Tween *fdk_tween(float from, float to, uint32_t duration_ms,
                      FDK_Easing easing,
                      FDK_TweenUpdateCb on_update,
                      FDK_TweenDoneCb   on_done,
                      void *ud);
void       fdk_tween_cancel(FDK_Tween *t);
bool       fdk_tween_is_done(const FDK_Tween *t);
/* Advance all live tweens — call once per frame (fdk_ui_step does this) */
void       fdk__tweens_tick(void);

#ifdef __cplusplus
}
#endif
#endif /* FDK_H */
