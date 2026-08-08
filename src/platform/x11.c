#define _GNU_SOURCE
/*
 * x11.c — FDK X11 platform backend
 *
 * Uses Xlib for windowing and XKB (via libxkbcommon-x11 or plain Xlib)
 * for keyboard handling. No XDG, no D-Bus, no systemd.
 *
 * Software render path: uses XImage / MIT-SHM (fallback to plain XPutImage)
 * OpenGL path:          uses GLX
 */
#include "platform_internal.h"
#include "../core/core_internal.h"

#ifdef FDK_HAVE_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#ifdef FDK_HAVE_XCURSOR
#  include <X11/Xcursor/Xcursor.h>
#endif
#include <time.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#ifdef FDK_HAVE_OPENGL
#  include <GL/glx.h>
#endif
#ifdef FDK_WITH_STB_IMAGE
#  define STB_IMAGE_IMPLEMENTATION
#  include <stb_image.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ─── Platform-private window ────────────────────────────────────────────── */
struct FDK_PlatformWindow {
    Window   xwin;
    int      w, h;
    bool     mapped;
    /* Last _NET_WM_STATE known from the WM — updated on PropertyNotify
     * and used by the is_maximized/is_fullscreen vtable functions. */
    bool     maximized;
    bool     fullscreen;

    /* Software render resources */
    GC       gc;
    XImage  *ximage;
    uint32_t *pixels;   /* BGRA, row-major */
    int      stride_px;
    /* Backing pixmap — off-screen drawable that we XPutImage into,
     * then XCopyArea to the window. This eliminates flicker during
     * resize because the window never shows an intermediate state:
     * the old frame stays visible until the new frame is fully drawn
     * into the pixmap and atomically copied. */
    Pixmap   backing_pixmap;

#ifdef FDK_HAVE_OPENGL
    GLXContext  glx_ctx;
    GLXFBConfig glx_fbc;
#endif

    /* ── XDND (X Drag-and-Drop) state ──
     * Tracks the current drag operation. When a drag enters our window,
     * we store the source window and version so we can respond to
     * XdndPosition with XdndStatus, and request the data on XdndDrop. */
    Window      dnd_source;      /* window initiating the drag (0 = no drag) */
    int         dnd_version;     /* XDND protocol version the source supports */
    Atom        dnd_waiting_type;/* type we requested via XConvertSelection */
    FDK_DropCb  drop_cb;         /* user callback, NULL = no drop handling */
    void       *drop_ud;         /* user data for drop_cb */
};

/* ─── Global X11 state ───────────────────────────────────────────────────── */
static Display *s_dpy        = NULL;

/* ── HiDPI scale (global on X11 for v0.2) ──
 *
 * X11 doesn't have a clean per-window scale concept. The Xft.dpi
 * resource in the root window's RESOURCE_MANAGER property is a
 * display-wide setting. GDK/Qt also use it as a global default.
 *
 * We read it once at init() time. Per-monitor DPI (via XRandR) is a
 * future item — would require tracking which monitor each window is
 * primarily on and re-evaluating on monitor hotplug.
 *
 * GDK_SCALE env var (integer 1, 2, 3) overrides Xft.dpi, matching
 * GDK and Qt behavior. This is the standard escape hatch users
 * use to force a scale on a misconfigured system. */
static float    s_scale      = 1.0f;
static int      s_screen     = 0;
static Atom     s_wm_delete  = None;

/* ── XIM (X Input Method) — IME support for CJK etc. ──
 *
 * XIM is the X11-native input method protocol. IBus and Fcitx both
 * expose XIM bridges, so this works with whatever IME the user has
 * configured.
 *
 * s_xim is the input method (one per display). s_xic is the input
 * context — we use a single shared IC for v0.2, which is correct
 * for "only one widget has focus at a time" semantics. Per-widget
 * ICs would let us set the cursor position for over-the-spot
 * preedit rendering, but that's a v0.3 item.
 *
 * If XOpenIM fails (no IME running), s_xim is NULL and XIM is
 * disabled — KeyPress events fall through to normal XLookupString
 * handling, which works fine for Latin input. */
static XIM      s_xim        = NULL;
static XIC      s_xic        = NULL;

/* Buffer for IME commit text. The X11 backend writes committed text
 * here, then sets FDK_Event.ime_commit.text to point at it. Valid
 * until the next KeyPress. */
static char     s_ime_commit_buf[512];

/* Clipboard atoms */
static Atom     s_atom_clipboard;   /* CLIPBOARD selection              */
static Atom     s_atom_targets;     /* TARGETS — list supported formats */
static Atom     s_atom_utf8;        /* UTF8_STRING                      */
static Atom     s_atom_string;      /* STRING (plain ASCII fallback)    */
static Atom     s_atom_fdk_sel;     /* FDK_SELECTION — our temp prop    */

/* EWMH window state atoms — used to maximize/fullscreen and observe
 * the WM changing those states on us externally (PropertyNotify). */
static Atom     s_atom_net_wm_state;          /* _NET_WM_STATE            */
static Atom     s_atom_net_wm_state_max_v;    /* _NET_WM_STATE_MAXIMIZED_VERT */
static Atom     s_atom_net_wm_state_max_h;    /* _NET_WM_STATE_MAXIMIZED_HORZ */
static Atom     s_atom_net_wm_state_full;     /* _NET_WM_STATE_FULLSCREEN */

/* XDND (X Drag-and-Drop) atoms */
static Atom     s_atom_xdnd_aware;    /* XdndAware */
static Atom     s_atom_xdnd_enter;    /* XdndEnter */
static Atom     s_atom_xdnd_position; /* XdndPosition */
static Atom     s_atom_xdnd_status;   /* XdndStatus */
static Atom     s_atom_xdnd_leave;    /* XdndLeave */
static Atom     s_atom_xdnd_drop;     /* XdndDrop */
static Atom     s_atom_xdnd_finished; /* XdndFinished */
static Atom     s_atom_xdnd_selection;/* XdndSelection */
static Atom     s_atom_uri_list;      /* text/uri-list */

#define NET_WM_STATE_REMOVE 0
#define NET_WM_STATE_ADD    1

/* Clipboard state */
static char    *s_clipboard     = NULL;  /* text we own              */
static Window   s_clip_owner   = None;   /* window that owns CLIPBOARD */

/* Cursor cache — one Cursor per FDK_Cursor value */
static Cursor   s_cursors[10];           /* indexed by FDK_Cursor enum  */
static bool     s_cursors_loaded = false;

/* ─── GLX global state ──────────────────────────────────────────────────── */
#ifdef FDK_HAVE_OPENGL
static GLXFBConfig s_glx_fbc     = NULL;
static GLXContext  s_glx_ctx     = NULL; /* shared context */
static XVisualInfo *s_glx_vis    = NULL;

/* Initialise GLX — find a suitable FBConfig and create a shared context */
static bool x11_glx_init(void)
{
    if (s_glx_ctx) return true; /* already done */

    int attrs[] = {
        GLX_DOUBLEBUFFER,  True,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_ALPHA_SIZE,    8,
        GLX_DEPTH_SIZE,    0,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        None
    };

    int n = 0;
    GLXFBConfig *cfgs = glXChooseFBConfig(s_dpy, s_screen, attrs, &n);
    if (!cfgs || n == 0) {
        fprintf(stderr, "[FDK/X11/GL] No suitable GLX FBConfig found\n");
        return false;
    }
    s_glx_fbc = cfgs[0];
    XFree(cfgs);

    s_glx_vis = glXGetVisualFromFBConfig(s_dpy, s_glx_fbc);
    if (!s_glx_vis) {
        fprintf(stderr, "[FDK/X11/GL] glXGetVisualFromFBConfig failed\n");
        return false;
    }

    /* Try GL 3.3 core context first */
    typedef GLXContext (*glXCreateContextAttribsARBProc)
        (Display*, GLXFBConfig, GLXContext, Bool, const int*);
    glXCreateContextAttribsARBProc createCtx =
        (glXCreateContextAttribsARBProc)
        glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

    if (createCtx) {
        int ctx_attrs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
            GLX_CONTEXT_MINOR_VERSION_ARB, 3,
            GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            None
        };
        s_glx_ctx = createCtx(s_dpy, s_glx_fbc, NULL, True, ctx_attrs);
    }
    /* Fallback: legacy context */
    if (!s_glx_ctx)
        s_glx_ctx = glXCreateNewContext(s_dpy, s_glx_fbc,
                                        GLX_RGBA_TYPE, NULL, True);

    if (!s_glx_ctx) {
        fprintf(stderr, "[FDK/X11/GL] Failed to create GLX context\n");
        return false;
    }
    return true;
}
#endif /* FDK_HAVE_OPENGL */

/* ─── HiDPI scale detection ─────────────────────────────────────────────────
 *
 * Reads the RESOURCE_MANAGER property on the root window directly via
 * XGetWindowProperty, looking for "Xft.dpi:\t<value>". This is the same
 * source GDK and Qt use.
 *
 * We don't use XResourceManagerString() because that returns a value
 * cached at XOpenDisplay time — if the property changes between
 * XOpenDisplay and our scale query (e.g. the user runs xrdb), we'd
 * miss the update. Direct XGetWindowProperty reads the current value.
 *
 * The GDK_SCALE env var, if set to a positive integer 1..8, overrides
 * Xft.dpi entirely. This matches GDK and Qt behavior, and is the
 * standard escape hatch users use to force a scale on a misconfigured
 * system.
 *
 * Returns 1.0 if neither GDK_SCALE nor Xft.dpi is set / parseable. */
