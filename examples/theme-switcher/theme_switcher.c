/*
 * theme_switcher.c — FDK comprehensive widget + live theme demo
 *
 * This file absorbed examples/showcase and examples/widgets during a
 * consolidation pass (6 example apps -> 4 -> this, 3): both were
 * "look at every widget" galleries with real but non-overlapping
 * pieces (showcase had the only MenuBar/Tabs/about-page; widgets had
 * the only fdk_widget_find()-by-tag usage) sitting alongside a lot of
 * content that just re-tested the same toggle/radio/spinner/badge/
 * tween/toast widgets theme_switcher already covered. Rather than
 * concatenate three files' worth of near-duplicate widget rows, the
 * UI below is reorganized into tabs so every widget type still gets
 * demonstrated exactly once.
 *
 * What it shows:
 *   - Every widget type at once so theme differences are immediately
 *     visible: buttons (incl. accent/danger/ghost variants), toggle,
 *     radio, checkbox, slider, progress bar, spinner, badge, dropdown,
 *     text input, separator, tabs, menubar, toasts.
 *   - Live theme switching — a radio group swaps between the three
 *     bundled themes at runtime via fdk_ui_set_theme().
 *   - Hot-reload — a "Watch live" toggle starts/stops fdk_theme_watch()
 *     on /tmp/fdk_live_theme.fdktheme; edit that file while the demo
 *     runs and the whole UI updates within one frame.
 *   - Two ways to reference a widget later: a direct FDK_Widget*
 *     kept in a closure/global (g_tween_bar), and fdk_widget_find()
 *     by integer tag (the volume spinner's badge+progress bar) — both
 *     are real patterns apps use, shown side by side on purpose.
 *
 * Run:
 *   ./fdk_theme_switcher                 — uses themes/ next to the binary
 *   ./fdk_theme_switcher /path/to/themes — explicit themes directory
 *   ./fdk_theme_switcher --auto          — zero-code three-tier resolution,
 *                                          same as fdk_ui_create(win, NULL)
 */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── Tags for fdk_widget_find() lookups ──────────────────────────────── */
#define TAG_VOL_BADGE    1
#define TAG_VOL_PROGRESS 2

/* ── Globals ──────────────────────────────────────────────────────────── */
static bool        g_running     = true;
static FDK_UI     *g_ui          = NULL;
static FDK_Widget *g_root        = NULL;
static FDK_Font   *g_font        = NULL;   /* tracked so it can be freed at exit */
static int         g_theme_sel   = 0;      /* radio group: 0=faded-dream 1=void 2=rose */
static int         g_demo_radio  = 0;      /* unrelated demo radio group, "Widgets" tab */
static char        g_themes_dir[256] = "themes";
static const char *LIVE_PATH     = "/tmp/fdk_live_theme.fdktheme";
static FDK_Widget *g_tween_bar   = NULL;

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

/* ── Theme switching ──────────────────────────────────────────────────── */
static void apply_theme_file(const char *filename)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_themes_dir, filename);

    FDK_Theme t = fdk_theme_faded_dream();  /* sane base for unset fields */
    bool ok = fdk_theme_load(&t, path);
    if (!ok) {
        fprintf(stderr, "[theme-switcher] failed to load %s\n", path);
        fdk_notify(g_ui, "Theme file not found", FDK_NOTIFY_ERROR, 0);
        return;
    }
    fdk_ui_set_theme(g_ui, g_root, &t);

    char msg[128];
    snprintf(msg, sizeof msg, "Theme: %s", filename);
    fdk_notify(g_ui, msg, FDK_NOTIFY_INFO, 1500);
}

static void on_theme_radio(FDK_Widget *w, void *ud)
{
    (void)w; (void)ud;
    switch (g_theme_sel) {
    case 0: apply_theme_file("faded-dream.fdktheme"); break;
    case 1: apply_theme_file("void.fdktheme");        break;
    case 2: apply_theme_file("rose.fdktheme");         break;
    }
}

