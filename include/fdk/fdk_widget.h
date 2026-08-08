/*
 * fdk_widget.h — FDK Widget Layer
 *
 * Immediate-mode drawing with a retained widget tree for hit-testing,
 * focus, and layout. Apps build a tree of FDK_Widget nodes, call
 * fdk_ui_run() and respond to callbacks.
 *
 * Widget lifecycle:
 *   FDK_Widget *btn = fdk_button("OK");
 *   fdk_button_on_click(btn, my_click_cb, userdata);
 *   fdk_container_add(root, btn);
 *   fdk_ui_run(win, root);   // blocks until window closes
 */
#ifndef FDK_WIDGET_H
#define FDK_WIDGET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "fdk.h"

/* ─── Forward declarations ───────────────────────────────────────────────── */
typedef struct FDK_Widget    FDK_Widget;
typedef struct FDK_UI        FDK_UI;

/* ─── Callback types ─────────────────────────────────────────────────────── */
typedef void (*FDK_ClickCb)(FDK_Widget *w, void *userdata);
typedef void (*FDK_ChangeCb)(FDK_Widget *w, const char *text, void *userdata);
typedef void (*FDK_DrawCb)(FDK_Widget *w, FDK_Rect bounds, void *userdata);
typedef void (*FDK_ValueCb)(FDK_Widget *w, float value, void *userdata);
typedef void (*FDK_SelectCb)(FDK_Widget *w, int index, const char *text, void *userdata);

/* ─── Widget types ───────────────────────────────────────────────────────── */
typedef enum {
    FDK_WIDGET_CONTAINER = 0,
    FDK_WIDGET_LABEL,
    FDK_WIDGET_BUTTON,
    FDK_WIDGET_TEXT_INPUT,
    FDK_WIDGET_CHECKBOX,
    FDK_WIDGET_SEPARATOR,
    FDK_WIDGET_CUSTOM,
    FDK_WIDGET_SLIDER,
    FDK_WIDGET_PROGRESS_BAR,
    FDK_WIDGET_SCROLL_VIEW,
    FDK_WIDGET_DROPDOWN,
    FDK_WIDGET_IMAGE,
    FDK_WIDGET_TOGGLE_BUTTON,   /* stateful on/off button              */
    FDK_WIDGET_RADIO_BUTTON,    /* exclusive selection within a group  */
    FDK_WIDGET_SPINNER,         /* numeric up/down input               */
    FDK_WIDGET_BADGE,           /* small count/status pill             */
    FDK_WIDGET_TABS,            /* horizontal tab bar + pane switcher  */
    FDK_WIDGET_MENUBAR,         /* top-of-window menu bar              */
    FDK_WIDGET_TEXTAREA,        /* multiline scrollable text input     */
    FDK_WIDGET_TITLEBAR,        /* client-side decoration title bar    */
    FDK_WIDGET_SWITCH,          /* slide toggle (different visual from toggle_button) */
    FDK_WIDGET_LEVEL_BAR,       /* segmented discrete progress          */
    FDK_WIDGET_SEARCH_ENTRY,    /* text input with search icon + clear button */
    FDK_WIDGET_EXPANDER,        /* collapsible section with label        */
    FDK_WIDGET_STATUS_BAR,      /* bottom bar with text slots            */
    FDK_WIDGET_GRID,            /* row/col grid layout with spans        */
    FDK_WIDGET_POPOVER,         /* transient anchored popup              */
    FDK_WIDGET_CALENDAR,        /* month-view date picker                */
} FDK_WidgetType;

/* ─── Layout ─────────────────────────────────────────────────────────────── */
typedef enum {
    FDK_LAYOUT_VERTICAL = 0,   /* children stacked top-to-bottom */
    FDK_LAYOUT_HORIZONTAL,     /* children side by side           */
} FDK_LayoutDir;

typedef enum {
    FDK_ALIGN_START = 0,
    FDK_ALIGN_CENTER,
    FDK_ALIGN_END,
    FDK_ALIGN_STRETCH,
} FDK_Align;

typedef struct {
    FDK_LayoutDir dir;
    FDK_Align     align;        /* cross-axis alignment            */
    int           gap;          /* px between children             */
    int           padding;      /* inner padding on all sides      */
} FDK_Layout;

/* ─── Size hints ─────────────────────────────────────────────────────────── */
#define FDK_SIZE_WRAP    (-1)   /* shrink to content               */
#define FDK_SIZE_FILL    (-2)   /* expand to fill available space  */

/* ─── Theme tokens ───────────────────────────────────────────────────────── */