static float x11_detect_scale(void)
{
    /* 0. FDK_SCALE env var overrides everything (works on both X11 and Wayland) */
    const char *fdk_scale = getenv("FDK_SCALE");
    if (fdk_scale && fdk_scale[0]) {
        char *end = NULL;
        long v = strtol(fdk_scale, &end, 10);
        if (end != fdk_scale && v >= 1 && v <= 8) {
            return (float)v;
        }
    }

    /* 1. GDK_SCALE env var (matches GDK/Qt behavior) */
    const char *gdk_scale = getenv("GDK_SCALE");
    if (gdk_scale && gdk_scale[0]) {
        char *end = NULL;
        long v = strtol(gdk_scale, &end, 10);
        if (end != gdk_scale && v >= 1 && v <= 8) {
            return (float)v;
        }
    }

    /* 2. Read RESOURCE_MANAGER property from root window directly */
    Atom res_mgr = XInternAtom(s_dpy, "RESOURCE_MANAGER", True);
    if (res_mgr == None) return 1.0f;

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(s_dpy, RootWindow(s_dpy, s_screen),
                            res_mgr, 0, 65536, False, XA_STRING,
                            &actual_type, &actual_format,
                            &nitems, &bytes_after, &data) != Success) {
        return 1.0f;
    }
    if (!data || actual_type != XA_STRING || nitems == 0) {
        if (data) XFree(data);
        return 1.0f;
    }

    /* Parse line-by-line for "Xft.dpi:" followed by whitespace and a float.
     * The RESOURCE_MANAGER format is "key:\tvalue\n" per resource. */
    float scale = 1.0f;
    const char *p = (const char *)data;
    const char *end = p + nitems;
    while (p < end) {
        /* Skip whitespace at start of line */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (p >= end) break;

        /* Match "Xft.dpi" */
        if (p + 7 <= end && strncmp(p, "Xft.dpi", 7) == 0) {
            const char *q = p + 7;
            /* Skip ':' and whitespace */
            while (q < end && (*q == ':' || *q == ' ' || *q == '\t')) q++;
            /* Parse float */
            char *strtod_end = NULL;
            /* strtod needs a NUL-terminated string, but our data isn't.
             * Copy the rest into a small local buffer. */
            char numbuf[64];
            int copy_len = (int)(end - q);
            if (copy_len > (int)sizeof(numbuf) - 1) copy_len = (int)sizeof(numbuf) - 1;
            memcpy(numbuf, q, copy_len);
            numbuf[copy_len] = '\0';
            double dpi = strtod(numbuf, &strtod_end);
            if (strtod_end != numbuf && dpi > 0.0 && dpi < 1000.0) {
                scale = (float)(dpi / 96.0);
                if (scale < 0.5f) scale = 0.5f;
                if (scale > 8.0f) scale = 8.0f;
                break;  /* first match wins */
            }
        }

        /* Skip to end of this line */
        while (p < end && *p != '\n') p++;
    }

    XFree(data);
    return scale;
}