/* ── Hot-reload toggle ────────────────────────────────────────────────── */
static void on_watch_toggle(FDK_Widget *w, void *ud)
{
    (void)ud;
    bool active = fdk_toggle_button_get_active(w);
    if (active) {
        /* Seed the live file with a copy of the currently-selected
         * bundled theme so editing has a sane starting point. */
        const char *src_name =
            g_theme_sel == 0 ? "faded-dream.fdktheme" :
            g_theme_sel == 1 ? "void.fdktheme" : "rose.fdktheme";
        char src[512];
        snprintf(src, sizeof src, "%s/%s", g_themes_dir, src_name);

        struct stat st;
        if (stat(LIVE_PATH, &st) != 0) {
            /* Only seed if it doesn't already exist — don't clobber edits */
            FILE *in = fopen(src, "r");
            FILE *out = in ? fopen(LIVE_PATH, "w") : NULL;
            if (in && out) {
                char buf[4096]; size_t n;
                while ((n = fread(buf, 1, sizeof buf, in)) > 0)
                    fwrite(buf, 1, n, out);
            }
            if (in)  fclose(in);
            if (out) fclose(out);
        }

        fdk_theme_watch(g_ui, g_root, LIVE_PATH);
        fdk_notify(g_ui, "Watching /tmp/fdk_live_theme.fdktheme \xe2\x80\x94 edit it!",
                   FDK_NOTIFY_SUCCESS, 3000);
    } else {
        fdk_theme_watch(g_ui, g_root, NULL);  /* stop watching */
        fdk_notify(g_ui, "Stopped watching live theme", FDK_NOTIFY_INFO, 1500);
    }
}

/* ── Misc callbacks ───────────────────────────────────────────────────── */
static void on_quit(FDK_Widget *w, void *ud) { (void)w; *(bool*)ud = false; }

static void on_notify_success(FDK_Widget *w, void *ud)
{ (void)w; (void)ud; fdk_notify(g_ui, "Success toast", FDK_NOTIFY_SUCCESS, 0); }
static void on_notify_warning(FDK_Widget *w, void *ud)
{ (void)w; (void)ud; fdk_notify(g_ui, "Warning toast", FDK_NOTIFY_WARNING, 0); }
static void on_notify_error(FDK_Widget *w, void *ud)
{ (void)w; (void)ud; fdk_notify(g_ui, "Error toast", FDK_NOTIFY_ERROR, 0); }

static void tween_cb(float v, void *ud) { (void)ud; fdk_progress_set_value(g_tween_bar, v); }
static void on_animate(FDK_Widget *w, void *ud)
{ (void)w; (void)ud; fdk_tween(0.f, 1.f, 900, FDK_EASE_OUT_ELASTIC, tween_cb, NULL, NULL); }

/* Volume spinner: demonstrates fdk_widget_find()-by-tag rather than a
 * direct pointer, so both reference patterns appear in this file. */
static void on_volume_spinner(FDK_Widget *w, float v, void *ud)
{
    FDK_Widget *tab_root = ud;
    FDK_Widget *bar = fdk_widget_find(tab_root, TAG_VOL_PROGRESS);
    if (bar) fdk_progress_set_value(bar, v / 100.f);
    FDK_Widget *badge = fdk_widget_find(tab_root, TAG_VOL_BADGE);
    if (badge) {
        char buf[12];
        snprintf(buf, sizeof buf, "%.0f%%", v);
        fdk_badge_set_text(badge, buf);
    }
    (void)w;
}