/* Per-widget style block — zero-valued fields mean "inherit from theme" */
typedef struct {
    FDK_Color    bg;
    FDK_Color    bg_hover;
    FDK_Color    bg_active;
    FDK_Color    fg;
    FDK_Color    border;
    FDK_Color    border_focus;
    FDK_Gradient gradient;
    FDK_Shadow   shadow;
    int          radius;      /* -1 = inherit */
    int          pad_v;       /* -1 = inherit */
    int          pad_h;       /* -1 = inherit */
    bool         has_bg, has_bg_hover, has_bg_active;
    bool         has_fg, has_border, has_border_focus;
    bool         has_gradient, has_shadow;
    bool         has_radius, has_pad;
} FDK_WidgetStyle;

#define FDK_VARIANTS_MAX 8

typedef struct {
    /* ── Global surface colours ── */
    FDK_Color bg_window;
    FDK_Color bg_widget;
    FDK_Color bg_widget_hover;
    FDK_Color bg_widget_active;
    FDK_Color bg_input;
    FDK_Color bg_input_focus;
    FDK_Color bg_selection;     /* text selection highlight */

    /* ── Foreground ── */
    FDK_Color fg_primary;
    FDK_Color fg_secondary;
    FDK_Color fg_disabled;
    FDK_Color fg_on_accent;

    /* ── Accent ── */
    FDK_Color accent;
    FDK_Color accent_hover;
    FDK_Color accent_active;

    /* ── Structural ── */
    FDK_Color border;
    FDK_Color border_focus;
    FDK_Color separator;
    FDK_Color scrollbar_track;
    FDK_Color scrollbar_thumb;

    /* ── Toast accent strips ── */
    FDK_Color toast_info;
    FDK_Color toast_success;
    FDK_Color toast_warning;
    FDK_Color toast_error;

    /* ── Geometry ── */
    int radius_sm;      /* inputs, buttons       */
    int radius_md;      /* cards, containers     */
    int scrollbar_w;    /* scrollbar width px    */

    /* ── Spacing ── */
    int gap_sm;         /* tight gaps            */
    int gap_md;         /* default widget gap    */
    int pad_sm;         /* tight padding         */
    int pad_md;         /* default padding       */

    /* ── Typography ── */
    FDK_Font *font_body;
    FDK_Font *font_label;
    FDK_Font *font_mono;    /* for textarea, code labels */
    int       line_height;

    /* ── Per-widget style overrides ──
     * Indexed by FDK_WidgetType; paint reads these before falling back
     * to global tokens. Populated by fdk_theme_load(). */
    FDK_WidgetStyle widget_styles[24];

    /* ── Named variants (e.g. [button.danger]) ──
     * fdk_widget_set_variant(w, "danger") makes paint look up
     * widget_variants[widget_type]["danger"]. */
    struct {
        char          name[48];  /* "TYPEIDX:variant_name\0" — worst case
                                   * %d of INT_MIN (11) + ':' + 31 + NUL = 44 */
        FDK_WidgetStyle style;
    } variants[FDK_VARIANTS_MAX];
    int variant_count;

    /* ── Window shadow ── */
    FDK_Shadow window_shadow;
} FDK_Theme;

/* ─── Built-in themes ────────────────────────────────────────────────────── */
FDK_Theme fdk_theme_faded_dream(void);

/* ─── .fdktheme file loader ──────────────────────────────────────────────── */
/* Load a .fdktheme file into *out.  Variables, per-widget sections,
 * gradients and shadows are all parsed.  Fonts are loaded if paths
 * are given.  Returns false only if the file cannot be opened. */
bool fdk_theme_load(FDK_Theme *out, const char *path);

/* ─── System-wide theme (GTK-like default) ──────────────────────────────────
 *
 * FDK apps that pass NULL to fdk_ui_create() automatically get the
 * system-wide theme: every FDK app on the machine looks the same unless
 * a developer opts out (see fdk_theme_force below) or a user opts an
 * individual app into a developer-bundled alternative (see
 * fdk_theme_register / the user override file, below).
 *
 * Location: ~/.FDKthemes/theme.fdktheme  (NOT ~/.themes — FDK does not
 * use the legacy GTK/icon-theme convention).
 * $FDK_THEME_FILE overrides this path entirely, for testing or for
 * users who want their system-wide file to live somewhere else. */
bool fdk_theme_load_system(FDK_Theme *base_theme);

/* ─── Per-app developer-provided alternatives (tier 2) ──────────────────────
 *
 * A developer can bundle one or more named themes with their app without
 * forcing anything: the app still defaults to the system-wide theme, but
 * the *user* can choose one of the developer's alternatives for that one
 * app via a small FDK-managed override file:
 *
 *   ~/.FDKthemes/overrides/<app_name>
 *
 * containing a single line: the *path* to the .fdktheme file the user
 * wants that app to use. The developer doesn't write this file or any
 * switcher UI — fdk_ui_create() checks for it automatically whenever
 * app_name is set in FDK_InitInfo.
 *
 * fdk_theme_register() is optional and purely informational — it lets a
 * developer tell FDK what their bundled themes are called and where they
 * live, so a future settings UI (or fdk_theme_list_registered(), below)
 * can show the user a menu instead of requiring them to know/hand-type a
 * path into the override file themselves. Registering does NOT change
 * resolution behaviour: the override file's path always wins regardless
 * of whether it matches something registered. */