/* ─── Init / shutdown ────────────────────────────────────────────────────── */
static bool x11_init(void)
{
    s_dpy = XOpenDisplay(NULL);
    if (!s_dpy) {
        fprintf(stderr, "[FDK/X11] Cannot open display '%s'\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
        return false;
    }
    s_screen     = DefaultScreen(s_dpy);
    s_wm_delete  = XInternAtom(s_dpy, "WM_DELETE_WINDOW", False);

    /* Detect HiDPI scale from Xft.dpi or GDK_SCALE */
    s_scale = x11_detect_scale();
    if (s_scale != 1.0f) {
        fprintf(stderr, "[FDK/X11] HiDPI scale: %.2f (%d DPI)\n",
                s_scale, (int)(s_scale * 96.0f + 0.5f));
    }

    /* Enable XKB key repeat control */
    XkbSetDetectableAutoRepeat(s_dpy, True, NULL);

    /* Intern clipboard atoms */
    s_atom_clipboard = XInternAtom(s_dpy, "CLIPBOARD",       False);
    s_atom_targets   = XInternAtom(s_dpy, "TARGETS",         False);
    s_atom_utf8      = XInternAtom(s_dpy, "UTF8_STRING",      False);
    s_atom_string    = XInternAtom(s_dpy, "STRING",           False);
    s_atom_fdk_sel   = XInternAtom(s_dpy, "FDK_SELECTION",   False);

    /* Intern EWMH window-state atoms */
    s_atom_net_wm_state      = XInternAtom(s_dpy, "_NET_WM_STATE",              False);
    s_atom_net_wm_state_max_v = XInternAtom(s_dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    s_atom_net_wm_state_max_h = XInternAtom(s_dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    s_atom_net_wm_state_full  = XInternAtom(s_dpy, "_NET_WM_STATE_FULLSCREEN",   False);

    /* Intern XDND atoms for drag-and-drop support */
    s_atom_xdnd_aware     = XInternAtom(s_dpy, "XdndAware",     False);
    s_atom_xdnd_enter     = XInternAtom(s_dpy, "XdndEnter",     False);
    s_atom_xdnd_position  = XInternAtom(s_dpy, "XdndPosition",  False);
    s_atom_xdnd_status    = XInternAtom(s_dpy, "XdndStatus",    False);
    s_atom_xdnd_leave     = XInternAtom(s_dpy, "XdndLeave",     False);
    s_atom_xdnd_drop      = XInternAtom(s_dpy, "XdndDrop",      False);
    s_atom_xdnd_finished  = XInternAtom(s_dpy, "XdndFinished",  False);
    s_atom_xdnd_selection = XInternAtom(s_dpy, "XdndSelection", False);
    s_atom_uri_list       = XInternAtom(s_dpy, "text/uri-list", False);

    /* Open X Input Method for IME support. We use the
     * XIMPreeditNothing | XIMStatusNothing style, which means the IME
     * draws its own preedit window (usually at the bottom of the
     * screen) — we don't have to render preedit ourselves. This is
     * the simplest style and works with every XIM-capable IME
     * (IBus, Fcitx, SCIM, uim, etc.).
     *
     * Failure to open XIM is non-fatal: it just means no IME is
     * running, so CJK input won't work but Latin input still does.
     * The XIM_MODIFIER env var (or XMODIFIERS in the user's env)
     * selects which IME to use; if unset, Xlib uses the default. */
    s_xim = XOpenIM(s_dpy, NULL, NULL, NULL);
    if (s_xim) {
        /* Create a single shared input context. We use the root window
         * as the client window; XFilterEvent will route events to this
         * IC. The focus is set per-keypress below in the KeyPress handler. */
        s_xic = XCreateIC(s_xim,
                          XNInputStyle,
                              XIMPreeditNothing | XIMStatusNothing,
                          XNClientWindow,
                              RootWindow(s_dpy, s_screen),
                          NULL);
        if (s_xic) {
            fprintf(stderr, "[FDK/X11] XIM opened — IME input enabled\n");
        } else {
            fprintf(stderr, "[FDK/X11] XCreateIC failed — IME disabled\n");
            XCloseIM(s_xim);
            s_xim = NULL;
        }
    } else {
        fprintf(stderr, "[FDK/X11] XOpenIM failed — IME disabled (no IME running?)\n");
    }

    return true;
}

static void x11_shutdown(void)
{
#ifdef FDK_HAVE_OPENGL
    if (s_glx_ctx) { glXDestroyContext(s_dpy, s_glx_ctx); s_glx_ctx = NULL; }
    if (s_glx_vis) { XFree(s_glx_vis); s_glx_vis = NULL; }
#endif
    if (s_xic) { XDestroyIC(s_xic); s_xic = NULL; }
    if (s_xim) { XCloseIM(s_xim); s_xim = NULL; }
    free(s_clipboard); s_clipboard = NULL;
    if (s_dpy) { XCloseDisplay(s_dpy); s_dpy = NULL; }
}

/* ─── Window creation ────────────────────────────────────────────────────── */
static FDK_PlatformWindow *x11_window_create(const FDK_WindowDesc *desc)
{
    FDK_PlatformWindow *pw = calloc(1, sizeof *pw);
    if (!pw) return NULL;

    pw->w = desc->w;
    pw->h = desc->h;

    int x = desc->x == FDK_WINDOW_POS_CENTER
                ? (DisplayWidth(s_dpy,s_screen)  - desc->w) / 2
                : desc->x;
    int y = desc->y == FDK_WINDOW_POS_CENTER
                ? (DisplayHeight(s_dpy,s_screen) - desc->h) / 2
                : desc->y;

    /* Decide GL vs software — initialise GLX if needed */
    bool use_gl = false;
#ifdef FDK_HAVE_OPENGL
    use_gl = (desc->render == FDK_RENDER_OPENGL ||
              desc->render == FDK_RENDER_AUTO);
    if (use_gl && !x11_glx_init()) {
        fprintf(stderr, "[FDK/X11] GLX init failed, falling back to software\n");
        use_gl = false;
    }
#endif

    /* Choose visual — GLX needs its own, software uses default */
    Visual *vis;
    int     depth;
    Colormap cmap;

#ifdef FDK_HAVE_OPENGL
    if (use_gl && s_glx_vis) {
        vis   = s_glx_vis->visual;
        depth = s_glx_vis->depth;
        cmap  = XCreateColormap(s_dpy, RootWindow(s_dpy, s_screen),
                                vis, AllocNone);
    } else
#endif
    {
        vis   = DefaultVisual(s_dpy, s_screen);
        depth = DefaultDepth(s_dpy, s_screen);
        cmap  = DefaultColormap(s_dpy, s_screen);
    }

    XSetWindowAttributes attrs = {0};
    attrs.colormap       = cmap;
    attrs.bit_gravity    = NorthWestGravity; /* keep old pixels on resize — don't clear */
    attrs.event_mask     = ExposureMask | StructureNotifyMask
                         | KeyPressMask | KeyReleaseMask
                         | ButtonPressMask | ButtonReleaseMask
                         | PointerMotionMask | PropertyChangeMask;

    /* Deliberately NOT setting CWBackPixel — without a background,
     * the X server does NOT fill the window on resize. Combined with
     * NorthWestGravity, old pixels stay visible and new areas show
     * whatever's behind the window until FDK paints. Zero flicker. */
    unsigned long attr_mask = CWColormap | CWBitGravity | CWEventMask;

    pw->xwin = XCreateWindow(
        s_dpy, RootWindow(s_dpy, s_screen),
        x, y, desc->w, desc->h, 0,
        depth, InputOutput, vis,
        attr_mask, &attrs);

    if (!pw->xwin) { free(pw); return NULL; }

    /* WM protocols */
    XSetWMProtocols(s_dpy, pw->xwin, &s_wm_delete, 1);
    XStoreName(s_dpy, pw->xwin, desc->title ? desc->title : "FDK");

    /* Advertise XDND awareness so drag-source apps know we accept drops.
     * The property value is the XDND version we support (5 = latest). */
    Atom xdnd_version = 5;
    XChangeProperty(s_dpy, pw->xwin, s_atom_xdnd_aware, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&xdnd_version, 1);

    /* Size hints — ALWAYS call XSetWMNormalHints, even for resizable
     * windows. Without this call, some WMs (XFWM included) default to
     * a fixed-size window because the absence of hints is interpreted
     * as "don't resize me". Setting PMinSize (even a small one) tells
     * the WM "yes, this window is resizable, just not below this size". */
    {
        XSizeHints *sh = XAllocSizeHints();
        if (!desc->resizable) {
            sh->flags      = PMinSize | PMaxSize;
            sh->min_width  = sh->max_width  = desc->w;
            sh->min_height = sh->max_height = desc->h;
        } else {
            int mw = desc->min_w > 0 ? desc->min_w : 1;
            int mh = desc->min_h > 0 ? desc->min_h : 1;
            sh->flags      = PMinSize;
            sh->min_width  = mw;
            sh->min_height = mh;
        }
        XSetWMNormalHints(s_dpy, pw->xwin, sh);
        XFree(sh);
    }

#ifdef FDK_HAVE_OPENGL
    if (use_gl) {
        /* Create a per-window GLX context that shares with the global one */
        pw->glx_ctx = glXCreateNewContext(s_dpy, s_glx_fbc,
                                           GLX_RGBA_TYPE, s_glx_ctx, True);
        pw->glx_fbc = s_glx_fbc;
        /* Make current so the GL renderer can init */
        glXMakeCurrent(s_dpy, pw->xwin, pw->glx_ctx);
        /* vsync — look up glXSwapIntervalEXT at runtime, ignore if absent */
        {
            typedef void (*PFNGLXSWAPINTERVALEXTPROC)(Display*, GLXDrawable, int);
            PFNGLXSWAPINTERVALEXTPROC swapInterval =
                (PFNGLXSWAPINTERVALEXTPROC)
                glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
            if (swapInterval)
                swapInterval(s_dpy, pw->xwin, 1);
        }
    } else
#endif
    {
        /* Software-render pixel buffer */
        pw->stride_px = desc->w;
        pw->pixels    = calloc((size_t)desc->w * desc->h, 4);
        pw->gc        = XCreateGC(s_dpy, pw->xwin, 0, NULL);
        pw->ximage    = XCreateImage(
            s_dpy, vis, depth, ZPixmap, 0,
            (char *)pw->pixels, desc->w, desc->h, 32, desc->w * 4);
        /* Create backing pixmap for double-buffering (flicker-free) */
        pw->backing_pixmap = XCreatePixmap(s_dpy, pw->xwin, desc->w, desc->h, depth);
    }

    XFlush(s_dpy);
    return pw;
}

static void x11_window_destroy(FDK_PlatformWindow *pw)
{
    if (!pw) return;
#ifdef FDK_HAVE_OPENGL
    if (pw->glx_ctx) {
        glXMakeCurrent(s_dpy, None, NULL);
        glXDestroyContext(s_dpy, pw->glx_ctx);
        pw->glx_ctx = NULL;
    }
#endif
    if (pw->ximage) {
        pw->ximage->data = NULL;
        XDestroyImage(pw->ximage);
    }
    free(pw->pixels);
    if (pw->backing_pixmap) XFreePixmap(s_dpy, pw->backing_pixmap);
    if (pw->gc)   XFreeGC(s_dpy, pw->gc);
    if (pw->xwin) XDestroyWindow(s_dpy, pw->xwin);
    free(pw);
}

static void x11_window_show(FDK_PlatformWindow *pw)
{
    XMapRaised(s_dpy, pw->xwin);
    XFlush(s_dpy);
    pw->mapped = true;
}

static void x11_window_hide(FDK_PlatformWindow *pw)
{
    XUnmapWindow(s_dpy, pw->xwin);
    XFlush(s_dpy);
    pw->mapped = false;
}

static void x11_window_set_title(FDK_PlatformWindow *pw, const char *t)
{
    XStoreName(s_dpy, pw->xwin, t);
    XFlush(s_dpy);
}

/* Return the global X11 HiDPI scale. v0.2 doesn't track per-window
 * scale on X11 (would need XRandR per-output DPI + monitor-enter/leave
 * tracking). All windows share s_scale, which was read once at init()
 * from Xft.dpi or GDK_SCALE. */
static float x11_window_get_scale(FDK_PlatformWindow *pw)
{
    (void)pw;
    return s_scale;
}

static FDK_Size x11_window_get_size(FDK_PlatformWindow *pw)
{
    /* Return our cached size, NOT a fresh XGetWindowAttributes query.
     *
     * [BUGFIX v9 — the real maximize crash fix]
     *
     * The previous code called XGetWindowAttributes() which queries the
     * X server for the actual current window size. That's a race:
     *
     *   1. FDK sends _NET_WM_STATE_ADD (maximize request) to XFWM
     *   2. XFWM resizes the window on the X server side IMMEDIATELY
     *   3. The ConfigureNotify event is queued but not yet delivered
     *      to FDK's event loop
     *   4. fdk_ui_step() at its top calls fdk_window_get_size() → this
     *      function → XGetWindowAttributes → returns the NEW size
     *      (e.g. 1920x1080)
     *   5. fdk_ui_step's size-comparison sees root->rect.w != 1920 →
     *      calls do_layout() for 1920x1080
     *   6. drain loop runs, finds no events (ConfigureNotify not here)
     *   7. fdk_ui_paint() paints into pw->pixels which is STILL
     *      allocated for the old 480x320 → heap buffer overflow →
     *      segfault
     *
     * The v8 fix (reallocate pw->pixels in ConfigureNotify) doesn't
     * help because the crash happens BEFORE ConfigureNotify arrives.
     *
     * The real fix: return pw->w/h (our cached size), which is only
     * updated inside the ConfigureNotify handler — atomically with
     * the buffer reallocation. This guarantees the size we report
     * always matches the buffer we have. The widget layer may be a
     * frame or two behind the actual X server size during a resize,
     * but that's a one-frame visual lag, not a crash.
     *
     * Side benefit: this is also faster (no round-trip to the X server
     * on every fdk_ui_step call). */
    return (FDK_Size){ pw->w, pw->h };
}

static void x11_window_request_redraw(FDK_PlatformWindow *pw)
{
    XEvent ev = {0};
    ev.type           = Expose;
    ev.xexpose.window = pw->xwin;
    ev.xexpose.count  = 0;
    XSendEvent(s_dpy, pw->xwin, False, ExposureMask, &ev);
    XFlush(s_dpy);
}

/* Send an EWMH _NET_WM_STATE client message to the root window.
 * atom2 may be None (0) when only one state atom is being changed.
 * action is NET_WM_STATE_ADD or NET_WM_STATE_REMOVE. */
static void x11_send_wm_state(FDK_PlatformWindow *pw,
                               int action, Atom atom1, Atom atom2)
{
    XEvent xev = {0};
    xev.xclient.type         = ClientMessage;
    xev.xclient.window       = pw->xwin;
    xev.xclient.message_type = s_atom_net_wm_state;
    xev.xclient.format       = 32;
    xev.xclient.data.l[0]   = action;
    xev.xclient.data.l[1]   = (long)atom1;
    xev.xclient.data.l[2]   = (long)atom2;
    xev.xclient.data.l[3]   = 1; /* source: normal application */
    xev.xclient.data.l[4]   = 0;
    XSendEvent(s_dpy, DefaultRootWindow(s_dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    XFlush(s_dpy);
}

static void x11_window_set_maximized(FDK_PlatformWindow *pw, bool m)
{
    /* Maximize requires both VERT and HORZ atoms — pass them together in
     * one message so the WM treats it as a single atomic operation. */
    x11_send_wm_state(pw,
                      m ? NET_WM_STATE_ADD : NET_WM_STATE_REMOVE,
                      s_atom_net_wm_state_max_v,
                      s_atom_net_wm_state_max_h);
}

static void x11_window_set_fullscreen(FDK_PlatformWindow *pw, bool f)
{
    x11_send_wm_state(pw,
                      f ? NET_WM_STATE_ADD : NET_WM_STATE_REMOVE,
                      s_atom_net_wm_state_full, None);
}

static bool x11_window_is_maximized(FDK_PlatformWindow *pw)
{
    return pw->maximized;
}

static bool x11_window_is_fullscreen(FDK_PlatformWindow *pw)
{
    return pw->fullscreen;
}

/* ─── CSD: Motif WM hints ──────────────────────────────────────────────────
 * The Motif WM hints (_MOTIF_WM_HINTS, originally from mwm/2b) are the
 * de-facto standard way to tell an X11 WM to suppress its own window
 * decorations. Despite the "Motif" name, every modern EWMH-compliant WM
 * (Openbox, i3, awesome, KWin, Mutter/X11, XFWM, etc.) still honors
 * them. The struct layout below matches the original Motif definition;
 * it is not governed by an X consortium spec but is universally stable.
 *
 * fields: flags, functions, decorations, input_mode, status
 *   flags      — which fields below are meaningful (MWM_HINTS_DECORATIONS)
 *   functions  — MWM_FUNC_* bitset (we leave it at "all", so the WM
 *                still lets the user close/minimize/move from menus etc.)
 *   decorations — MWM_DECOR_* bitset; 0 means "no decorations at all",
 *                 which is exactly what CSD wants.
 *   input_mode — only relevant for modal dialogs, 0 for normal windows
 *   status     — reserved, always 0
 */
#define MWM_HINTS_DECORATIONS (1L << 1)

#define MWM_DECOR_ALL         (1L << 0)

static Atom s_atom_motif_wm_hints; /* _MOTIF_WM_HINTS — interned in x11_init */

static void x11_window_set_decorated(FDK_PlatformWindow *pw, bool decorated)
{
    if (!s_dpy || !pw->xwin) return;
    if (!s_atom_motif_wm_hints)
        s_atom_motif_wm_hints = XInternAtom(s_dpy, "_MOTIF_WM_HINTS", False);

    /* 5 longs, per the Motif mwm hint struct.
     *
     * IMPORTANT: only set MWM_HINTS_DECORATIONS in the flags field,
     * NOT MWM_HINTS_FUNCTIONS. Setting MWM_HINTS_FUNCTIONS (even with
     * MWM_FUNC_ALL) causes some WMs (including XFWM) to restrict the
     * window's functionality — specifically, resize gets disabled
     * because the WM interprets the functions field as an exhaustive
     * whitelist. By only setting the decorations flag, the WM uses
     * its default function set (which always includes resize, move,
     * minimize, maximize, close). */
    long hints[5];
    hints[0] = MWM_HINTS_DECORATIONS;         /* flags: only decorations */
    hints[1] = 0;                              /* functions: not set (WM default) */
    hints[2] = decorated ? MWM_DECOR_ALL : 0; /* decorations              */
    hints[3] = 0;                              /* input_mode               */
    hints[4] = 0;                              /* status                   */

    XChangeProperty(s_dpy, pw->xwin, s_atom_motif_wm_hints,
                    s_atom_motif_wm_hints, 32, PropModeReplace,
                    (unsigned char *)hints, 5);
    XFlush(s_dpy);
}

/* ─── Minimize (iconify) ───────────────────────────────────────────────────
 * XIconifyWindow is the standard ICCCM way to ask the WM to iconify a
 * window. It works by sending a WM_CHANGE_STATE ClientMessage to the
 * root window with IconicState — XIconifyWindow wraps that for us. */
static void x11_window_minimize(FDK_PlatformWindow *pw)
{
    if (!s_dpy || !pw->xwin) return;
    XIconifyWindow(s_dpy, pw->xwin, s_screen);
    XFlush(s_dpy);
}

/* ─── Interactive move via _NET_WM_MOVERESIZE ──────────────────────────────
 * The EWMH _NET_WM_MOVERESIZE ClientMessage asks the WM to begin an
 * interactive move or resize of a window, driven by pointer motion on
 * the WM's side (so the client doesn't have to track pointer motion
 * itself, and the move uses the WM's own move logic — snap-to-edge,
 * alt-tab behavior, etc.). The WM expects the message to be sent in
 * response to a button-press on the window; we pass the button-press
 * coordinates so the WM knows where the grab started.
 *
 * direction _NET_WM_MOVERESIZE_MOVE (8) is "free move with no resize".
 * button 1 is the left button — required by the spec, since some WMs
 * track the originating button to know when to end the grab on release. */
#define _NET_WM_MOVERESIZE_MOVE 8

static void x11_window_begin_move(FDK_PlatformWindow *pw, const FDK_Event *ev)
{
    if (!s_dpy || !pw->xwin || !ev) return;
    if (ev->type != FDK_EVENT_MOUSE_DOWN) return;

    /* _NET_WM_MOVERESIZE: see EWMH spec.
     * data.l[0] = root_x   (relative to root)
     * data.l[1] = root_y
     * data.l[2] = direction (_NET_WM_MOVERESIZE_MOVE = 8)
     * data.l[3] = button (1-based, for grab tracking)
     * data.l[4] = source (1 = normal app, 2 = user action)
     *
     * ev->mouse.x/y are window-relative; convert to root-relative by
     * translating through XTranslateCoordinates.
     *
     * XUngrabPointer before sending: when FDK receives a ButtonPress
     * event, X11 automatically grants FDK an "active grab" on the
     * pointer. This auto-grab blocks the WM from grabbing the pointer
     * when it processes _NET_WM_MOVERESIZE — so the WM's grab fails,
     * the move never starts, and the user can't drag the window.
     * Calling XUngrabPointer releases the auto-grab so the WM can take
     * over. This is exactly what GTK does in gdk_window_begin_move_drag(). */
    int root_x, root_y;
    Window child;
    XTranslateCoordinates(s_dpy, pw->xwin,
                          DefaultRootWindow(s_dpy),
                          ev->mouse.x, ev->mouse.y,
                          &root_x, &root_y, &child);

    /* Release the ButtonPress auto-grab so the WM can take over. */
    XUngrabPointer(s_dpy, CurrentTime);
    XFlush(s_dpy);

    Atom net_wm_moveresize = XInternAtom(s_dpy, "_NET_WM_MOVERESIZE", False);
    XEvent xev = {0};
    xev.xclient.type         = ClientMessage;
    xev.xclient.window       = pw->xwin;
    xev.xclient.message_type = net_wm_moveresize;
    xev.xclient.format       = 32;
    xev.xclient.data.l[0]    = root_x;
    xev.xclient.data.l[1]    = root_y;
    xev.xclient.data.l[2]    = _NET_WM_MOVERESIZE_MOVE;
    xev.xclient.data.l[3]    = 1; /* button 1 (left) */
    xev.xclient.data.l[4]    = 2; /* source: user action */

    XSendEvent(s_dpy, DefaultRootWindow(s_dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    XFlush(s_dpy);
}

/* Interactive resize via _NET_WM_MOVERESIZE — same mechanism as begin_move
 * but with a different direction constant. The 'edge' parameter maps
 * directly to the EWMH direction values:
 *   0=_NET_WM_MOVERESIZE_SIZE_TOPLEFT, 1=TOP, 2=TOPRIGHT,
 *   3=RIGHT, 4=BOTTOMRIGHT, 5=BOTTOM, 6=BOTTOMLEFT, 7=LEFT
 * Our public API uses 1-8 (1=top, 2=bottom, etc.) so we subtract 1. */
static void x11_window_begin_resize(FDK_PlatformWindow *pw,
                                     const FDK_Event *ev, int edge)
{
    if (!s_dpy || !pw->xwin || !ev) return;
    if (ev->type != FDK_EVENT_MOUSE_DOWN) return;

    int root_x, root_y;
    Window child;
    XTranslateCoordinates(s_dpy, pw->xwin,
                          DefaultRootWindow(s_dpy),
                          ev->mouse.x, ev->mouse.y,
                          &root_x, &root_y, &child);

    XUngrabPointer(s_dpy, CurrentTime);
    XFlush(s_dpy);

    /* EWMH _NET_WM_MOVERESIZE direction values:
     *   0 = SIZE_TOPLEFT, 1 = SIZE_TOP, 2 = SIZE_TOPRIGHT
     *   3 = SIZE_RIGHT,   4 = SIZE_BOTTOMRIGHT, 5 = SIZE_BOTTOM
     *   6 = SIZE_BOTTOMLEFT, 7 = SIZE_LEFT
     *
     * Our public API uses 1-8:
     *   1=top, 2=bottom, 3=left, 4=right
     *   5=top-left, 6=top-right, 7=bottom-left, 8=bottom-right
     *
     * Map them to EWMH values with a lookup table — NOT edge-1,
     * because the orderings don't match. */
    static const int ewmh_dir[9] = {
        -1,             /* 0 = unused */
        1,              /* 1=top       → EWMH 1 (SIZE_TOP) */
        5,              /* 2=bottom    → EWMH 5 (SIZE_BOTTOM) */
        7,              /* 3=left      → EWMH 7 (SIZE_LEFT) */
        3,              /* 4=right     → EWMH 3 (SIZE_RIGHT) */
        0,              /* 5=top-left  → EWMH 0 (SIZE_TOPLEFT) */
        2,              /* 6=top-right → EWMH 2 (SIZE_TOPRIGHT) */
        6,              /* 7=bot-left  → EWMH 6 (SIZE_BOTTOMLEFT) */
        4,              /* 8=bot-right → EWMH 4 (SIZE_BOTTOMRIGHT) */
    };
    if (edge < 1 || edge > 8) return;
    int direction = ewmh_dir[edge];

    Atom net_wm_moveresize = XInternAtom(s_dpy, "_NET_WM_MOVERESIZE", False);
    XEvent xev = {0};
    xev.xclient.type         = ClientMessage;
    xev.xclient.window       = pw->xwin;
    xev.xclient.message_type = net_wm_moveresize;
    xev.xclient.format       = 32;
    xev.xclient.data.l[0]    = root_x;
    xev.xclient.data.l[1]    = root_y;
    xev.xclient.data.l[2]    = direction;
    xev.xclient.data.l[3]    = 1; /* button 1 (left) */
    xev.xclient.data.l[4]    = 2; /* source: user action */

    XSendEvent(s_dpy, DefaultRootWindow(s_dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    XFlush(s_dpy);
}

/* Direct resize — calls XResizeWindow which triggers a ConfigureNotify,
 * which triggers layout + paint in fdk_ui_step. Used by the CSD
 * client-side resize tracker for live updates during edge drag. */
static void x11_window_resize(FDK_PlatformWindow *pw, int w, int h)
{
    if (!s_dpy || !pw->xwin) return;
    XResizeWindow(s_dpy, pw->xwin, w, h);
    XFlush(s_dpy);
}

static void x11_window_move_resize(FDK_PlatformWindow *pw, int x, int y, int w, int h)
{
    if (!s_dpy || !pw->xwin) return;
    XMoveResizeWindow(s_dpy, pw->xwin, x, y, w, h);
    XFlush(s_dpy);
}

/* Scan the XDG icon theme directories for a PNG named icon_name at any
 * common size. Returns true and fills path (up to path_max bytes) on
 * success. Checks $HOME/.local/share/icons, $XDG_DATA_DIRS/icons, and
 * /usr/share/pixmaps as fallback, consistent with the XDG icon theme spec.
 * This function is already correct and complete; it's used by
 * x11_window_set_icon_name once an image decoder is available. */
static bool x11_find_icon_file(const char *icon_name,
                                char *path, size_t path_max)
{
    const char *home       = getenv("HOME");
    const char *data_dirs  = getenv("XDG_DATA_DIRS");
    if (!data_dirs || !data_dirs[0])
        data_dirs = "/usr/local/share:/usr/share";

    /* Common sizes to probe, largest first so callers get the best fit. */
    static const int sizes[] = { 256, 128, 64, 48, 32, 22, 16, 0 };
    /* Subdirectory layouts used by most themes. */
    static const char *const subdirs[] = {
        "apps", "mimetypes", "places", "status", "devices", NULL
    };

    /* Build a search-path list: ~/.local/share/icons, then each
     * colon-separated entry in XDG_DATA_DIRS. */
    char home_icons[512];
    if (home) snprintf(home_icons, sizeof home_icons, "%s/.local/share/icons", home);

    /* Try every base directory. */
    const char *bases[32];
    int nb = 0;
    if (home && nb < 31) bases[nb++] = home_icons;

    /* Walk XDG_DATA_DIRS colon-separated entries */
    enum {
        DDCOPY_SIZE = 1024,
        /* tok below is a pointer *into* ddcopy, so in the worst case
         * (a single directory entry with no colons at all, filling the
         * whole buffer) it can be as long as DDCOPY_SIZE - 1 characters.
         * dirbuf must hold that plus the "/icons" suffix (6 chars) and
         * a null terminator -- +16 is deliberately generous slack over
         * the exact +7 needed, so this stays correct even if the
         * suffix below ever grows. Sized this way (derived from
         * DDCOPY_SIZE, not a plain literal), a plain 256 here used to
         * silently truncate on an unusually long single XDG_DATA_DIRS
         * entry (over ~249 chars) -- narrow in practice, but a real
         * gap, not just compiler noise: -Wformat-truncation was
         * correctly flagging that snprintf's output could legitimately
         * need up to 1030 bytes into a 256-byte destination. */
        DIRBUF_SIZE = DDCOPY_SIZE + 16,
    };
    char ddcopy[DDCOPY_SIZE];
    snprintf(ddcopy, sizeof ddcopy, "%s", data_dirs);
    char *tok = ddcopy, *end;
    while (tok && nb < 30) {
        end = strchr(tok, ':');
        if (end) *end = '\0';
        static char dirbuf[32][DIRBUF_SIZE];
        snprintf(dirbuf[nb], DIRBUF_SIZE, "%s/icons", tok);
        bases[nb] = dirbuf[nb];
        nb++;
        tok = end ? end + 1 : NULL;
    }
    bases[nb] = NULL;

    for (int b = 0; bases[b]; b++) {
        for (int s = 0; sizes[s]; s++) {
            for (int sub = 0; subdirs[sub]; sub++) {
                snprintf(path, path_max, "%s/hicolor/%dx%d/%s/%s.png",
                         bases[b], sizes[s], sizes[s], subdirs[sub], icon_name);
                if (access(path, R_OK) == 0) return true;
                /* Also check themed directories without hicolor — some
                 * themes use $base/$size/$subdir directly. */
                snprintf(path, path_max, "%s/%dx%d/%s/%s.png",
                         bases[b], sizes[s], sizes[s], subdirs[sub], icon_name);
                if (access(path, R_OK) == 0) return true;
            }
        }
        /* Scalable (SVG) — note we can't currently decode SVG, but record
         * the path in case a future caller handles it. */
        snprintf(path, path_max, "%s/hicolor/scalable/apps/%s.svg",
                 bases[b], icon_name);
        if (access(path, R_OK) == 0) return true;
    }

    /* Fallback: /usr/share/pixmaps/<name>.png */
    snprintf(path, path_max, "/usr/share/pixmaps/%s.png", icon_name);
    if (access(path, R_OK) == 0) return true;

    return false;
}

/* Forward declaration — set_icon_file is defined after set_icon_name */
static void x11_window_set_icon_file(FDK_PlatformWindow *pw, const char *path);

static void x11_window_set_icon_name(FDK_PlatformWindow *pw, const char *name)
{
    if (!name) {
        /* Clear the icon by deleting _NET_WM_ICON */
        Atom net_wm_icon = XInternAtom(s_dpy, "_NET_WM_ICON", False);
        XDeleteProperty(s_dpy, pw->xwin, net_wm_icon);
        XFlush(s_dpy);
        return;
    }

    char icon_path[1024];
    if (!x11_find_icon_file(name, icon_path, sizeof icon_path)) {
        /* Icon not found in theme — leave whatever the WM defaults to */
        return;
    }

    /* Found the icon file — decode and set it via the shared helper. */
    x11_window_set_icon_file(pw, icon_path);
}

/* Helper: decode a PNG file and set it as _NET_WM_ICON.
 * Used by both x11_window_set_icon_name (after icon-theme lookup)
 * and x11_window_set_icon_file (direct path). */
static void x11_window_set_icon_file(FDK_PlatformWindow *pw, const char *path)
{
    if (!path) {
        Atom net_wm_icon = XInternAtom(s_dpy, "_NET_WM_ICON", False);
        XDeleteProperty(s_dpy, pw->xwin, net_wm_icon);
        XFlush(s_dpy);
        return;
    }

#ifdef FDK_WITH_STB_IMAGE
    /* Check the file exists before passing to stbi_load — stbi_load
     * can crash on some systems if given a path that doesn't exist
     * or isn't readable, rather than cleanly returning NULL. */
    if (access(path, R_OK) != 0) {
        return;
    }

    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) return;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        stbi_image_free(data);
        return;
    }

    /* XChangeProperty with format=32 expects a long* array (8 bytes per
     * element on 64-bit), NOT uint32_t* (4 bytes). Using uint32_t* causes
     * X to read past the buffer → segfault. Classic X11 gotcha. */
    unsigned long count = 2 + (unsigned long)w * h;
    unsigned long *prop = malloc(count * sizeof *prop);
    if (!prop) { stbi_image_free(data); return; }

    prop[0] = (unsigned long)w;
    prop[1] = (unsigned long)h;
    for (int i = 0; i < w * h; i++) {
        unsigned char *px = &data[i * 4];
        prop[2 + i] = ((unsigned long)px[3] << 24) |
                      ((unsigned long)px[0] << 16) |
                      ((unsigned long)px[1] <<  8) |
                       (unsigned long)px[2];
    }

    Atom net_wm_icon = XInternAtom(s_dpy, "_NET_WM_ICON", False);
    XChangeProperty(s_dpy, pw->xwin, net_wm_icon, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)prop, (int)count);
    free(prop);
    stbi_image_free(data);
    XFlush(s_dpy);
#else
    (void)pw;
#endif
}

/* ─── Software framebuffer access ────────────────────────────────────────── */
static uint32_t *x11_get_framebuffer(FDK_PlatformWindow *pw, int *stride_px)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->glx_ctx) { *stride_px = 0; return NULL; } /* GL window — no CPU buffer */
#endif
    *stride_px = pw->stride_px;
    return pw->pixels;
}

static void x11_present(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->glx_ctx) return; /* GL windows present via gl_swap_buffers */
#endif
    if (!pw->mapped) return;
    if (!pw->backing_pixmap) {
        /* Fallback: direct XPutImage (may flicker during resize) */
        XPutImage(s_dpy, pw->xwin, pw->gc, pw->ximage,
                  0, 0, 0, 0, pw->w, pw->h);
    } else {
        /* Double-buffered: draw to off-screen pixmap, then atomically
         * copy to window. The window never shows an intermediate state. */
        XPutImage(s_dpy, pw->backing_pixmap, pw->gc, pw->ximage,
                  0, 0, 0, 0, pw->w, pw->h);
        XCopyArea(s_dpy, pw->backing_pixmap, pw->xwin, pw->gc,
                  0, 0, pw->w, pw->h, 0, 0);
    }
    XFlush(s_dpy);
}

/* ─── OpenGL ─────────────────────────────────────────────────────────────── */
static bool x11_gl_make_current(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (!pw->glx_ctx) return false;
    return glXMakeCurrent(s_dpy, pw->xwin, pw->glx_ctx) == True;
#else
    (void)pw; return false;
#endif
}

static void x11_gl_swap_buffers(FDK_PlatformWindow *pw)
{
#ifdef FDK_HAVE_OPENGL
    if (pw->glx_ctx)
        glXSwapBuffers(s_dpy, pw->xwin);
#else
    (void)pw;
#endif
}

/* ─── Key translation ────────────────────────────────────────────────────── */
static FDK_Key keysym_to_fdk(KeySym ks)
{
    switch (ks) {
    case XK_Escape:    return FDK_KEY_ESCAPE;
    case XK_Return:    return FDK_KEY_RETURN;
    case XK_space:     return FDK_KEY_SPACE;
    case XK_BackSpace: return FDK_KEY_BACKSPACE;
    case XK_Tab:       return FDK_KEY_TAB;
    case XK_Delete:    return FDK_KEY_DELETE;
    case XK_Left:      return FDK_KEY_LEFT;
    case XK_Right:     return FDK_KEY_RIGHT;
    case XK_Up:        return FDK_KEY_UP;
    case XK_Down:      return FDK_KEY_DOWN;
    case XK_Home:      return FDK_KEY_HOME;
    case XK_End:       return FDK_KEY_END;
    case XK_Page_Up:   return FDK_KEY_PAGEUP;
    case XK_Page_Down: return FDK_KEY_PAGEDOWN;
    case XK_F1:        return FDK_KEY_F1;
    case XK_F2:        return FDK_KEY_F2;
    case XK_F3:        return FDK_KEY_F3;
    case XK_F4:        return FDK_KEY_F4;
    case XK_F5:        return FDK_KEY_F5;
    case XK_F6:        return FDK_KEY_F6;
    case XK_F7:        return FDK_KEY_F7;
    case XK_F8:        return FDK_KEY_F8;
    case XK_F9:        return FDK_KEY_F9;
    case XK_F10:       return FDK_KEY_F10;
    case XK_F11:       return FDK_KEY_F11;
    case XK_F12:       return FDK_KEY_F12;
    default:
        if (ks >= 'a' && ks <= 'z') return (FDK_Key)ks;
        if (ks >= 'A' && ks <= 'Z') return (FDK_Key)(ks - 'A' + 'a');
        return FDK_KEY_UNKNOWN;
    }
}

static FDK_Modifier x11_mods(unsigned int state)
{
    FDK_Modifier m = FDK_MOD_NONE;
    if (state & ShiftMask)   m |= FDK_MOD_SHIFT;
    if (state & ControlMask) m |= FDK_MOD_CTRL;
    if (state & Mod1Mask)    m |= FDK_MOD_ALT;
    if (state & Mod4Mask)    m |= FDK_MOD_SUPER;
    return m;
}

/* ─── Event translation ──────────────────────────────────────────────────── */
/* We keep a simple linked list of FDK_Windows so we can look up by XWindow */
typedef struct WinNode { Window xw; FDK_Window *fdkw; struct WinNode *next; } WinNode;
static WinNode *s_wins = NULL;



static FDK_Window *find_fdkw(Window xw)
{
    for (WinNode *n = s_wins; n; n = n->next)
        if (n->xw == xw) return n->fdkw;
    return NULL;
}

/* Read the current _NET_WM_STATE property from the X server into pw,
 * returning true if maximized or fullscreen actually changed.
 * Called both on PropertyNotify and after the initial window map to
 * pick up any state the WM applied before we could observe it. */
static bool x11_read_wm_state(FDK_PlatformWindow *pw)
{
    Atom           actual_type;
    int            actual_fmt;
    unsigned long  nitems, bytes_left;
    unsigned char *data = NULL;

    bool was_max  = pw->maximized;
    bool was_full = pw->fullscreen;

    XGetWindowProperty(s_dpy, pw->xwin, s_atom_net_wm_state,
                       0, 1024, False, XA_ATOM,
                       &actual_type, &actual_fmt,
                       &nitems, &bytes_left, &data);

    bool now_max = false, now_full = false;
    if (data) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == s_atom_net_wm_state_max_v ||
                atoms[i] == s_atom_net_wm_state_max_h)  now_max  = true;
            if (atoms[i] == s_atom_net_wm_state_full)   now_full = true;
        }
        XFree(data);
    }

    pw->maximized  = now_max;
    pw->fullscreen = now_full;
    return (now_max != was_max || now_full != was_full);
}

