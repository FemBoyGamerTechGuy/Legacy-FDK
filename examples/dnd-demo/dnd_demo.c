/*
 * dnd_demo.c — drag-and-drop demo
 *
 * Register a drop handler on the window, then drag files from your
 * file manager into the window. The dropped file paths are displayed
 * in a label.
 *
 * Run:
 *   ./fdk_dnd_demo
 *
 * Then open your file manager (Thunar, PCManFM, Dolphin, etc.) and
 * drag a file into the FDK window.
 */
#define _POSIX_C_SOURCE 200809L

#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FDK_Widget *g_result_label = NULL;

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

/* Called when files are dragged into the window and dropped.
 * uri_list is a newline-separated list of "file:///path/to/file" URIs. */
static void on_drop(FDK_Window *win, const char *uri_list, void *ud)
{
    (void)win; (void)ud;
    printf("=== Drop received ===\n%s\n", uri_list);

    /* Build a display string from the URI list */
    char display[1024] = "Dropped:\n";
    const char *p = uri_list;
    while (*p && strlen(display) < sizeof(display) - 100) {
        /* Parse "file:///path" — skip the "file://" prefix */
        if (strncmp(p, "file://", 7) == 0) p += 7;
        /* Copy until newline or end */
        char line[512] = {0};
        int i = 0;
        while (*p && *p != '\n' && *p != '\r' && i < 511) {
            line[i++] = *p++;
        }
        line[i] = '\0';
        if (i > 0) {
            strcat(display, "  ");
            strncat(display, line, sizeof(display) - strlen(display) - 2);
            strcat(display, "\n");
        }
        /* Skip newline */
        while (*p == '\n' || *p == '\r') p++;
    }
    fdk_label_set_text(g_result_label, display);
}

int main(void)
{
    FDK_Font *font = NULL;
    for (int i = 0; font_paths[i] && !font; i++)
        font = fdk_font_load(font_paths[i], 15);

    FDK_Theme theme = fdk_theme_faded_dream();
    if (font) {
        theme.font_body  = font;
        theme.font_label = font;
        theme.font_mono  = font;
    }

    if (!fdk_init(&(FDK_InitInfo){
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = "fdk-dnd-demo",
    })) {
        fprintf(stderr, "fdk_init failed\n");
        return 1;
    }

    FDK_Window *win = fdk_window_create(&(FDK_WindowDesc){
        .title     = "Drag and Drop Demo",
        .x = FDK_WINDOW_POS_CENTER, .y = FDK_WINDOW_POS_CENTER,
        .w = 500, .h = 400,
        .resizable = true,
        .render    = FDK_RENDER_SOFTWARE,
        .csd       = true,
        .min_w     = 360, .min_h = 280,
    });
    if (!win) { fprintf(stderr, "window_create failed\n"); return 1; }
    fdk_window_show(win);

    /* Register the drop handler — this enables XDND on X11 */
    fdk_window_set_drop_handler(win, on_drop, NULL);

    FDK_UI *ui = fdk_ui_create(win, &theme);
    if (!ui) { fprintf(stderr, "ui_create failed\n"); return 1; }

    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    FDK_Widget *tb = fdk_titlebar("Drag and Drop Demo");
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);

    /* Body container with padding for content */
    FDK_Widget *body = fdk_vbox(20, 24);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, body);

    fdk_container_add(body, fdk_label("Drag and Drop Demo"));
    fdk_container_add(body, fdk_label("Drag files from your file manager"));
    fdk_container_add(body, fdk_label("into this window."));
    fdk_container_add(body, fdk_separator());
    g_result_label = fdk_label("(drop files here)");
    fdk_container_add(body, g_result_label);
    fdk_container_add(body, fdk_label(""));
    fdk_container_add(body, fdk_label("Press ESC to quit."));

    /* Wait for first EXPOSE */
    FDK_Event ev;
    bool got_size = false;
    while (!got_size) {
        fdk_wait_event(&ev);
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) goto done;
        if (ev.type == FDK_EVENT_RESIZE || ev.type == FDK_EVENT_EXPOSE) {
            got_size = true;
            fdk_ui_layout(ui, root);
            fdk_ui_paint(ui, root);
        }
    }

    /* Main loop */
    for (;;) {
        bool got = fdk_wait_event_timeout(&ev, 16);
        if (!got) ev.type = FDK_EVENT_NONE;
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) break;
        if (ev.type == FDK_EVENT_KEY_DOWN && ev.key.key == FDK_KEY_ESCAPE) break;
        fdk_ui_step(ui, root, &ev);
        if (ev.type == FDK_EVENT_CLOSE) break;
    }

done:
    fdk_ui_destroy(ui);
    fdk_widget_destroy(root);
    fdk_window_destroy(win);
    fdk_shutdown();
    if (font) fdk_font_destroy(font);
    return 0;
}