#define FDK_THEME_REGISTRY_MAX 16

void fdk_theme_register(const char *app_name, const char *theme_name,
                         const char *path);

/* Returns the number of themes registered for app_name, filling
 * out_names/out_paths (each arrays of max_count char* slots, caller-owned
 * storage, FDK copies into pointers the caller already allocated is NOT
 * required — FDK returns pointers to its own internal registry storage,
 * valid for the lifetime of the program). */
int fdk_theme_list_registered(const char *app_name,
                               const char *out_names[],
                               const char *out_paths[],
                               int max_count);

/* ─── Hard developer force (tier 1) ──────────────────────────────────────────
 *
 * Call before fdk_ui_create() to bypass system-wide AND user-override
 * resolution entirely for this process. No fallback exists once this is
 * called — the developer is opting their app out of the shared-theme
 * ecosystem completely, by design, with no obligation to provide a
 * system-wide-compatible alternative. */
void fdk_theme_force(const FDK_Theme *theme);
void fdk_theme_force_file(const char *path);

/* Internal resolver — called by fdk_ui_create() when theme == NULL.
 * Exposed publicly so apps can call it themselves if they create the UI
 * theme separately from fdk_ui_create() for some reason. Implements the
 * three-tier priority described above. app_name may be NULL (then tier 2
 * is skipped and only tiers 1 and 3 apply). */
FDK_Theme fdk_theme_resolve(const char *app_name);

/* Same resolution as fdk_theme_resolve(), but also reports which file
 * backed the result via out_path (pass a buffer of at least 512 bytes;
 * out_size is the buffer's capacity). out_path is set to an empty
 * string for tier-1 (fdk_theme_force/fdk_theme_force_file) results,
 * since a forced theme has no backing file to report.
 * Useful for apps that want to display "currently using: <path>" in a
 * settings UI, or that want to call fdk_theme_watch() themselves on
 * exactly the file that was actually used (e.g. when they need to pass
 * a non-NULL theme to fdk_ui_create() for some other reason, which
 * suppresses fdk_ui_create()'s own automatic watch). */
FDK_Theme fdk_theme_resolve_ex(const char *app_name, char *out_path, size_t out_size);

/* Runtime switch — marks UI dirty, next fdk_ui_step relayouts + repaints */
void fdk_ui_set_theme(FDK_UI *ui, FDK_Widget *root, const FDK_Theme *theme);

/* Hot reload — watches path with inotify, calls fdk_ui_set_theme on change.
 * Only one watch per UI at a time.  Pass NULL to stop watching.
 *
 * fdk_ui_create() automatically starts watching the resolved theme file
 * for apps that end up on tier 2 or tier 3 (system-wide or a registered
 * user-selected alternative), so system-wide theme changes propagate
 * live without the app doing anything. Apps on tier 1 (fdk_theme_force)
 * are not auto-watched, since they've opted out of dynamic theme changes
 * by design — call fdk_theme_watch() yourself if you still want hot
 * reload on your forced theme file. */
void fdk_theme_watch(FDK_UI *ui, FDK_Widget *root, const char *path);

/* Watch ~/.config/FDK/fdk.conf for changes. When fdk-theme set <name>
 * rewrites the conf, this fires and re-runs the full three-tier
 * resolution automatically — so the app re-themes live whenever the
 * user runs fdk-theme set, with no restart needed.
 * Pass the same app_name you used in FDK_InitInfo, or NULL to only
 * respond to system-wide (tier-3) changes. */
void fdk_theme_watch_conf(FDK_UI *ui, FDK_Widget *root, const char *app_name);

/* Per-widget variant — theme can define [button.danger] etc. */
void fdk_widget_set_variant(FDK_Widget *w, const char *variant);

/* Per-widget style override (takes priority over theme file) */
void fdk_widget_set_style(FDK_Widget *w, const FDK_WidgetStyle *style);

/* Attach a context menu that auto-shows on right-click */
void fdk_widget_set_context_menu(FDK_Widget *w, FDK_ContextMenu *cm);

/* ─── Text area (multiline) ──────────────────────────────────────────────── */
#define FDK_TEXTAREA_MAX  65536  /* max bytes */

FDK_Widget *fdk_textarea(const char *initial_text);
const char *fdk_textarea_get_text(const FDK_Widget *w);
void        fdk_textarea_set_text(FDK_Widget *w, const char *text);
void        fdk_textarea_set_readonly(FDK_Widget *w, bool ro);
void        fdk_textarea_on_change(FDK_Widget *w,
                                    void (*cb)(FDK_Widget *, void *), void *ud);