static bool x11_poll_event(FDK_Event *out)
{
    if (!XPending(s_dpy)) return false;

    XEvent xe;
    XNextEvent(s_dpy, &xe);

    /* XFilterEvent: let the X Input Method (XIM) consume the event
     * first. If XFilterEvent returns True, the IME ate the event
     * (e.g., to update its preedit window); we should NOT translate
     * it to an FDK event. The IME will later commit text via a
     * KeyPress event with commit string data, which we handle below
     * in the KeyPress case via Xutf8LookupString.
     *
     * Without this filter, IME input (CJK via IBus/Fcitx/XIM) is
     * completely broken — every keystroke goes straight to the
     * widget as a literal key, bypassing the input method. */
    if (s_xim && XFilterEvent(&xe, None)) return false;

    memset(out, 0, sizeof *out);

    switch (xe.type) {
    case ClientMessage: {
        Atom msg = xe.xclient.message_type;
        FDK_Window *fw = find_fdkw(xe.xclient.window);

        /* WM_DELETE_WINDOW — window close request */
        if ((Atom)xe.xclient.data.l[0] == s_wm_delete) {
            out->type   = FDK_EVENT_CLOSE;
            out->window = fw;
            return true;
        }

        /* ── XDND (X Drag-and-Drop) handling ──
         * Only process if this window has a drop callback registered. */
        if (fw && fw->pw->drop_cb) {
            if (msg == s_atom_xdnd_enter) {
                /* Source is entering our window. data.l[0] = source window,
                 * data.l[1] bit 0 = "more than 3 types" (need to fetch
                 * XdndTypeList), data.l[1] bits 8+ = protocol version,
                 * data.l[2..4] = first 3 offered types (if ≤3). */
                fw->pw->dnd_source  = (Window)xe.xclient.data.l[0];
                fw->pw->dnd_version = (xe.xclient.data.l[1] >> 24) & 0xFF;
            }
            else if (msg == s_atom_xdnd_position) {
                /* Source is moving over our window. We must respond with
                 * XdndStatus to say whether we accept the drop.
                 * data.l[0] = source window
                 * data.l[1] bit 0 = "send me XdndPosition updates"
                 * data.l[2] = root window x,y (packed)
                 * data.l[3] bit 0 = "will you accept?" (we set this in reply)
                 * data.l[4] = action atom (XdndActionCopy etc.) */
                fw->pw->dnd_source = (Window)xe.xclient.data.l[0];

                /* Reply with XdndStatus: yes, we accept, whole window */
                XEvent reply = {0};
                reply.xclient.type         = ClientMessage;
                reply.xclient.window       = fw->pw->dnd_source;
                reply.xclient.message_type = s_atom_xdnd_status;
                reply.xclient.format       = 32;
                reply.xclient.data.l[0]    = (long)fw->pw->xwin;  /* our window */
                reply.xclient.data.l[1]    = 1;  /* bit 0 = accept */
                reply.xclient.data.l[2]    = 0;  /* rectangle: 0,0,0,0 = whole window */
                reply.xclient.data.l[3]    = 0;
                reply.xclient.data.l[4]    = xe.xclient.data.l[4]; /* echo the action */
                XSendEvent(s_dpy, fw->pw->dnd_source, False, NoEventMask, &reply);
                XFlush(s_dpy);
            }
            else if (msg == s_atom_xdnd_leave) {
                /* Source left without dropping */
                fw->pw->dnd_source = 0;
            }
            else if (msg == s_atom_xdnd_drop) {
                /* User released the mouse — request the data.
                 * data.l[0] = source window
                 * data.l[2] = timestamp */
                fw->pw->dnd_source = (Window)xe.xclient.data.l[0];
                Time timestamp = (Time)xe.xclient.data.l[2];

                /* Request text/uri-list from the source via the XDND
                 * selection. The data will arrive as a SelectionNotify
                 * event (handled below). */
                fw->pw->dnd_waiting_type = s_atom_uri_list;
                XConvertSelection(s_dpy, s_atom_xdnd_selection,
                                  s_atom_uri_list, s_atom_fdk_sel,
                                  fw->pw->xwin, timestamp);

                /* Reply with XdndFinished so the source knows we
                 * accepted. (Strictly, we should wait for the data to
                 * arrive before sending Finished, but most sources
                 * don't care about the timing.) */
                XEvent fin = {0};
                fin.xclient.type         = ClientMessage;
                fin.xclient.window       = fw->pw->dnd_source;
                fin.xclient.message_type = s_atom_xdnd_finished;
                fin.xclient.format       = 32;
                fin.xclient.data.l[0]    = (long)fw->pw->xwin;
                fin.xclient.data.l[1]    = 1;  /* bit 0 = success */
                fin.xclient.data.l[2]    = xe.xclient.data.l[4]; /* action */
                XSendEvent(s_dpy, fw->pw->dnd_source, False, NoEventMask, &fin);
                XFlush(s_dpy);
            }
        }
        break;
    }

    case PropertyNotify: {
        if (xe.xproperty.atom != s_atom_net_wm_state) break;
        FDK_Window *fw = find_fdkw(xe.xproperty.window);
        if (!fw) break;
        if (x11_read_wm_state(fw->pw)) {
            out->type              = FDK_EVENT_STATE_CHANGE;
            out->window            = fw;
            out->state.maximized   = fw->pw->maximized;
            out->state.fullscreen  = fw->pw->fullscreen;
            out->state.activated   = false; /* X11 doesn't use this path */
            return true;
        }
        break;
    }

    case ConfigureNotify: {
        FDK_Window *fw = find_fdkw(xe.xconfigure.window);
        if (fw) {
            int new_w = xe.xconfigure.width;
            int new_h = xe.xconfigure.height;
            /* ── Reallocate the software pixel buffer on resize ──
             * [BUGFIX v8] The previous code updated pw->w/h but never
             * reallocated pw->pixels or recreated pw->ximage. When the
             * WM maximized the window (480x320 → e.g. 1920x1080), the
             * widget layer would lay out and paint for the new larger
             * dimensions, but the renderer was still writing into a
             * buffer allocated for the original smaller size — a
             * classic heap buffer overflow that crashed in
             * fdk_ui_paint() the first time the maximize button was
             * clicked. This was a pre-existing bug in the X11 backend,
             * not introduced by the CSD work, but the CSD maximize
             * button is what exposed it.
             *
             * Fix: on every ConfigureNotify that changes the window
             * size, free the old pixel buffer + XImage and allocate
             * new ones at the new size. GL windows don't have a CPU
             * pixel buffer so they're skipped (GLX resizes its own
             * drawable automatically). */
            if (fw->pw->pixels &&
                (new_w != fw->pw->w || new_h != fw->pw->h)) {
                /* Use realloc instead of free+calloc so the old pixel
                 * data is preserved during the resize — this prevents
                 * the window from flashing black between the free and
                 * the next paint. The old content is still in the buffer
                 * (just at the wrong size) so the backing pixmap copy
                 * shows the old frame until the new paint completes. */
                if (fw->pw->ximage) {
                    fw->pw->ximage->data = NULL;
                    XDestroyImage(fw->pw->ximage);
                    fw->pw->ximage = NULL;
                }
                fw->pw->w         = new_w;
                fw->pw->h         = new_h;
                fw->pw->stride_px = new_w;
                fw->pw->pixels    = realloc(fw->pw->pixels,
                                            (size_t)new_w * new_h * 4);
                if (!fw->pw->pixels) {
                    break;
                }
                Visual *vis   = DefaultVisual(s_dpy, s_screen);
                int     depth = DefaultDepth(s_dpy, s_screen);
                fw->pw->ximage = XCreateImage(
                    s_dpy, vis, depth, ZPixmap, 0,
                    (char *)fw->pw->pixels, new_w, new_h, 32, new_w * 4);
                if (fw->pw->backing_pixmap) {
                    XFreePixmap(s_dpy, fw->pw->backing_pixmap);
                }
                fw->pw->backing_pixmap = XCreatePixmap(s_dpy, fw->pw->xwin,
                                                        new_w, new_h, depth);
            } else {
                fw->pw->w = new_w;
                fw->pw->h = new_h;
            }
            fw->w = new_w;
            fw->h = new_h;
            out->type        = FDK_EVENT_RESIZE;
            out->window      = fw;
            out->resize.w    = new_w;
            out->resize.h    = new_h;
            return true;
        }
        break;
    }

    case Expose:
        if (xe.xexpose.count == 0) {
            out->type   = FDK_EVENT_EXPOSE;
            out->window = find_fdkw(xe.xexpose.window);
            return true;
        }
        break;

    case SelectionNotify: {
        /* This fires for both clipboard pastes and XDND drops.
         * Check if it's our XDND selection (the property we set in
         * XConvertSelection above). */
        if (xe.xselection.property == s_atom_fdk_sel &&
            xe.xselection.selection == s_atom_xdnd_selection) {
            FDK_Window *fw = find_fdkw(xe.xselection.requestor);
            if (fw && fw->pw->drop_cb) {
                /* Read the URI list from the property */
                Atom actual_type;
                int actual_fmt;
                unsigned long nitems, bytes_left;
                unsigned char *data = NULL;
                XGetWindowProperty(s_dpy, fw->pw->xwin,
                                   s_atom_fdk_sel, 0, 65536,
                                   True, /* delete after reading */
                                   AnyPropertyType,
                                   &actual_type, &actual_fmt,
                                   &nitems, &bytes_left, &data);
                if (data && nitems > 0) {
                    /* Null-terminate and call the user's drop callback */
                    char *uri_list = malloc(nitems + 1);
                    if (uri_list) {
                        memcpy(uri_list, data, nitems);
                        uri_list[nitems] = '\0';
                        fw->pw->drop_cb(fw, uri_list, fw->pw->drop_ud);
                        free(uri_list);
                    }
                }
                if (data) XFree(data);
                fw->pw->dnd_waiting_type = 0;
            }
        }
        break;  /* Don't return an FDK_Event — drops are handled via callback */
    }

    case KeyPress:
    case KeyRelease: {
        KeySym ks = XkbKeycodeToKeysym(s_dpy, xe.xkey.keycode, 0, 0);
        out->type     = (xe.type == KeyPress) ? FDK_EVENT_KEY_DOWN
                                               : FDK_EVENT_KEY_UP;
        out->window   = find_fdkw(xe.xkey.window);
        out->key.key  = keysym_to_fdk(ks);
        out->key.mods = x11_mods(xe.xkey.state);

        /* On KeyPress, try the XIM input context first. If the IME
         * commits text (e.g., the user picked a candidate from the
         * candidate window), Xutf8LookupString returns the committed
         * UTF-8 string. We emit it as an FDK_EVENT_IME_COMMIT instead
         * of (or in addition to) the FDK_EVENT_KEY_DOWN, so widgets
         * can insert multi-character strings.
         *
         * For non-IME input (Latin layout, or no IME running),
         * Xutf8LookupString returns the same single-character text
         * that XLookupString would, and we set codepoint from the
         * first byte.
         *
         * If both IME commit AND a key event are produced (rare but
         * possible — e.g., IME returns XLookupBoth), we emit the
         * IME_COMMIT and let the KEY_DOWN pass through too. */
        if (xe.type == KeyPress && s_xic) {
            Status im_status = 0;
            int n = Xutf8LookupString(s_xic, &xe.xkey,
                                       s_ime_commit_buf,
                                       sizeof(s_ime_commit_buf) - 1,
                                       NULL, &im_status);
            if (n > 0 && (im_status == XLookupChars ||
                          im_status == XLookupBoth)) {
                s_ime_commit_buf[n] = '\0';
                /* Emit IME_COMMIT event. The widget layer (widget.c)
                 * handles this by inserting the string at the cursor
                 * of the focused TextInput/TextArea/SearchEntry.
                 *
                 * We still fall through to set codepoint from the
                 * first byte so existing KEY_DOWN handlers that
                 * don't yet handle IME_COMMIT can still see something. */
                /* For multi-byte commits, set codepoint to the first
                 * decoded UTF-8 char — but the IME_COMMIT event
                 * carries the full string. */
                /* (We can't return two events from one XEvent; the
                 * IME_COMMIT path is handled separately — the FDK
                 * event loop will see this as a KEY_DOWN with the
                 * first codepoint, and the platform layer queues
                 * the IME_COMMIT as the next event.) */
                /* For v0.2 simplicity: if the IME commits more than
                 * 1 byte OR a non-ASCII char, deliver it as IME_COMMIT
                 * by mutating this event in-place. The widget layer
                 * handles IME_COMMIT before KEY_DOWN. */
                if (n > 1 || (s_ime_commit_buf[0] & 0x80)) {
                    out->type = FDK_EVENT_IME_COMMIT;
                    out->ime_commit.text = s_ime_commit_buf;
                    return true;
                }
                /* Single ASCII char — fall through to set codepoint */
                out->key.codepoint = (uint32_t)(unsigned char)s_ime_commit_buf[0];
                return true;
            }
            /* XLookupKeySym or XLookupNone: fall through to plain
             * XLookupString below. */
        }

        /* decode UTF-8 codepoint (fallback when no XIC or IME didn't commit) */
        char buf[8] = {0};
        XLookupString(&xe.xkey, buf, sizeof buf, NULL, NULL);
        out->key.codepoint = (uint32_t)(unsigned char)buf[0];
        return true;
    }

    case ButtonPress:
    case ButtonRelease: {
        /* Mouse wheel: only generate scroll on ButtonPress, not
         * ButtonRelease. X sends both events for wheel scrolls,
         * and processing both would scroll then un-scroll (net
         * zero movement). */
        if (xe.type == ButtonPress &&
            (xe.xbutton.button == 4 || xe.xbutton.button == 5)) {
            /* FDK_Event uses a union — mouse.x/y and scroll.dx/dy share
             * the same memory. We can't set both. The widget layer uses
             * ui->mouse_x/mouse_y (from MOUSE_MOVE) for scroll hit-testing,
             * so we only set scroll.dx/dy here. */
            out->type      = FDK_EVENT_MOUSE_SCROLL;
            out->window    = find_fdkw(xe.xbutton.window);
            out->scroll.dx = 0;
            out->scroll.dy = (xe.xbutton.button == 4) ? 1.0f : -1.0f;
            return true;
        }
        /* Swallow the ButtonRelease for wheel buttons (don't generate
         * a MOUSE_UP event for them either) */
        if (xe.type == ButtonRelease &&
            (xe.xbutton.button == 4 || xe.xbutton.button == 5)) {
            return false;
        }
        out->type          = (xe.type == ButtonPress)
                                ? FDK_EVENT_MOUSE_DOWN : FDK_EVENT_MOUSE_UP;
        out->window        = find_fdkw(xe.xbutton.window);
        out->mouse.x       = xe.xbutton.x;
        out->mouse.y       = xe.xbutton.y;
        out->mouse.button  = (FDK_MouseButton)xe.xbutton.button;
        out->mouse.mods    = x11_mods(xe.xbutton.state);
        return true;
    }

    case MotionNotify:
        out->type        = FDK_EVENT_MOUSE_MOVE;
        out->window      = find_fdkw(xe.xmotion.window);
        out->motion.x    = xe.xmotion.x;
        out->motion.y    = xe.xmotion.y;
        out->motion.mods = x11_mods(xe.xmotion.state);
        return true;

    default:
        break;
    }

    out->type = FDK_EVENT_NONE;
    return false;
}

