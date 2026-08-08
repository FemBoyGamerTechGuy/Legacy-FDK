/*
 * systemwide_test.c — minimal FDK app to verify systemwide theming works
 *
 * This app does exactly what a real FDK app should do:
 *   1. fdk_init() with an app_name
 *   2. fdk_ui_create(win, NULL) — zero theme code, auto-resolves from
 *      ~/.config/FDK/fdk.conf -> ~/.FDKthemes/<name>.fdktheme
 *   3. fdk_theme_watch_conf() — re-themes live whenever fdk-theme set
 *      <name> is run in another terminal
 *
 * To test:
 *   Terminal 1: ./fdk_systemwide_test
 *   Terminal 2: fdk-theme set void       <- UI re-themes within ~1 sec
 *               fdk-theme set rose       <- switches again
 *               fdk-theme set faded-dream
 */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <string.h>

#define APP_NAME "fdk-systemwide-test"

static bool        g_running = true;
static FDK_UI     *g_ui      = NULL;
static FDK_Widget *g_root    = NULL;

/* Font fallback paths for environments without fontconfig */
static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

static void on_quit(FDK_Widget *w, void *ud)
{ (void)w; *(bool*)ud = false; }

static FDK_Widget *build_ui(void)
{
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    FDK_Widget *tb = fdk_titlebar("FDK Systemwide Theme Test");
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);

    /* Body container with padding for content */
    FDK_Widget *body = fdk_vbox(16, 24);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, body);

    fdk_container_add(body, fdk_label(
        "FDK Systemwide Theme Test"));
    fdk_container_add(body, fdk_label(
        "This app auto-resolved its theme from ~/.config/FDK/fdk.conf"));
    fdk_container_add(body, fdk_label(
        "Run  fdk-theme set <name>  in another terminal to switch live."));

    fdk_container_add(body, fdk_separator());

    /* A row of button variants so color changes are immediately obvious */
    fdk_container_add(body, fdk_label("Button variants:"));
    FDK_Widget *row = fdk_hbox(10, 0);
    fdk_widget_set_size(row, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    FDK_Widget *btn_def = fdk_button("Default");
    fdk_widget_set_size(btn_def, 120, 36);
    fdk_container_add(row, btn_def);

    FDK_Widget *btn_acc = fdk_button("Accent");
    fdk_widget_set_variant(btn_acc, "accent");
    fdk_widget_set_size(btn_acc, 120, 36);
    fdk_container_add(row, btn_acc);

    FDK_Widget *btn_dan = fdk_button("Danger");
    fdk_widget_set_variant(btn_dan, "danger");
    fdk_widget_set_size(btn_dan, 120, 36);
    fdk_container_add(row, btn_dan);

    FDK_Widget *btn_gho = fdk_button("Ghost");
    fdk_widget_set_variant(btn_gho, "ghost");
    fdk_widget_set_size(btn_gho, 120, 36);
    fdk_container_add(row, btn_gho);

    fdk_container_add(body, row);
    fdk_container_add(body, fdk_separator());

    /* A slider and progress bar to show accent color on other widgets */
    fdk_container_add(body, fdk_label("Other accented widgets:"));
    FDK_Widget *slider = fdk_slider(0.f, 100.f, 60.f);
    fdk_widget_set_size(slider, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(body, slider);

    FDK_Widget *progress = fdk_progress_bar(0.7f);
    fdk_widget_set_size(progress, FDK_SIZE_FILL, 18);
    fdk_container_add(body, progress);

    FDK_Widget *row2 = fdk_hbox(10, 0);
    fdk_widget_set_size(row2, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row2, fdk_toggle_button("Toggle", true));
    fdk_container_add(row2, fdk_checkbox("Checkbox", true));
    fdk_container_add(row2, fdk_badge("Badge"));
    fdk_container_add(body, row2);

    fdk_container_add(body, fdk_separator());

    /* Quit button at bottom */
    FDK_Widget *row_bot = fdk_hbox(0, 0);
    fdk_widget_set_size(row_bot, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    FDK_Widget *spacer = fdk_label("");
    fdk_widget_set_size(spacer, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_bot, spacer);
    FDK_Widget *btn_quit = fdk_button("Quit");
    fdk_button_on_click(btn_quit, on_quit, &g_running);
    fdk_widget_set_size(btn_quit, 90, 36);
    fdk_container_add(row_bot, btn_quit);
    fdk_container_add(body, row_bot);

    return root;
}

int main(void)
{
    FDK_InitInfo init = {
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = APP_NAME
    };
    if (!fdk_init(&init)) return 1;

    FDK_WindowDesc wd = {
        .title     = "FDK Systemwide Theme Test",
        .x         = FDK_WINDOW_POS_CENTER,
        .y         = FDK_WINDOW_POS_CENTER,
        .w         = 600, .h = 420,
        .resizable = true,
        .csd       = true,
        .min_w     = 400, .min_h = 320
    };
    FDK_Window *win = fdk_window_create(&wd);
    if (!win) { fdk_shutdown(); return 1; }
    fdk_window_show(win);

    /* Resolve the theme via the three-tier system (fdk.conf -> ~/.FDKthemes)
     * and patch in a font fallback if the theme file didn't specify one. */
    char resolved_path[512];
    FDK_Theme theme = fdk_theme_resolve_ex(APP_NAME, resolved_path,
                                            sizeof resolved_path);
    if (!theme.font_body) {
        for (int i = 0; font_paths[i] && !theme.font_body; i++)
            theme.font_body = fdk_font_load(font_paths[i], 15);
    }
    if (!theme.font_label) theme.font_label = theme.font_body;
    if (!theme.font_mono)  theme.font_mono  = theme.font_body;

    g_ui   = fdk_ui_create(win, &theme);
    g_root = build_ui();

    /* Watch fdk.conf for changes — when fdk-theme set <name> rewrites it,
     * the watch fires, re-resolves the new theme, and applies it live. */
    fdk_theme_watch_conf(g_ui, g_root, APP_NAME);

    fprintf(stderr, "[systemwide-test] theme resolved from: %s\n",
            resolved_path[0] ? resolved_path : "(built-in defaults)");
    fprintf(stderr, "[systemwide-test] watching ~/.config/FDK/fdk.conf\n");
    fprintf(stderr, "[systemwide-test] run 'fdk-theme set <name>' to switch live\n");

    /* Wait for first expose to get real window dimensions */
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

    while (g_running) {
        bool got = fdk_wait_event_timeout(&ev, 16);
        if (!got) ev.type = FDK_EVENT_NONE;
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) break;
        if (ev.type == FDK_EVENT_KEY_DOWN && ev.key.key == FDK_KEY_ESCAPE) break;
        fdk_ui_step(g_ui, g_root, &ev);
        /* Check CLOSE again AFTER fdk_ui_step — catches titlebar close button */
        if (ev.type == FDK_EVENT_CLOSE) break;
    }

done:
    fdk_theme_watch_conf(g_ui, g_root, NULL);  /* stop watch */
    fdk_ui_destroy(g_ui);
    fdk_widget_destroy(g_root);
    fdk_window_destroy(win);
    fdk_shutdown();
    return 0;
}