/* ── Menu bar callbacks (File / Edit / Help) ─────────────────────────── */
static void menu_new(void *ud)
{ (void)ud; fdk_notify(g_ui, "New file created", FDK_NOTIFY_SUCCESS, 0); }
static void menu_open(void *ud)
{ (void)ud; fdk_notify(g_ui, "Open: no file selected", FDK_NOTIFY_WARNING, 0); }
static void menu_save(void *ud)
{ (void)ud; fdk_notify(g_ui, "Saved successfully", FDK_NOTIFY_SUCCESS, 0); }
static void menu_quit(void *ud)
{ (void)ud; g_running = false; }
static void menu_undo(void *ud)
{ (void)ud; fdk_notify(g_ui, "Nothing to undo", FDK_NOTIFY_INFO, 0); }
static void menu_about(void *ud)
{ (void)ud; fdk_notify(g_ui, "FDK \xe2\x80\x94 Faded Dream Kit", FDK_NOTIFY_INFO, 3500); }

static FDK_Widget *build_menubar(void)
{
    FDK_Widget *mb = fdk_menubar();
    fdk_widget_set_size(mb, FDK_SIZE_FILL, 28);

    int file = fdk_menubar_add_menu(mb, "File");
    fdk_menu_add_item(mb, file, "New",  "Ctrl+N", menu_new,  NULL);
    fdk_menu_add_item(mb, file, "Open", "Ctrl+O", menu_open, NULL);
    fdk_menu_add_item(mb, file, "Save", "Ctrl+S", menu_save, NULL);
    fdk_menu_add_separator(mb, file);
    fdk_menu_add_item(mb, file, "Quit", "Ctrl+Q", menu_quit, NULL);

    int edit = fdk_menubar_add_menu(mb, "Edit");
    fdk_menu_add_item(mb, edit, "Undo", "Ctrl+Z", menu_undo, NULL);
    fdk_menu_add_item(mb, edit, "Redo", "Ctrl+Y", NULL,      NULL);
    fdk_menu_add_separator(mb, edit);
    fdk_menu_add_item(mb, edit, "Cut",   "Ctrl+X", NULL, NULL);
    fdk_menu_add_item(mb, edit, "Copy",  "Ctrl+C", NULL, NULL);
    fdk_menu_add_item(mb, edit, "Paste", "Ctrl+V", NULL, NULL);

    int help = fdk_menubar_add_menu(mb, "Help");
    fdk_menu_add_item(mb, help, "About FDK", NULL, menu_about, NULL);

    return mb;
}

/* ── Tab pages ────────────────────────────────────────────────────────── */