static void x11_wait_event(FDK_Event *out)
{
    /* Block until we successfully translate an event.
     * XNextEvent blocks when the queue is empty — no busy-wait. */
    do {
        XEvent xe;
        XNextEvent(s_dpy, &xe);
        /* Push it back and let poll_event handle the translation,
         * so all event logic stays in one place. */
        XPutBackEvent(s_dpy, &xe);
    } while (!x11_poll_event(out));
}

/* ─── Cursor ────────────────────────────────────────────────────────────────── */
static void x11_load_cursors(void)
{
    if (s_cursors_loaded) return;

    /* Classic X11 font-cursor fallbacks (XC_* glyph indices in the
     * cursor font). Used directly when libxcursor is unavailable, and
     * as a per-cursor fallback when XcursorLibraryLoadCursor() returns
     * None (no theme icon for that name on this system). */
    static const unsigned int font_fallbacks[10] = {
        68,   /* FDK_CURSOR_DEFAULT     — left-ptr */
        60,   /* FDK_CURSOR_POINTER     — hand2 */
        152,  /* FDK_CURSOR_TEXT        — xterm */
        34,   /* FDK_CURSOR_CROSSHAIR   — crosshair */
        52,   /* FDK_CURSOR_MOVE        — fleur */
        108,  /* FDK_CURSOR_RESIZE_H    — sb_h_double_arrow */
        116,  /* FDK_CURSOR_RESIZE_V    — sb_v_double_arrow */
        134,  /* FDK_CURSOR_RESIZE_TL   — top_left_arrow (↖↘) */
        136,  /* FDK_CURSOR_RESIZE_TR   — top_right_arrow (↗↙) */
        0,    /* FDK_CURSOR_NOT_ALLOWED — X cursor (glyph 0) */
    };

#ifdef FDK_HAVE_XCURSOR
    /* Map FDK_Cursor values to Xcursor theme names */
    const char *names[] = {
        "default",      /* FDK_CURSOR_DEFAULT     */
        "pointer",      /* FDK_CURSOR_POINTER     */
        "text",         /* FDK_CURSOR_TEXT        */
        "crosshair",    /* FDK_CURSOR_CROSSHAIR   */
        "move",         /* FDK_CURSOR_MOVE        */
        "ew-resize",    /* FDK_CURSOR_RESIZE_H    */
        "ns-resize",    /* FDK_CURSOR_RESIZE_V    */
        "nwse-resize",  /* FDK_CURSOR_RESIZE_TL   ↖↘ */
        "nesw-resize",  /* FDK_CURSOR_RESIZE_TR   ↗↙ */
        "not-allowed",  /* FDK_CURSOR_NOT_ALLOWED */
    };

    for (int i = 0; i < 10; i++) {
        s_cursors[i] = XcursorLibraryLoadCursor(s_dpy, names[i]);
        if (s_cursors[i] == None)
            s_cursors[i] = XCreateFontCursor(s_dpy, font_fallbacks[i]);
    }
#else
    for (int i = 0; i < 10; i++)
        s_cursors[i] = XCreateFontCursor(s_dpy, font_fallbacks[i]);
#endif
    s_cursors_loaded = true;
}

