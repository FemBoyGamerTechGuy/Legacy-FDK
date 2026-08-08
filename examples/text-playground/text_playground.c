/*
 * text_playground.c — Interactive text rendering playground
 *
 * Type or paste any text (Arabic, Hebrew, CJK, mixed LTR/RTL, etc.)
 * and see it rendered live through FDK's BiDi + HarfBuzz pipeline.
 *
 * Features:
 *   - TextInput at the top — type or Ctrl+V to paste any UTF-8 text
 *   - Preset buttons that load sample text in various scripts
 *   - Large preview area showing the text rendered at 28px
 *   - Detected paragraph direction (LTR / RTL) shown live
 *   - Byte count and codepoint count
 *   - Font selector (cycles through system fonts if available)
 *
 * This is the app to use to verify BiDi/shaping works visually.
 *
 * Build: (built by CMake as part of examples/)
 * Run:   ./examples/text-playground/fdk_text_playground
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE          /* strcasecmp */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>             /* strcasecmp */
#include <stdarg.h>              /* va_list for debug_log */
#include <dirent.h>
#include <sys/stat.h>

/* ─── Global state ────────────────────────────────────────────────────────── */

static FDK_Widget *g_input;        /* the text input field */
static FDK_Widget *g_preview;      /* custom widget that renders the preview */
static FDK_Widget *g_dir_label;    /* shows "LTR" or "RTL" */
static FDK_Widget *g_count_label;  /* shows byte/cp count */
static FDK_Widget *g_font_label;   /* shows current font name */
static FDK_Font   *g_preview_font; /* larger font for preview area */
static int         g_font_idx = 0;
static char        g_font_name[256];

/* Font discovery: scan /usr/share/fonts for .ttf files */
#define MAX_FONTS 64
static char *g_font_paths[MAX_FONTS];
static char *g_font_names[MAX_FONTS];
static int   g_font_count = 0;

/* Preset text samples */
typedef struct { const char *name; const char *text; } Preset;
static const Preset PRESETS[] = {
    {"English",     "Hello World"},
    {"Arabic",      "\u0627\u0644\u0633\u0644\u0627\u0645 \u0639\u0644\u064a\u0643\u0645"},
    /*              السلام عليكم  */
    {"Hebrew",      "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"},
    /*              שלום עולם  */
    {"Mixed L+R",   "Hello \u0627\u0644\u0639\u0627\u0644\u0645"},
    /*              Hello العالم  */
    {"Mixed R+L",   "\u05e9\u05dc\u05d5\u05dd World"},
    /*              שלום World  */
    {"Brackets",    "\u0633\u064a\u0627\u0631\u0629 (car) \u0628\u0627\u0631\u062f\u0629"},
    /*              سيارة (car) باردة  */
    {"Numbers RTL", "\u0627\u0644\u0633\u0646\u0629 1990 \u0645"},
    /*              السنة 1990 م  */
    {"CJK",         "\u4f60\u597d\u4e16\u754c \u3053\u3093\u306b\u3061\u306f"},
    /*              你好世界 こんにちは  */
    {"Long mixed",  "The word \u0633\u0644\u0627\u0645 means peace, "
                    "and \u05e9\u05dc\u05d5\u05dd also means peace. "
                    "Numbers: 123 \u0661\u0662\u0663 (Arabic-Indic)."},
};
#define N_PRESETS (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))

/* ─── Font discovery ──────────────────────────────────────────────────────── */

static int has_ttf_ext(const char *name)
{
    int len = (int)strlen(name);
    return len > 4 &&
           (strcasecmp(name + len - 4, ".ttf") == 0 ||
            strcasecmp(name + len - 4, ".otf") == 0);
}