/* ─── Title bar (client-side decoration) ────────────────────────────────────
 * Client-side decoration title bar. Renders a title and minimize/maximize/
 * close button glyphs at a window's top edge, AND wires those buttons to
 * the window-state API:
 *   - close     → fires FDK_EVENT_CLOSE (same path as a WM_DELETE_WINDOW)
 *   - maximize  → toggles fdk_window_set_maximized()
 *   - minimize  → calls fdk_window_minimize()
 * Clicking and dragging the empty titlebar area (not on a button) begins
 * an interactive window move via fdk_window_begin_move(), driven by the
 * compositor/WM.
 *
 * To actually suppress the compositor's own decorations on a window
 * using this widget, also set FDK_WindowDesc.csd = true when creating
 * the window — that asks the platform backend to negotiate
 * no-server-side-decorations (Motif hints on X11, xdg-decoration on
 * Wayland). Without csd=true the titlebar widget still works, but you
 * may end up with both the compositor's titlebar and this one visible
 * at the top of the window. */
FDK_Widget *fdk_titlebar(const char *title);
void        fdk_titlebar_set_title(FDK_Widget *w, const char *title);
/* Which buttons to draw — all default to true. A dialog might want only
 * show_close, for example. */
void        fdk_titlebar_set_buttons(FDK_Widget *w, bool show_minimize,
                                      bool show_maximize, bool show_close);
/* Set the double-click interval in milliseconds (default: 400).
 * Two clicks within this interval on the empty titlebar area
 * toggle maximized state. Set to 0 to disable double-click. */
void        fdk_titlebar_set_dblclick_ms(FDK_Widget *w, int ms);

/* ─── Context menu ────────────────────────────────────────────────────────── */
/* A context menu is a transient popup, not a widget in the tree.
 * Call fdk_context_menu_show() from a right-click handler; the menu is
 * rendered as an overlay and dismissed on any click outside it. */
#define FDK_CTX_MAX_ITEMS 20

/* FDK_ContextMenu is forward-declared in fdk.h */

FDK_ContextMenu *fdk_context_menu_new(void);
void             fdk_context_menu_free(FDK_ContextMenu *cm);
void             fdk_context_menu_add(FDK_ContextMenu *cm,
                                       const char *label,
                                       void (*cb)(void *ud), void *ud);
void             fdk_context_menu_add_separator(FDK_ContextMenu *cm);

/* Show at (x, y) in window coords. Ownership stays with the caller. */
void             fdk_context_menu_show(FDK_UI *ui,
                                        FDK_ContextMenu *cm, int x, int y);
void             fdk_context_menu_hide(FDK_UI *ui);

/* ─── UI context ─────────────────────────────────────────────────────────── */
/* One UI context per window */
FDK_UI    *fdk_ui_create(FDK_Window *win, FDK_Theme *theme);
void       fdk_ui_destroy(FDK_UI *ui);

/* Set root widget and enter the event loop (blocks until window closes) */
void       fdk_ui_run(FDK_UI *ui, FDK_Widget *root);

/* Step variant — call inside your own loop */
bool       fdk_ui_step(FDK_UI *ui, FDK_Widget *root, FDK_Event *ev);
void       fdk_ui_paint(FDK_UI *ui, FDK_Widget *root);

/* Reset cursor blink — call after focus changes or events */
void       fdk_ui_reset_blink(FDK_UI *ui);

/* Force a layout pass — call after window is shown but before first paint */
void       fdk_ui_layout(FDK_UI *ui, FDK_Widget *root);
/* Mark the UI dirty — forces a repaint on next step */
void       fdk_ui_invalidate(FDK_UI *ui);

/* ─── Widget base ─────────────────────────────────────────────────────────── */
/* Computed layout rect — set after fdk_ui_layout() */
FDK_Rect   fdk_widget_get_rect(const FDK_Widget *w);

/* Size hints */
void       fdk_widget_set_size(FDK_Widget *w, int w_hint, int h_hint);
void       fdk_widget_set_min_size(FDK_Widget *w, int min_w, int min_h);
void       fdk_widget_set_max_size(FDK_Widget *w, int max_w, int max_h); /* 0 = no limit */

/* Visibility */
void       fdk_widget_show(FDK_Widget *w);
void       fdk_widget_hide(FDK_Widget *w);
bool       fdk_widget_is_visible(const FDK_Widget *w);

/* Enable / disable */
void       fdk_widget_enable(FDK_Widget *w);
void       fdk_widget_disable(FDK_Widget *w);
bool       fdk_widget_is_enabled(const FDK_Widget *w);

/* User data */
void       fdk_widget_set_userdata(FDK_Widget *w, void *data);
void      *fdk_widget_get_userdata(const FDK_Widget *w);