static void x11_set_cursor(FDK_Cursor cursor)
{
    if (!s_dpy) return;
    x11_load_cursors();

    int idx = (int)cursor;
    if (idx < 0 || idx >= 10) idx = 0;

    /* Apply cursor to all registered windows */
    for (WinNode *n = s_wins; n; n = n->next)
        XDefineCursor(s_dpy, n->xw, s_cursors[idx]);
    XFlush(s_dpy);
}

static void x11_get_root_mouse(int *root_x, int *root_y)
{
    if (!s_dpy || !s_wins) { *root_x = 0; *root_y = 0; return; }
    Window root_ret, child_ret;
    int win_x, win_y;
    unsigned int mask;
    XQueryPointer(s_dpy, s_wins->xw, &root_ret, &child_ret,
                  root_x, root_y, &win_x, &win_y, &mask);
}

/* ─── Window register ────────────────────────────────────────────────────────── */
static void x11_window_register(FDK_PlatformWindow *pw, FDK_Window *fdkw)
{
    WinNode *n = malloc(sizeof *n);
    n->xw = pw->xwin; n->fdkw = fdkw; n->next = s_wins; s_wins = n;
}

/* ─── Drag and Drop ─────────────────────────────────────────────────────────── */
static void x11_window_set_drop_handler(FDK_PlatformWindow *pw,
                                         FDK_DropCb cb, void *ud)
{
    if (!pw) return;
    pw->drop_cb = cb;
    pw->drop_ud = ud;
}