static void scan_fonts_recursive(const char *dir_path, int depth)
{
    if (depth > 3 || g_font_count >= MAX_FONTS) return;
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && g_font_count < MAX_FONTS) {
        if (ent->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_fonts_recursive(full, depth + 1);
        } else if (S_ISREG(st.st_mode) && has_ttf_ext(ent->d_name)) {
            g_font_paths[g_font_count] = strdup(full);
            /* Use just the filename as the display name */
            g_font_names[g_font_count] = strdup(ent->d_name);
            g_font_count++;
        }
    }
    closedir(d);
}

static void discover_fonts(void)
{
    scan_fonts_recursive("/usr/share/fonts/truetype", 0);
    scan_fonts_recursive("/usr/share/fonts/opentype", 0);
    scan_fonts_recursive("/usr/share/fonts", 0);
    fprintf(stderr, "[text-playground] Found %d fonts\n", g_font_count);
}

static FDK_Font *load_preview_font(int idx)
{
    if (g_font_count == 0) return NULL;
    if (idx < 0) idx = 0;
    if (idx >= g_font_count) idx = 0;
    if (g_preview_font) fdk_font_destroy(g_preview_font);
    g_preview_font = fdk_font_load(g_font_paths[idx], 28.0f);
    if (g_preview_font) {
        strncpy(g_font_name, g_font_names[idx], sizeof(g_font_name) - 1);
        g_font_name[sizeof(g_font_name) - 1] = '\0';
    }
    g_font_idx = idx;
    return g_preview_font;
}

/* ─── Preview rendering ──────────────────────────────────────────────────── */

static void draw_preview(FDK_Widget *w, FDK_Rect r, void *ud)
{
    (void)w; (void)ud;

    /* Background */
    FDK_Color bg = {25, 25, 32, 255};
    FDK_Color border = {50, 50, 60, 255};
    fdk_fill_rect_rounded(r, 6, bg);
    fdk_stroke_rect(r, border, 1);

    /* Get the text from the input */
    const char *text = fdk_text_input_get_text(g_input);
    if (!text || !text[0]) {
        /* Show placeholder */
        FDK_Color ph = {100, 100, 120, 255};
        if (g_preview_font) {
            fdk_draw_text(g_preview_font, "Type or paste text above…",
                          r.x + 16, r.y + 20, ph);
        }
        return;
    }

    /* Render the text at the preview font size */
    FDK_Color tc = {230, 230, 240, 255};
    if (g_preview_font) {
        /* Clip to the preview area so long text doesn't overflow */
        fdk_push_clip((FDK_Rect){r.x + 2, r.y + 2, r.w - 4, r.h - 4});
        fdk_draw_text(g_preview_font, text, r.x + 16, r.y + 20, tc);
        fdk_pop_clip();
    }
}

/* ─── Debug logging ──────────────────────────────────────────────────────── */
/* Set FDK_DEBUG=1 in the environment to enable. Logs to stderr AND to
 * debug.log in the current directory. Shows every event with mouse
 * coordinates, scroll values, and scroll-view state. */
static bool g_debug = false;
static FILE *g_debug_file = NULL;

static void debug_init(void)
{
    const char *env = getenv("FDK_DEBUG");
    if (env && env[0] == '1') {
        g_debug = true;
        g_debug_file = fopen("debug.log", "w");
        if (g_debug_file) {
            setvbuf(g_debug_file, NULL, _IOLBF, 0); /* line-buffered */
            fprintf(g_debug_file, "=== FDK Text Playground Debug Log ===\n");
        }
        fprintf(stderr, "[FDK_DEBUG] Logging to debug.log (and stderr)\n");
    }
}

static void debug_log(const char *fmt, ...)
{
    if (!g_debug) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[FDK_DEBUG] %s\n", buf);
    if (g_debug_file) {
        fprintf(g_debug_file, "%s\n", buf);
    }
}