/* Destroy a widget and all its children */
void       fdk_widget_destroy(FDK_Widget *w);

/* ─── Container ──────────────────────────────────────────────────────────── */
FDK_Widget *fdk_container(FDK_Layout layout);
void        fdk_container_add(FDK_Widget *container, FDK_Widget *child);
void        fdk_container_remove(FDK_Widget *container, FDK_Widget *child);
void        fdk_container_clear(FDK_Widget *container);

/* Convenience constructors */
FDK_Widget *fdk_vbox(int gap, int padding);   /* vertical container   */
FDK_Widget *fdk_hbox(int gap, int padding);   /* horizontal container */

/* ─── Label ──────────────────────────────────────────────────────────────── */
FDK_Widget *fdk_label(const char *text);
void        fdk_label_set_text(FDK_Widget *w, const char *text);
const char *fdk_label_get_text(const FDK_Widget *w);
void        fdk_label_set_color(FDK_Widget *w, FDK_Color color);

/* ─── Button ─────────────────────────────────────────────────────────────── */
FDK_Widget *fdk_button(const char *label);
void        fdk_button_set_label(FDK_Widget *w, const char *label);
void        fdk_button_on_click(FDK_Widget *w, FDK_ClickCb cb, void *userdata);

/* ─── Text input ─────────────────────────────────────────────────────────── */
FDK_Widget *fdk_text_input(const char *placeholder);
void        fdk_text_input_set_text(FDK_Widget *w, const char *text);
const char *fdk_text_input_get_text(const FDK_Widget *w);
void        fdk_text_input_set_placeholder(FDK_Widget *w, const char *text);
void        fdk_text_input_on_change(FDK_Widget *w, FDK_ChangeCb cb, void *ud);
void        fdk_text_input_set_max_len(FDK_Widget *w, int max);

/* ─── Checkbox ───────────────────────────────────────────────────────────── */
FDK_Widget *fdk_checkbox(const char *label, bool checked);
bool        fdk_checkbox_get_checked(const FDK_Widget *w);
void        fdk_checkbox_set_checked(FDK_Widget *w, bool checked);
void        fdk_checkbox_on_change(FDK_Widget *w, FDK_ClickCb cb, void *ud);

/* ─── Separator ──────────────────────────────────────────────────────────── */
FDK_Widget *fdk_separator(void);

/* ─── Custom widget ──────────────────────────────────────────────────────── */
FDK_Widget *fdk_custom(int w_hint, int h_hint,
                        FDK_DrawCb draw_cb, void *userdata);

/* ─── Slider ─────────────────────────────────────────────────────────────── */
FDK_Widget *fdk_slider(float min, float max, float value);
float       fdk_slider_get_value(const FDK_Widget *w);
void        fdk_slider_set_value(FDK_Widget *w, float value);
void        fdk_slider_set_range(FDK_Widget *w, float min, float max);
void        fdk_slider_set_step(FDK_Widget *w, float step);  /* 0 = continuous */
void        fdk_slider_on_change(FDK_Widget *w, FDK_ValueCb cb, void *ud);

/* ─── Progress bar ───────────────────────────────────────────────────────── */
FDK_Widget *fdk_progress_bar(float value);              /* value: 0.0 – 1.0   */
void        fdk_progress_set_value(FDK_Widget *w, float value);
float       fdk_progress_get_value(const FDK_Widget *w);
void        fdk_progress_set_indeterminate(FDK_Widget *w, bool on);

/* ─── Scroll view ────────────────────────────────────────────────────────── */
FDK_Widget *fdk_scroll_view(FDK_Widget *content);       /* wraps any widget   */
void        fdk_scroll_view_set_content(FDK_Widget *w, FDK_Widget *content);
void        fdk_scroll_view_scroll_to(FDK_Widget *w, int x, int y);

/* ─── Dropdown ───────────────────────────────────────────────────────────── */
FDK_Widget *fdk_dropdown(const char *placeholder);
void        fdk_dropdown_add_item(FDK_Widget *w, const char *text);
void        fdk_dropdown_clear(FDK_Widget *w);
int         fdk_dropdown_get_selected(const FDK_Widget *w); /* -1 = none     */
const char *fdk_dropdown_get_selected_text(const FDK_Widget *w);
void        fdk_dropdown_set_selected(FDK_Widget *w, int index);
void        fdk_dropdown_on_change(FDK_Widget *w, FDK_SelectCb cb, void *ud);

/* ─── Widget tag / ID ────────────────────────────────────────────────────── */
/* Attach an integer tag for lookup; 0 = unset */
void        fdk_widget_set_tag(FDK_Widget *w, int tag);
int         fdk_widget_get_tag(const FDK_Widget *w);
/* Walk the tree and return the first widget whose tag == tag, or NULL */
FDK_Widget *fdk_widget_find(FDK_Widget *root, int tag);