/* ─── Clipboard ──────────────────────────────────────────────────────────────── */

/* Set our text as the CLIPBOARD owner */
static void x11_clipboard_set(const char *text)
{
    if (!text || !s_dpy) return;
    free(s_clipboard);
    s_clipboard = strdup(text);

    /* We need a window to own the selection — use the first registered one */
    Window owner = s_wins ? s_wins->xw : None;
    if (owner == None) return;

    XSetSelectionOwner(s_dpy, s_atom_clipboard, owner, CurrentTime);
    if (XGetSelectionOwner(s_dpy, s_atom_clipboard) == owner)
        s_clip_owner = owner;
    XFlush(s_dpy);
}

/* Handle SelectionRequest — another app wants our clipboard data */
static void x11_handle_selection_request(XSelectionRequestEvent *req)
{
    XSelectionEvent reply = {0};
    reply.type      = SelectionNotify;
    reply.display   = s_dpy;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target    = req->target;
    reply.property  = None;   /* default: refuse */
    reply.time      = req->time;

    if (req->target == s_atom_targets) {
        /* Report which formats we support */
        Atom supported[] = { s_atom_targets, s_atom_utf8, s_atom_string };
        XChangeProperty(s_dpy, req->requestor, req->property,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)supported, 3);
        reply.property = req->property;
    } else if ((req->target == s_atom_utf8 ||
                req->target == s_atom_string ||
                req->target == XA_STRING) && s_clipboard) {
        XChangeProperty(s_dpy, req->requestor, req->property,
                        req->target, 8, PropModeReplace,
                        (unsigned char*)s_clipboard,
                        (int)strlen(s_clipboard));
        reply.property = req->property;
    }

    XSendEvent(s_dpy, req->requestor, False, 0, (XEvent*)&reply);
    XFlush(s_dpy);
}

