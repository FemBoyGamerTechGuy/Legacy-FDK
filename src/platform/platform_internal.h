/*
 * platform_internal.h — FDK Platform Abstraction Layer (internal)
 */
#ifndef FDK_PLATFORM_INTERNAL_H
#define FDK_PLATFORM_INTERNAL_H

#include "fdk/fdk.h"

typedef struct FDK_PlatformWindow FDK_PlatformWindow;

typedef struct {
    const char *name;
    bool (*init)(void);
    void (*shutdown)(void);
    FDK_PlatformWindow *(*window_create)(const FDK_WindowDesc *desc);
    void                (*window_destroy)(FDK_PlatformWindow *pw);
    void                (*window_show)(FDK_PlatformWindow *pw);
    void                (*window_hide)(FDK_PlatformWindow *pw);
    void                (*window_set_title)(FDK_PlatformWindow *pw, const char *title);
    FDK_Size            (*window_get_size)(FDK_PlatformWindow *pw);
    /* Current DPI scale factor for this window. 1.0 = 96 DPI, 2.0 = 192 DPI.
     * See fdk_window_get_scale() docs in fdk.h. Returns 1.0 if the
     * backend doesn't track per-window scale (X11 currently returns
     * the global Xft.dpi for all windows). */
    float               (*window_get_scale)(FDK_PlatformWindow *pw);
    void                (*window_request_redraw)(FDK_PlatformWindow *pw);
    /* Window state: maximize/fullscreen requests and current-state queries.
     * Requests are fire-and-forget (compositor may ignore); queries return
     * the last state this backend observed, updated whenever a configure/
     * PropertyNotify reports a change. */
    void (*window_set_maximized)(FDK_PlatformWindow *pw, bool maximized);
    void (*window_set_fullscreen)(FDK_PlatformWindow *pw, bool fullscreen);
    bool (*window_is_maximized)(FDK_PlatformWindow *pw);
    bool (*window_is_fullscreen)(FDK_PlatformWindow *pw);
    /* Minimize (iconify) the window — request only, WM may ignore. */
    void (*window_minimize)(FDK_PlatformWindow *pw);
    /* Begin an interactive move driven by the compositor/WM. ev is the
     * FDK_EVENT_MOUSE_DOWN that triggered the move (the platform layer
     * needs the underlying input serial/button). May be a no-op on
     * platforms/backends that don't expose this. */
    void (*window_begin_move)(FDK_PlatformWindow *pw, const FDK_Event *ev);
    void (*window_begin_resize)(FDK_PlatformWindow *pw, const FDK_Event *ev, int edge);
    void (*window_resize)(FDK_PlatformWindow *pw, int w, int h);
    void (*window_move_resize)(FDK_PlatformWindow *pw, int x, int y, int w, int h);
    /* Toggle whether the compositor/WM should draw its own decorations.
     * Called once at create time when FDK_WindowDesc.csd is true; may
     * also be called later to flip the mode at runtime (some WMs will
     * only honor the request at map time). */
    void (*window_set_decorated)(FDK_PlatformWindow *pw, bool decorated);
    /* Drag-and-drop: set the callback that receives file drops. */
    void (*window_set_drop_handler)(FDK_PlatformWindow *pw,
                                     FDK_DropCb cb, void *ud);
    /* Set window taskbar/switcher icon by XDG icon name. NULL clears. */
    void (*window_set_icon_name)(FDK_PlatformWindow *pw, const char *name);
    /* Set icon from a specific PNG file path (decoded via stb_image). */
    void (*window_set_icon_file)(FDK_PlatformWindow *pw, const char *path);
    uint32_t *(*window_get_framebuffer)(FDK_PlatformWindow *pw, int *out_stride_px);
    void      (*window_present)(FDK_PlatformWindow *pw);
    bool (*gl_make_current)(FDK_PlatformWindow *pw);
    void (*gl_swap_buffers)(FDK_PlatformWindow *pw);
    bool (*poll_event)(FDK_Event *out);
    void (*wait_event)(FDK_Event *out);
    /* Returns false on timeout, true if event produced. timeout_ms<0 = block forever */
    bool (*wait_event_timeout)(FDK_Event *out, int timeout_ms);
    /* Called after window creation so backends can store the FDK_Window* */
    void (*window_register)(FDK_PlatformWindow *pw, struct FDK_Window *fdkw);
    /* Change the cursor shape */
    void  (*set_cursor)(FDK_Cursor cursor);
    /* Get root-relative mouse position (for CSD resize tracking) */
    void  (*get_root_mouse)(int *root_x, int *root_y);
    /* Clipboard */
    void  (*clipboard_set)(const char *text);
    char *(*clipboard_get)(void);   /* caller must free() */
} FDK_PlatformVTable;

extern const FDK_PlatformVTable fdk_platform_x11;
extern const FDK_PlatformVTable fdk_platform_wayland;
extern const FDK_PlatformVTable *fdk__platform;

/* True if the current platform supports client-side resize (X11).
 * False on Wayland (compositor controls resize via xdg_toplevel_resize). */
extern bool fdk__client_side_resize;

#endif /* FDK_PLATFORM_INTERNAL_H */