/* ─── Tooltip ─────────────────────────────────────────────────────────────── */
/* Attach a tooltip string to any widget — shown after FDK_TOOLTIP_DELAY_MS */
void        fdk_widget_set_tooltip(FDK_Widget *w, const char *text);
const char *fdk_widget_get_tooltip(const FDK_Widget *w);

/* ─── Toggle button ──────────────────────────────────────────────────────── */
FDK_Widget *fdk_toggle_button(const char *label, bool active);
bool        fdk_toggle_button_get_active(const FDK_Widget *w);
void        fdk_toggle_button_set_active(FDK_Widget *w, bool active);
void        fdk_toggle_button_on_change(FDK_Widget *w, FDK_ClickCb cb, void *ud);

/* ─── Radio button ───────────────────────────────────────────────────────── */
/* Radio buttons share a group pointer — selecting one deselects the rest.
 * Pass a pointer to a local int that holds the selected index (0-based).
 *   int *group = &my_selected;
 *   fdk_radio_button("A", group, 0);
 *   fdk_radio_button("B", group, 1);
 */
FDK_Widget *fdk_radio_button(const char *label, int *group, int index);
void        fdk_radio_button_on_change(FDK_Widget *w, FDK_ClickCb cb, void *ud);

/* ─── Spinner (numeric input) ────────────────────────────────────────────── */
FDK_Widget *fdk_spinner(double min, double max, double value, double step);
double      fdk_spinner_get_value(const FDK_Widget *w);
void        fdk_spinner_set_value(FDK_Widget *w, double value);
void        fdk_spinner_set_range(FDK_Widget *w, double min, double max);
void        fdk_spinner_set_step(FDK_Widget *w, double step);
void        fdk_spinner_set_decimals(FDK_Widget *w, int decimals); /* display precision */
void        fdk_spinner_on_change(FDK_Widget *w, FDK_ValueCb cb, void *ud);

/* ─── Notification / toast ────────────────────────────────────────────────── */
/* Toasts are window-level overlays, not widgets in the tree.
 * fdk_notify() is called with a window handle; fdk_ui_step() drives them.
 * Up to FDK_TOAST_MAX simultaneous toasts; oldest dismissed when full. */
#define FDK_TOAST_MAX 4

typedef enum {
    FDK_NOTIFY_INFO    = 0,
    FDK_NOTIFY_SUCCESS,
    FDK_NOTIFY_WARNING,
    FDK_NOTIFY_ERROR,
} FDK_NotifyKind;

/* Post a toast. duration_ms = 0 → 2500ms default. Thread-safe via flag. */
void fdk_notify(FDK_UI *ui, const char *message,
                FDK_NotifyKind kind, uint32_t duration_ms);

/* ─── Tab bar ─────────────────────────────────────────────────────────────── */
/* fdk_tabs() creates the tab bar widget.
 * Each "page" is a child widget added with fdk_tabs_add_page().
 * The tab bar manages visibility — only the active page is shown.
 * The whole tab widget (bar + active page) fills its layout bounds. */
#define FDK_TABS_MAX 12

FDK_Widget *fdk_tabs(void);
/* Add a page. Returns the page container for the caller to populate. */
FDK_Widget *fdk_tabs_add_page(FDK_Widget *tabs, const char *label);
void        fdk_tabs_set_active(FDK_Widget *tabs, int index);
int         fdk_tabs_get_active(const FDK_Widget *tabs);
void        fdk_tabs_on_change(FDK_Widget *tabs, void (*cb)(FDK_Widget*, int, void*), void *ud);

/* ─── Menu bar ────────────────────────────────────────────────────────────── */
/* fdk_menubar() creates a horizontal menu bar (28px tall).
 * Add menus with fdk_menubar_add_menu(), items with fdk_menu_add_item().
 * The menu bar renders as an overlay — put it first in your root vbox
 * and give it a fixed height of 28. */
#define FDK_MENU_MAX_ITEMS 24
#define FDK_MENU_MAX_MENUS 8

typedef struct FDK_MenuItem {
    char        *label;
    char        *shortcut;   /* display only, e.g. "Ctrl+S" */
    bool         separator;  /* if true, render as a divider */
    bool         disabled;
    void       (*cb)(void *ud);
    void        *ud;
} FDK_MenuItem;

typedef struct FDK_Menu {
    char         *label;
    FDK_MenuItem  items[FDK_MENU_MAX_ITEMS];
    int           item_count;
    bool          open;
} FDK_Menu;

FDK_Widget  *fdk_menubar(void);
/* Add a top-level menu (e.g. "File"). Returns the menu index. */
int          fdk_menubar_add_menu(FDK_Widget *mb, const char *label);
/* Add an item to a menu. cb fires on click. shortcut may be NULL. */
void         fdk_menu_add_item(FDK_Widget *mb, int menu_idx,
                               const char *label, const char *shortcut,
                               void (*cb)(void *ud), void *ud);