/* Request clipboard text from the current CLIPBOARD owner */
static char *x11_clipboard_get(void)
{
    if (!s_dpy) return NULL;

    /* If we own the clipboard just return our copy */
    Window owner = XGetSelectionOwner(s_dpy, s_atom_clipboard);
    if (owner == None) return NULL;
    if (owner == s_clip_owner && s_clipboard)
        return strdup(s_clipboard);

    /* Need a requestor window */
    Window req_win = s_wins ? s_wins->xw : None;
    if (req_win == None) return NULL;

    /* Ask the owner to convert to UTF8_STRING and put it in our property */
    XConvertSelection(s_dpy, s_atom_clipboard, s_atom_utf8,
                      s_atom_fdk_sel, req_win, CurrentTime);
    XFlush(s_dpy);

    /* Wait for SelectionNotify with a timeout (~200ms) */
    XEvent ev;
    for (int i = 0; i < 200; i++) {
        if (XCheckTypedWindowEvent(s_dpy, req_win,
                                   SelectionNotify, &ev)) {
            if (ev.xselection.property == None) return NULL;

            /* Read the property */
            Atom           actual_type;
            int            actual_fmt;
            unsigned long  nitems, bytes_left;
            unsigned char *data = NULL;

            XGetWindowProperty(s_dpy, req_win, s_atom_fdk_sel,
                               0, 65536, True,
                               AnyPropertyType,
                               &actual_type, &actual_fmt,
                               &nitems, &bytes_left, &data);
            if (data && nitems > 0) {
                char *result = strdup((char*)data);
                XFree(data);
                return result;
            }
            if (data) XFree(data);
            return NULL;
        }
        /* Process any pending events while waiting */
        while (XPending(s_dpy)) {
            XEvent pending;
            XNextEvent(s_dpy, &pending);
            if (pending.type == SelectionRequest)
                x11_handle_selection_request(&pending.xselectionrequest);
            else
                XPutBackEvent(s_dpy, &pending);
        }
        struct timespec ts = {0, 1000000}; /* 1ms */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static bool x11_wait_event_timeout(FDK_Event *out, int timeout_ms)
{
    /* Check already-queued events first */
    if (x11_poll_event(out)) return true;
    if (timeout_ms == 0) return false;

    int fd = ConnectionNumber(s_dpy);
    struct pollfd pfd = { fd, POLLIN, 0 };
    int ret = poll(&pfd, 1, timeout_ms);

    if (ret > 0)
        return x11_poll_event(out);
    if (ret == 0)
        return false; /* timeout */
    if (errno == EINTR)
        return false;
    return false;
}

/* ─── VTable ─────────────────────────────────────────────────────────────── */
const FDK_PlatformVTable fdk_platform_x11 = {
    .name                  = "X11",
    .init                  = x11_init,
    .shutdown              = x11_shutdown,
    .window_create         = x11_window_create,
    .window_destroy        = x11_window_destroy,
    .window_show           = x11_window_show,
    .window_hide           = x11_window_hide,
    .window_set_title      = x11_window_set_title,
    .window_get_size       = x11_window_get_size,
    .window_get_scale      = x11_window_get_scale,
    .window_request_redraw = x11_window_request_redraw,
    .window_set_maximized  = x11_window_set_maximized,
    .window_set_fullscreen = x11_window_set_fullscreen,
    .window_is_maximized   = x11_window_is_maximized,
    .window_is_fullscreen  = x11_window_is_fullscreen,
    .window_minimize       = x11_window_minimize,
    .window_begin_move     = x11_window_begin_move,
    .window_begin_resize   = x11_window_begin_resize,
    .window_resize         = x11_window_resize,
    .window_move_resize    = x11_window_move_resize,
    .window_set_decorated  = x11_window_set_decorated,
    .window_set_drop_handler = x11_window_set_drop_handler,
    .window_set_icon_name  = x11_window_set_icon_name,
    .window_set_icon_file  = x11_window_set_icon_file,
    .window_get_framebuffer= x11_get_framebuffer,
    .window_present        = x11_present,
    .gl_make_current       = x11_gl_make_current,
    .gl_swap_buffers       = x11_gl_swap_buffers,
    .poll_event            = x11_poll_event,
    .wait_event            = x11_wait_event,
    .wait_event_timeout    = x11_wait_event_timeout,
    .window_register       = x11_window_register,
    .set_cursor            = x11_set_cursor,
    .get_root_mouse        = x11_get_root_mouse,
    .clipboard_set         = x11_clipboard_set,
    .clipboard_get         = x11_clipboard_get,
};

#endif /* FDK_HAVE_X11 */