static void debug_event(const FDK_Event *ev)
{
    if (!g_debug) return;
    switch (ev->type) {
    case FDK_EVENT_NONE:
        /* Don't log NONE events — too noisy (fires every 16ms) */
        break;
    case FDK_EVENT_MOUSE_SCROLL:
        debug_log("MOUSE_SCROLL  mouse=(%d,%d)  scroll=(%.1f,%.1f)",
                  ev->mouse.x, ev->mouse.y, ev->scroll.dx, ev->scroll.dy);
        break;
    case FDK_EVENT_MOUSE_MOVE:
        /* Don't log MOUSE_MOVE — too noisy */
        break;
    case FDK_EVENT_MOUSE_DOWN:
        debug_log("MOUSE_DOWN    mouse=(%d,%d)  button=%d",
                  ev->mouse.x, ev->mouse.y, ev->mouse.button);
        break;
    case FDK_EVENT_MOUSE_UP:
        debug_log("MOUSE_UP      mouse=(%d,%d)  button=%d",
                  ev->mouse.x, ev->mouse.y, ev->mouse.button);
        break;
    case FDK_EVENT_KEY_DOWN:
        debug_log("KEY_DOWN      key=%d  codepoint=%u  mods=%d",
                  ev->key.key, ev->key.codepoint, ev->key.mods);
        break;
    case FDK_EVENT_RESIZE:
        debug_log("RESIZE        %dx%d", ev->resize.w, ev->resize.h);
        break;
    case FDK_EVENT_EXPOSE:
        debug_log("EXPOSE");
        break;
    case FDK_EVENT_CLOSE:
        debug_log("CLOSE");
        break;
    case FDK_EVENT_QUIT:
        debug_log("QUIT");
        break;
    default:
        debug_log("EVENT type=%d", ev->type);
        break;
    }
}

static void debug_shutdown(void)
{
    if (g_debug_file) {
        fprintf(g_debug_file, "=== End of debug log ===\n");
        fclose(g_debug_file);
        g_debug_file = NULL;
    }
    g_debug = false;
}



/* Count codepoints in a UTF-8 string */
static int utf8_count_cps(const char *s)
{
    int n = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) { s++; n++; }
        else if (c < 0xC0) { s++; } /* stray continuation */
        else if (c < 0xE0) { s += 2; n++; }
        else if (c < 0xF0) { s += 3; n++; }
        else { s += 4; n++; }
    }
    return n;
}

/* Detect paragraph direction: 0=LTR, 1=RTL.
 * Walks the string looking for the first strong directional char. */
static int detect_direction(const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s;
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if (c < 0xC0) { s++; continue; }
        else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
        else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
        else { cp = c & 0x07; extra = 3; }
        s++;
        for (int i = 0; i < extra && *s; i++, s++)
            cp = (cp << 6) | ((unsigned char)*s & 0x3F);

        /* Check if it's a strong LTR or RTL char */
        if (cp >= 0x0041 && cp <= 0x005A) return 0; /* A-Z */
        if (cp >= 0x0061 && cp <= 0x007A) return 0; /* a-z */
        if (cp >= 0x0590 && cp <= 0x05FF) return 1; /* Hebrew */
        if (cp >= 0x0600 && cp <= 0x06FF) return 1; /* Arabic */
        if (cp >= 0x0700 && cp <= 0x074F) return 1; /* Syriac */
        if (cp >= 0x0780 && cp <= 0x07BF) return 1; /* Thaana */
        if (cp >= 0x07C0 && cp <= 0x07FF) return 1; /* NKo */
        if (cp >= 0x0800 && cp <= 0x083F) return 1; /* Samaritan */
        if (cp >= 0x0840 && cp <= 0x085F) return 1; /* Mandaic */
        if (cp >= 0xFB1D && cp <= 0xFB4F) return 1; /* Hebrew Pres Forms */
        if (cp >= 0xFB50 && cp <= 0xFDFF) return 1; /* Arabic Pres Forms-A */
        if (cp >= 0xFE70 && cp <= 0xFEFF) return 1; /* Arabic Pres Forms-B */
        if (cp >= 0x10800 && cp <= 0x10CFF) return 1; /* Historical RTL */
        if (cp >= 0x10D00 && cp <= 0x10D3F) return 1; /* Hanifi Rohingya */
        if (cp >= 0x10D40 && cp <= 0x10EBF) return 1; /* Yezidi */
        if (cp >= 0x1E800 && cp <= 0x1EC6F) return 1; /* Mende/Adlam */
    }
    return 0; /* default LTR */
}