/* Add a separator line */
void         fdk_menu_add_separator(FDK_Widget *mb, int menu_idx);

/* ─── Badge ──────────────────────────────────────────────────────────────── */
/* A small coloured pill, typically for counts or status labels */
FDK_Widget *fdk_badge(const char *text);
void        fdk_badge_set_text(FDK_Widget *w, const char *text);
void        fdk_badge_set_color(FDK_Widget *w, FDK_Color bg, FDK_Color fg);

/* ─── Image ──────────────────────────────────────────────────────────────── */
/* pixels: RGBA8888, caller owns the buffer — FDK copies it internally       */
FDK_Widget *fdk_image(const uint8_t *pixels, int width, int height);
void        fdk_image_set_pixels(FDK_Widget *w, const uint8_t *pixels,
                                  int width, int height);
/* Load from a PPM (P6), PNG, or JPEG file.
 * PNG/JPEG require FDK_WITH_STB_IMAGE to be defined and stb_image.h present.*/
FDK_Widget *fdk_image_from_file(const char *path);

/* ─── Switch (slide toggle) ────────────────────────────────────────────────
 * A binary on/off switch with a different visual style from ToggleButton.
 * Renders as a rounded pill (track) with a sliding knob — matches the
 * GNOME / iOS / Android switch convention. Semantically identical to
 * ToggleButton; the difference is purely visual.
 *
 * Use ToggleButton for "button that stays pressed" semantics (e.g., a
 * Bold/Italic toggle in a toolbar). Use Switch for "preference on/off"
 * semantics (e.g., Enable Notifications). */
FDK_Widget *fdk_switch(bool active);
bool        fdk_switch_get_active(const FDK_Widget *w);
void        fdk_switch_set_active(FDK_Widget *w, bool active);
void        fdk_switch_on_change(FDK_Widget *w, FDK_ClickCb cb, void *ud);

/* ─── Level bar (segmented progress) ───────────────────────────────────────
 * A progress indicator divided into discrete segments, like a signal-
 * strength bar or battery-level indicator. Different visual from
 * ProgressBar (which is continuous).
 *
 * value is clamped to [0, max]. The number of filled segments is
 * round(value / max * segments). */
FDK_Widget *fdk_level_bar(float value, float max, int segments);
void        fdk_level_bar_set_value(FDK_Widget *w, float value);
float       fdk_level_bar_get_value(const FDK_Widget *w);
void        fdk_level_bar_set_max(FDK_Widget *w, float max);
void        fdk_level_bar_set_segments(FDK_Widget *w, int segments);

/* ─── Search entry ──────────────────────────────────────────────────────────
 * A text input specialized for search: has a magnifier-glass icon on
 * the left, a clear (×) button on the right that appears when there's
 * text, and a `search-changed` signal that is debounced (fires 300ms
 * after the last keystroke, not on every keystroke).
 *
 * Semantically a SearchEntry IS-A TextInput, but the public API is
 * separate so the search icon, clear button, and debounce are wired
 * automatically. Use fdk_search_entry_get_text() to read the current
 * query. */
FDK_Widget *fdk_search_entry(const char *placeholder);
const char *fdk_search_entry_get_text(const FDK_Widget *w);
void        fdk_search_entry_set_text(FDK_Widget *w, const char *text);
void        fdk_search_entry_clear(FDK_Widget *w);
/* Fires 300ms after the last keystroke (debounced). Use this for
 * triggering the actual search, not on_change — on_change fires on
 * every keystroke and would do too much work. */
void        fdk_search_entry_on_search(FDK_Widget *w,
                                        FDK_ChangeCb cb, void *ud);

/* ─── Expander (collapsible section) ───────────────────────────────────────
 * A label with a disclosure triangle that expands/collapses a child
 * widget. Used in settings dialogs, preference panes, advanced options.
 *
 * Click the label to toggle expansion. The child widget is shown/hidden
 * with an optional slide animation (currently instant; animation is a
 * future item). */
FDK_Widget *fdk_expander(const char *label, FDK_Widget *child);
bool        fdk_expander_get_expanded(const FDK_Widget *w);
void        fdk_expander_set_expanded(FDK_Widget *w, bool expanded);
void        fdk_expander_set_child(FDK_Widget *w, FDK_Widget *child);
void        fdk_expander_on_toggle(FDK_Widget *w, FDK_ClickCb cb, void *ud);

/* ─── Status bar ───────────────────────────────────────────────────────────
 * A bottom bar with push/pop text slots. Used by editors, IDEs, terminals
 * to show status messages, cursor position, file encoding, etc.
 *
 * Messages are stacked — push a new message and it appears; pop removes
 * the most recent, revealing the previous one. */