/* "Widgets" — buttons, variants, toggle/checkbox/slider, dropdown+input */
static void build_tab_widgets(FDK_Widget *page)
{
    fdk_container_add(page, fdk_label("Buttons (variants from [button.*] sections):"));
    FDK_Widget *row_btn = fdk_hbox(10, 0);
    fdk_widget_set_size(row_btn, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    FDK_Widget *btn_default = fdk_button("Default");
    fdk_widget_set_size(btn_default, 110, 36);
    fdk_container_add(row_btn, btn_default);

    FDK_Widget *btn_accent = fdk_button("Accent");
    fdk_widget_set_variant(btn_accent, "accent");
    fdk_widget_set_size(btn_accent, 110, 36);
    fdk_container_add(row_btn, btn_accent);

    FDK_Widget *btn_danger = fdk_button("Danger");
    fdk_widget_set_variant(btn_danger, "danger");
    fdk_widget_set_size(btn_danger, 110, 36);
    fdk_container_add(row_btn, btn_danger);

    FDK_Widget *btn_ghost = fdk_button("Ghost");
    fdk_widget_set_variant(btn_ghost, "ghost");
    fdk_widget_set_size(btn_ghost, 110, 36);
    fdk_container_add(row_btn, btn_ghost);

    fdk_container_add(page, row_btn);
    fdk_container_add(page, fdk_separator());

    fdk_container_add(page, fdk_label("Toggle / radio / checkbox / slider:"));
    FDK_Widget *row_inputs = fdk_hbox(16, 0);
    fdk_widget_set_size(row_inputs, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    fdk_container_add(row_inputs, fdk_toggle_button("Notifications", true));
    fdk_container_add(row_inputs, fdk_checkbox("Remember me", true));

    const char *demo_labels[] = { "Alpha", "Beta", "Gamma" };
    for (int i = 0; i < 3; i++)
        fdk_container_add(row_inputs, fdk_radio_button(demo_labels[i], &g_demo_radio, i));

    FDK_Widget *slider = fdk_slider(0.f, 100.f, 60.f);
    fdk_widget_set_size(slider, 160, FDK_SIZE_WRAP);
    fdk_container_add(row_inputs, slider);

    fdk_container_add(page, row_inputs);
    fdk_container_add(page, fdk_separator());

    fdk_container_add(page, fdk_label("Dropdown + text input:"));
    FDK_Widget *row_dd = fdk_hbox(12, 0);
    fdk_widget_set_size(row_dd, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    FDK_Widget *dropdown = fdk_dropdown("Select option");
    fdk_dropdown_add_item(dropdown, "Option A");
    fdk_dropdown_add_item(dropdown, "Option B");
    fdk_dropdown_add_item(dropdown, "Option C");
    fdk_widget_set_size(dropdown, 160, 36);
    fdk_container_add(row_dd, dropdown);

    FDK_Widget *input = fdk_text_input("Type something...");
    fdk_widget_set_size(input, FDK_SIZE_FILL, 36);
    fdk_container_add(row_dd, input);

    fdk_container_add(page, row_dd);
}

/* "Animation & Toasts" — spinner+badge+progress (tag-based lookup),
 * tween demo, toast buttons */
static void build_tab_animation(FDK_Widget *page)
{
    fdk_container_add(page, fdk_label("Spinner (updates badge + bar via "
                                       "fdk_widget_find() by tag):"));
    FDK_Widget *progress = fdk_progress_bar(0.42f);
    fdk_widget_set_size(progress, FDK_SIZE_FILL, 18);
    fdk_widget_set_tag(progress, TAG_VOL_PROGRESS);

    FDK_Widget *row_spin = fdk_hbox(12, 0);
    fdk_widget_set_size(row_spin, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_spin, fdk_label("Volume:"));

    FDK_Widget *spinner = fdk_spinner(0.0, 100.0, 42.0, 1.0);
    fdk_spinner_set_decimals(spinner, 0);
    fdk_widget_set_size(spinner, 130, 36);
    fdk_spinner_on_change(spinner, on_volume_spinner, page);
    fdk_container_add(row_spin, spinner);

    FDK_Widget *badge = fdk_badge("42%");
    fdk_widget_set_tag(badge, TAG_VOL_BADGE);
    fdk_container_add(row_spin, badge);

    fdk_container_add(page, row_spin);
    fdk_container_add(page, progress);
    fdk_container_add(page, fdk_separator());

    fdk_container_add(page, fdk_label("Tween (OUT_ELASTIC):"));
    FDK_Widget *tween_bar = fdk_progress_bar(0.f);
    fdk_widget_set_size(tween_bar, FDK_SIZE_FILL, 18);
    g_tween_bar = tween_bar;
    fdk_container_add(page, tween_bar);

    FDK_Widget *row_toast = fdk_hbox(10, 0);
    fdk_widget_set_size(row_toast, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    FDK_Widget *btn_anim = fdk_button("Animate");
    fdk_button_on_click(btn_anim, on_animate, NULL);
    fdk_widget_set_tooltip(btn_anim, "Launches an elastic tween on the bar above");
    fdk_widget_set_size(btn_anim, 100, 36);
    fdk_container_add(row_toast, btn_anim);

    FDK_Widget *btn_succ = fdk_button("Toast: Success");
    fdk_widget_set_variant(btn_succ, "accent");
    fdk_button_on_click(btn_succ, on_notify_success, NULL);
    fdk_widget_set_size(btn_succ, 140, 36);
    fdk_container_add(row_toast, btn_succ);

    FDK_Widget *btn_warn = fdk_button("Toast: Warning");
    fdk_button_on_click(btn_warn, on_notify_warning, NULL);
    fdk_widget_set_size(btn_warn, 140, 36);
    fdk_container_add(row_toast, btn_warn);

    FDK_Widget *btn_err = fdk_button("Toast: Error");
    fdk_widget_set_variant(btn_err, "danger");
    fdk_button_on_click(btn_err, on_notify_error, NULL);
    fdk_widget_set_size(btn_err, 130, 36);
    fdk_container_add(row_toast, btn_err);

    fdk_container_add(page, row_toast);
}

/* "About" — platform/build info, ported from the old showcase.c */
static void build_tab_about(FDK_Widget *page)
{
    fdk_container_add(page, fdk_label("FDK \xe2\x80\x94 Faded Dream Kit"));
    fdk_container_add(page, fdk_separator());
    fdk_container_add(page, fdk_label("Platform:   Linux (Wayland + X11)"));
    fdk_container_add(page, fdk_label("Renderer:   Software (Cairo-free)"));
    fdk_container_add(page, fdk_label("License:    Proprietary \xe2\x80\x94 see LICENSE"));
    fdk_container_add(page, fdk_label("Font:       FreeType (FTL)"));
    fdk_container_add(page, fdk_separator());
    fdk_container_add(page, fdk_label("Widget types demonstrated across this app:"));
    fdk_container_add(page, fdk_label(
        "  \xe2\x80\xa2  Buttons, toggle, radio, checkbox, slider, dropdown, text input"));
    fdk_container_add(page, fdk_label(
        "  \xe2\x80\xa2  Spinner, progress bar, badge, separator, tooltip"));
    fdk_container_add(page, fdk_label(
        "  \xe2\x80\xa2  Tabs, menubar, toast notifications, tween animation"));
    fdk_container_add(page, fdk_label(
        "  \xe2\x80\xa2  Live theme switching + fdk_theme_watch() hot-reload"));
}

/* ── Build the full UI ────────────────────────────────────────────────── */
static FDK_Widget *build_ui(void)
{
    /* No padding on the outer box — titlebar/menubar touch the window edges */
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    /* CSD step 1 (rendering only) added here so it's actually visible
     * somewhere real, not just a throwaway verification program. Two
     * things NOT to expect yet, both already tracked as their own
     * separate roadmap sub-items:
     *   - The minimize/maximize/close buttons don't respond to clicks
     *     yet -- hit-testing isn't wired up (step 2).
     *   - Hyprland's own window chrome (if it draws any at all for a
     *     tiling layout) hasn't been told to step aside -- Wayland
     *     decoration-negotiation isn't implemented yet (step 4), so
     *     you may see this titlebar AND whatever Hyprland itself
     *     shows, not just this one on its own. */
    FDK_Widget *tb = fdk_titlebar("FDK Theme Switcher");
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);
    fdk_container_add(root, build_menubar());

    /* Inner vbox with padding for everything below the menubar */
    FDK_Widget *body = fdk_vbox(14, 16);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);

    /* ── Live theme-switching row — kept outside the tabs since it's
     * this app's headline feature, not just one widget among many ── */
    fdk_container_add(body, fdk_label("FDK \xe2\x80\x94 Theme Switcher"));

    FDK_Widget *row_theme = fdk_hbox(16, 0);
    fdk_widget_set_size(row_theme, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_theme, fdk_label("Theme:"));

    const char *theme_labels[] = { "Faded Dream", "Void", "Ros\xc3\xa9" };
    for (int i = 0; i < 3; i++) {
        FDK_Widget *rb = fdk_radio_button(theme_labels[i], &g_theme_sel, i);
        fdk_radio_button_on_change(rb, on_theme_radio, NULL);
        fdk_container_add(row_theme, rb);
    }

    FDK_Widget *spacer1 = fdk_label("");
    fdk_widget_set_size(spacer1, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_theme, spacer1);

    FDK_Widget *watch_toggle = fdk_toggle_button("Watch live", false);
    fdk_toggle_button_on_change(watch_toggle, on_watch_toggle, NULL);
    fdk_widget_set_tooltip(watch_toggle,
        "Hot-reload /tmp/fdk_live_theme.fdktheme via inotify");
    fdk_container_add(row_theme, watch_toggle);

    fdk_container_add(body, row_theme);
    fdk_container_add(body, fdk_separator());

    /* ── Tabs: everything else, one widget type per row, no repeats ── */
    FDK_Widget *tabs = fdk_tabs();
    fdk_widget_set_size(tabs, FDK_SIZE_FILL, FDK_SIZE_FILL);

    FDK_Widget *p1 = fdk_tabs_add_page(tabs, "Widgets");
    FDK_Widget *p2 = fdk_tabs_add_page(tabs, "Animation & Toasts");
    FDK_Widget *p3 = fdk_tabs_add_page(tabs, "About");
    build_tab_widgets(p1);
    build_tab_animation(p2);
    build_tab_about(p3);

    fdk_container_add(body, tabs);

    /* ── Bottom row ── */
    FDK_Widget *row_bot = fdk_hbox(12, 0);
    fdk_widget_set_size(row_bot, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    fdk_container_add(row_bot, fdk_label(
        "[button.accent] [button.danger] [button.ghost] all from theme file"));

    FDK_Widget *spacer2 = fdk_label("");
    fdk_widget_set_size(spacer2, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_bot, spacer2);

    FDK_Widget *btn_quit = fdk_button("Quit");
    fdk_button_on_click(btn_quit, on_quit, &g_running);
    fdk_widget_set_tooltip(btn_quit, "Exit the application (also in File \xe2\x86\x92 Quit)");
    fdk_widget_set_size(btn_quit, 90, 36);
    fdk_container_add(row_bot, btn_quit);

    fdk_container_add(body, row_bot);
    fdk_container_add(root, body);

    return root;
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    bool use_auto_resolve = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0) use_auto_resolve = true;
        else snprintf(g_themes_dir, sizeof g_themes_dir, "%s", argv[i]);
    }

    /* app_name kept as a single clean token (no spaces) since it
     * doubles as the filename FDK looks for at
     * ~/.FDKthemes/overrides/<app_name> for tier-2 per-app overrides. */
    FDK_InitInfo init = {
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = "fdk-theme-switcher"
    };
    if (!fdk_init(&init)) return 1;

    FDK_WindowDesc wd = {
        .title     = "FDK v0.1.0 \xe2\x80\x94 Theme Switcher",
        .x         = FDK_WINDOW_POS_CENTER,
        .y         = FDK_WINDOW_POS_CENTER,
        .w         = 760, .h = 640,
        .resizable = true,
        .csd       = true,
        .min_w     = 480, .min_h = 360
    };
    FDK_Window *win = fdk_window_create(&wd);
    if (!win) { fdk_shutdown(); return 1; }
    fdk_window_show(win);

    FDK_Theme theme;
    char auto_resolved_path[512] = {0};
    if (use_auto_resolve) {
        /* --auto demonstrates the zero-code path: fdk_theme_resolve_ex()
         * is the exact same three-tier logic (developer force -> user
         * per-app override at ~/.FDKthemes/overrides/fdk-theme-switcher
         * -> system-wide ~/.FDKthemes/theme.fdktheme) that
         * fdk_ui_create(win, NULL) would run internally — the _ex
         * variant additionally reports which file backed the result,
         * which this demo reuses below to start its own hot-reload
         * watch. A real app that doesn't need either the font fallback
         * or the resolved-path string could skip all of this and just
         * call fdk_ui_create(win, NULL) directly — that one call alone
         * gets the same resolution AND automatic hot-reload watching,
         * with zero other code required. */
        theme = fdk_theme_resolve_ex("fdk-theme-switcher",
                                      auto_resolved_path, sizeof auto_resolved_path);
        fprintf(stderr, "[theme-switcher] --auto: resolved via three-tier system "
                        "(see ~/.FDKthemes)\n");
    } else {
        /* Default: explicit load from the bundled themes/ directory,
         * so the demo also proves fdk_theme_load() end-to-end on its
         * own bundled files regardless of what (if anything) exists
         * under ~/.FDKthemes on the machine running it. */
        theme = fdk_theme_faded_dream();
        char init_path[512];
        snprintf(init_path, sizeof init_path, "%s/faded-dream.fdktheme", g_themes_dir);
        if (!fdk_theme_load(&theme, init_path))
            fprintf(stderr, "[theme-switcher] warning: %s not found, using C defaults\n",
                    init_path);
    }

    if (!theme.font_body) {
        for (int i = 0; font_paths[i] && !theme.font_body; i++)
            theme.font_body = fdk_font_load(font_paths[i], 15);
        g_font = theme.font_body;   /* tracked for fdk_font_destroy() at exit --
                                     * only when WE loaded it via the fallback
                                     * scan above, not when a theme file already
                                     * specified its own font */
    }
    if (!theme.font_label) theme.font_label = theme.font_body;
    if (!theme.font_mono)  theme.font_mono  = theme.font_body;

    /* Passing the resolved-and-font-patched theme explicitly here (for
     * both branches) means fdk_ui_create() won't auto-start a watch of
     * its own — it only does that when theme == NULL, since an
     * explicitly-supplied theme is itself a form of opting out of the
     * automatic system. So in --auto mode we start the watch
     * ourselves, pointed at the same file fdk_theme_resolve() actually
     * used, to keep the demo's hot-reload behaviour identical either
     * way. A real app that just wants the fully automatic behaviour —
     * including auto-watch — should call fdk_ui_create(win, NULL)
     * directly instead of pre-resolving like this demo does. */
    g_ui = fdk_ui_create(win, &theme);
    if (use_auto_resolve && auto_resolved_path[0])
        fdk_theme_watch(g_ui, NULL, auto_resolved_path);
    g_root = build_ui();

    /* Wait for first EXPOSE/RESIZE for real window dimensions */
    FDK_Event ev;
    bool got_size = false;
    while (!got_size) {
        fdk_wait_event(&ev);
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) goto done;
        if (ev.type == FDK_EVENT_RESIZE || ev.type == FDK_EVENT_EXPOSE) {
            got_size = true;
            fdk_ui_layout(g_ui, g_root);
            fdk_ui_paint(g_ui, g_root);
        }
    }

    fdk_notify(g_ui, "Pick a theme on the left to switch live", FDK_NOTIFY_INFO, 3000);

    while (g_running) {
        bool got = fdk_wait_event_timeout(&ev, 16);
        if (!got) ev.type = FDK_EVENT_NONE;
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) break;
        if (ev.type == FDK_EVENT_KEY_DOWN) {
            if (ev.key.key == FDK_KEY_ESCAPE) break;
            /* Global Ctrl+Q / Ctrl+W to quit, matching the menu's shortcut */
            if ((ev.key.mods & FDK_MOD_CTRL) &&
                (ev.key.key == 'q' || ev.key.key == 'w')) break;
        }
        fdk_ui_step(g_ui, g_root, &ev);
        /* Check CLOSE again AFTER fdk_ui_step — catches titlebar close button */
        if (ev.type == FDK_EVENT_CLOSE) break;
    }

done:
    fdk_theme_watch(g_ui, g_root, NULL);  /* stop any active watch thread */
    fdk_ui_destroy(g_ui);
    fdk_widget_destroy(g_root);
    if (g_font) fdk_font_destroy(g_font);
    fdk_window_destroy(win);
    fdk_shutdown();
    return 0;
}