/* ─── Update the info labels ─────────────────────────────────────────────── */

static void update_info(void)
{
    const char *text = fdk_text_input_get_text(g_input);
    if (!text) text = "";

    /* Direction */
    int dir = detect_direction(text);
    char dir_buf[64];
    snprintf(dir_buf, sizeof dir_buf, "Direction: %s", dir ? "RTL" : "LTR");
    fdk_label_set_text(g_dir_label, dir_buf);

    /* Counts */
    int bytes = (int)strlen(text);
    int cps = utf8_count_cps(text);
    char count_buf[128];
    snprintf(count_buf, sizeof count_buf, "Bytes: %d  |  Codepoints: %d", bytes, cps);
    fdk_label_set_text(g_count_label, count_buf);

    /* Font name */
    char font_buf[300];
    snprintf(font_buf, sizeof font_buf, "Font: %s  (%d/%d)",
             g_font_count > 0 ? g_font_name : "(none)",
             g_font_idx + 1, g_font_count);
    fdk_label_set_text(g_font_label, font_buf);
}

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

static void on_text_change(FDK_Widget *w, const char *text, void *ud)
{
    (void)w; (void)text; (void)ud;
    update_info();
}

static void on_preset(FDK_Widget *w, void *ud)
{
    int idx = (int)(intptr_t)ud;
    if (idx < 0 || idx >= N_PRESETS) return;
    fdk_text_input_set_text(g_input, PRESETS[idx].text);
    update_info();
}

static void on_prev_font(FDK_Widget *w, void *ud)
{
    (void)w; (void)ud;
    if (g_font_count == 0) return;
    int idx = g_font_idx - 1;
    if (idx < 0) idx = g_font_count - 1;
    load_preview_font(idx);
    update_info();
}

static void on_next_font(FDK_Widget *w, void *ud)
{
    (void)w; (void)ud;
    if (g_font_count == 0) return;
    int idx = g_font_idx + 1;
    if (idx >= g_font_count) idx = 0;
    load_preview_font(idx);
    update_info();
}

static void on_clear(FDK_Widget *w, void *ud)
{
    (void)w; (void)ud;
    fdk_text_input_set_text(g_input, "");
    update_info();
}