FDK_Widget *fdk_status_bar(void);
void        fdk_status_bar_push(FDK_Widget *w, const char *text);
void        fdk_status_bar_pop(FDK_Widget *w);
const char *fdk_status_bar_get_text(const FDK_Widget *w);

/* ─── Grid layout ──────────────────────────────────────────────────────────
 * A container that arranges children in a row/column grid. Children are
 * added with explicit row, col, and optional row_span / col_span.
 *
 * Unlike hbox/vbox which auto-flow, grid requires explicit positioning.
 * This matches the common use case of settings dialogs, forms, and
 * property editors.
 *
 * Columns can be homogeneous (all same width) or non-homogeneous (each
 * column sized to its widest child). */
#define FDK_GRID_MAX_CHILDREN 64

FDK_Widget *fdk_grid(int cols, bool homogeneous);
void        fdk_grid_add(FDK_Widget *grid, FDK_Widget *child,
                          int row, int col, int row_span, int col_span);
/* Convenience: add at the next available position (left-to-right, top-to-bottom) */
void        fdk_grid_add_next(FDK_Widget *grid, FDK_Widget *child);

/* ─── Popover ──────────────────────────────────────────────────────────────
 * A transient popup anchored to a point. Renders above all other widgets,
 * dismissed on outside-click. Used by MenuButton, combo boxes with custom
 * content, search filters, tooltips.
 *
 * The popover contains any single child widget (typically a container).
 * Show it at (x, y) in window coordinates; it auto-dismisses when the
 * user clicks outside or presses Escape. */
FDK_Widget *fdk_popover(FDK_Widget *content);
void        fdk_popover_set_content(FDK_Widget *w, FDK_Widget *content);
void        fdk_popover_show(FDK_UI *ui, FDK_Widget *popover, int x, int y);
void        fdk_popover_hide(FDK_UI *ui);
bool        fdk_popover_is_visible(const FDK_Widget *w);

/* ─── Calendar ─────────────────────────────────────────────────────────────
 * A month-view date picker. Shows a calendar grid with navigation arrows
 * for previous/next month. The user can click a day to select it.
 *
 * Keyboard: arrows to navigate, Enter to select, PageUp/Down for
 * prev/next month. */
typedef struct {
    int year;
    int month;  /* 0-11 (0 = January) */
    int day;    /* 1-31 */
} FDK_Date;

FDK_Widget *fdk_calendar(void);
FDK_Date    fdk_calendar_get_date(const FDK_Widget *w);
void        fdk_calendar_set_date(FDK_Widget *w, int year, int month, int day);
void        fdk_calendar_on_change(FDK_Widget *w,
                                    void (*cb)(FDK_Widget*, const FDK_Date*, void*),
                                    void *ud);

/* ─── File dialog ────────────────────────────────────────────────────────── */
/* A modal file picker overlay. Blocks until the user selects a file or
 * cancels, then returns the selected path (caller must free) or NULL.
 *
 * No D-Bus, no XDG portal, no external dependencies — the dialog is
 * rendered entirely by FDK as an overlay on the parent window, using
 * the same widget primitives as every other FDK widget. This matches
 * the project's "no desktop-ecosystem deps" philosophy.
 *
 * The dialog supports:
 *   - Directory navigation (double-click a dir to enter, ".." to go up)
 *   - File filtering by glob pattern (e.g. "*.txt;*.md")
 *   - Keyboard: Up/Down to navigate, Enter to open/enter, Esc to cancel
 *   - Hidden files (dotfiles) are hidden by default
 *
 * Example:
 *   FDK_FileDialogDesc desc = {
 *       .title       = "Open File",
 *       .initial_dir = NULL,       // NULL = current working directory
 *       .filter      = "*.txt",    // NULL = show all files
 *   };
 *   char *path = fdk_file_dialog(win, &desc);
 *   if (path) {
 *       printf("Selected: %s\n", path);
 *       free(path);
 *   }
 */
typedef struct {
    const char *title;        /* "Open File" / "Save File" — shown in dialog title bar */
    const char *initial_dir;  /* NULL = getcwd(), or an absolute/relative path */
    const char *filter;       /* NULL = all files, or glob: "*.txt" or "*.txt;*.md" */
} FDK_FileDialogDesc;

/* Show a modal file dialog. BLOCKS until the user selects a file or cancels.
 * Returns a malloc'd path string (caller must free) or NULL if cancelled. */
char *fdk_file_dialog(FDK_Window *win, const FDK_FileDialogDesc *desc);

/* ─── UI tooltip rendering ────────────────────────────────────────────────── */
#define FDK_TOOLTIP_DELAY_MS 600   /* hover delay before tooltip appears */

#ifdef __cplusplus
}
#endif
#endif /* FDK_WIDGET_H */