/* ─── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    debug_init();
    FDK_InitInfo info = {
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = "text_playground",
    };
    if (!fdk_init(&info)) {
        fprintf(stderr, "fdk_init failed\n");
        return 1;
    }

    discover_fonts();
    load_preview_font(0);

    FDK_WindowDesc desc = {
        .title = "FDK Text Playground — BiDi + HarfBuzz",
        .x = -1, .y = -1, .w = 800, .h = 600,
        .resizable = true,
        .csd = true,
    };
    FDK_Window *win = fdk_window_create(&desc);
    if (!win) { fdk_shutdown(); return 1; }
    fdk_window_show(win);

    FDK_UI *ui = fdk_ui_create(win, NULL);
    if (!ui) { fdk_window_destroy(win); fdk_shutdown(); return 1; }

    /* The default C theme has font_body = NULL, which means no text
     * renders. We'll load a font and set it on the theme after
     * creating the widget tree. */

    /* Build the UI — root is a vbox with titlebar on top, content below */
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    /* Titlebar — buttons auto-wired to minimize/maximize/close.
     * Drag the empty area to move the window (driven by WM/compositor).
     * Double-click the empty area to toggle maximized state. */
    FDK_Widget *titlebar = fdk_titlebar("FDK Text Playground — BiDi + HarfBuzz");
    fdk_widget_set_size(titlebar, FDK_SIZE_FILL, 40);
    fdk_container_add(root, titlebar);

    /* Body — content wrapped in a scroll view so the window can be
     * smaller than all the widgets. Without this, the new widgets
     * section at the bottom is invisible (clipped by the window edge). */
    FDK_Widget *body = fdk_vbox(6, 12);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);

    FDK_Widget *scroll = fdk_scroll_view(body);
    fdk_widget_set_size(scroll, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, scroll);

    /* Title */
    FDK_Widget *title = fdk_label("Type or paste any text — Arabic, Hebrew, CJK, mixed…");
    fdk_container_add(body, title);

    /* Text input */
    g_input = fdk_text_input("Type here, or Ctrl+V to paste…");
    fdk_widget_set_size(g_input, FDK_SIZE_FILL, 36);
    fdk_text_input_on_change(g_input, on_text_change, NULL);
    fdk_container_add(body, g_input);

    /* Info labels row */
    FDK_Widget *info_row = fdk_hbox(8, 4);
    g_dir_label = fdk_label("Direction: LTR");
    g_count_label = fdk_label("Bytes: 0  |  Codepoints: 0");
    fdk_container_add(info_row, g_dir_label);
    fdk_container_add(info_row, g_count_label);
    fdk_container_add(body, info_row);

    /* Font selector row */
    FDK_Widget *font_row = fdk_hbox(4, 4);
    FDK_Widget *prev_font = fdk_button("<");
    fdk_button_on_click(prev_font, on_prev_font, NULL);
    g_font_label = fdk_label("Font: (none)");
    fdk_widget_set_size(g_font_label, FDK_SIZE_FILL, -1);
    FDK_Widget *next_font = fdk_button(">");
    fdk_button_on_click(next_font, on_next_font, NULL);
    fdk_container_add(font_row, prev_font);
    fdk_container_add(font_row, g_font_label);
    fdk_container_add(font_row, next_font);
    fdk_container_add(body, font_row);

    /* Preview area (large) */
    g_preview = fdk_custom(FDK_SIZE_FILL, 120, draw_preview, NULL);
    fdk_container_add(body, g_preview);

    /* Preset buttons */
    FDK_Widget *preset_label = fdk_label("Presets — click to load sample text:");
    fdk_container_add(body, preset_label);

    /* Grid of preset buttons (2 rows) */
    for (int row = 0; row < 2; row++) {
        FDK_Widget *row_w = fdk_hbox(4, 4);
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            if (idx >= N_PRESETS) break;
            FDK_Widget *btn = fdk_button(PRESETS[idx].name);
            fdk_button_on_click(btn, on_preset, (void *)(intptr_t)idx);
            fdk_widget_set_size(btn, FDK_SIZE_FILL, 32);
            fdk_container_add(row_w, btn);
        }
        fdk_container_add(body, row_w);
    }

    /* Clear button */
    FDK_Widget *clear_btn = fdk_button("Clear");
    fdk_button_on_click(clear_btn, on_clear, NULL);
    fdk_container_add(body, clear_btn);

    /* ─── New Widgets Showcase ─────────────────────────────────────── */
    FDK_Widget *showcase_label = fdk_label("─── New Widgets (v0.2) ───");
    fdk_container_add(body, showcase_label);

    /* Switch row */
    FDK_Widget *switch_row = fdk_hbox(8, 4);
    fdk_container_add(switch_row, fdk_label("Switch:"));
    fdk_container_add(switch_row, fdk_switch(false));
    fdk_container_add(switch_row, fdk_switch(true));
    fdk_container_add(body, switch_row);

    /* LevelBar row */
    FDK_Widget *level_row = fdk_hbox(8, 4);
    fdk_container_add(level_row, fdk_label("LevelBar (2/4):"));
    fdk_container_add(level_row, fdk_level_bar(2.0f, 4.0f, 4));
    fdk_container_add(body, level_row);

    /* StatusBar */
    FDK_Widget *status = fdk_status_bar();
    fdk_status_bar_push(status, "Ready — try the widgets above");
    fdk_container_add(body, status);

    /* Expander with content */
    FDK_Widget *exp_content = fdk_vbox(4, 4);
    fdk_container_add(exp_content, fdk_label("  This content is inside an expander."));
    fdk_container_add(exp_content, fdk_label("  Click the header to collapse/expand."));
    FDK_Widget *exp = fdk_expander("Advanced Settings (click to expand)", exp_content);
    fdk_container_add(body, exp);

    /* Spacer between expander and calendar */
    fdk_container_add(body, fdk_separator());
    fdk_container_add(body, fdk_label(""));

    /* Calendar */
    fdk_container_add(body, fdk_label("Calendar:"));
    FDK_Widget *cal = fdk_calendar();
    fdk_widget_set_size(cal, 260, 240);
    fdk_container_add(body, cal);

    /* Initialize info display */
    update_info();

    /* Load a UI font and set it on the theme so all widgets render text.
     * The default C theme has font_body = NULL (no font), so without
     * this, fdk_draw_text does nothing and no text appears. */
    {
        const char *font_candidates[] = {
            /* Debian/Ubuntu */
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            /* Arch/Artix/Fedora */
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            /* Generic fallbacks */
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            NULL,
        };
        FDK_Font *ui_font = NULL;
        for (int i = 0; font_candidates[i]; i++) {
            ui_font = fdk_font_load(font_candidates[i], 15.0f);
            if (ui_font) break;
        }
        if (ui_font) {
            FDK_Theme theme = fdk_theme_resolve(NULL);
            theme.font_body = ui_font;
            theme.font_label = ui_font;
            fdk_ui_set_theme(ui, root, &theme);
        } else {
            fprintf(stderr, "[text-playground] WARNING: No UI font found — "
                    "text will not render. Install fonts-dejavu.\n");
        }
    }

    /* Run — manual event loop with "wait for first EXPOSE/RESIZE" pattern
     * (same as csd_demo). Under a real WM like XFCE's xfwm4, the window
     * may not be immediately ready for painting — we need to wait for
     * the first EXPOSE or RESIZE event before doing layout+paint. */
    FDK_Event ev;
    bool got_size = false;
    while (!got_size) {
        fdk_wait_event(&ev);
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) {
            goto cleanup;
        }
        if (ev.type == FDK_EVENT_RESIZE || ev.type == FDK_EVENT_EXPOSE) {
            got_size = true;
            fdk_ui_layout(ui, root);
            fdk_ui_paint(ui, root);
        }
    }

    for (;;) {
        bool got = fdk_wait_event_timeout(&ev, 16);
        if (!got) ev.type = FDK_EVENT_NONE;

        debug_event(&ev);

        if (ev.type == FDK_EVENT_QUIT) break;
        if (ev.type == FDK_EVENT_CLOSE) break;
        if (ev.type == FDK_EVENT_KEY_DOWN && ev.key.key == FDK_KEY_ESCAPE) break;

        fdk_ui_step(ui, root, &ev);

        /* Check CLOSE again AFTER fdk_ui_step */
        if (ev.type == FDK_EVENT_CLOSE) break;
    }

cleanup:
    debug_shutdown();

    /* Cleanup */
    fdk_widget_destroy(root);
    if (g_preview_font) fdk_font_destroy(g_preview_font);
    for (int i = 0; i < g_font_count; i++) {
        free(g_font_paths[i]);
        free(g_font_names[i]);
    }
    fdk_ui_destroy(ui);
    fdk_window_destroy(win);
    fdk_shutdown();
    return 0;
}
