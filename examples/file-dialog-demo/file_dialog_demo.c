/*
 * file_dialog_demo.c — minimal FDK app showing the built-in file dialog
 *
 * Creates a window with an "Open File" button. Clicking it opens FDK's
 * built-in modal file picker (no D-Bus, no XDG portal, no external deps).
 * The selected path is displayed in a label below the button.
 *
 * Run:
 *   ./fdk_file_dialog_demo
 */
#define _POSIX_C_SOURCE 200809L

#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FDK_Widget *g_result_label = NULL;
static FDK_Window *g_win = NULL;

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

static void on_open_button(FDK_Widget *w, void *ud)
{
    (void)w; (void)ud;
    /* Show the file dialog — this BLOCKS until the user picks or cancels */
    FDK_FileDialogDesc desc = {
        .title       = "Open File",
        .initial_dir = NULL,      /* current working directory */
        .filter      = "*.txt;*.md;*.c;*.h;*.lua;*.sh",
    };
    char *path = fdk_file_dialog(g_win, &desc);

    if (path) {
        printf("Selected: %s\n", path);
        /* Update the label to show the selected path */
        fdk_label_set_text(g_result_label, path);
        free(path);
    } else {
        printf("Cancelled\n");
        fdk_label_set_text(g_result_label, "(cancelled)");
    }
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
        .app_name = "fdk-file-dialog-demo",
    })) {
        fprintf(stderr, "fdk_init failed\n");
        return 1;
    }

    g_win = fdk_window_create(&(FDK_WindowDesc){
        .title     = "File Dialog Demo",
        .x = FDK_WINDOW_POS_CENTER, .y = FDK_WINDOW_POS_CENTER,
        .w = 500, .h = 300,
        .resizable = true,
        .render    = FDK_RENDER_SOFTWARE,
        .csd       = true,
        .min_w     = 360, .min_h = 280,
    });
    if (!g_win) { fprintf(stderr, "window_create failed\n"); return 1; }
    fdk_window_show(g_win);

    FDK_UI *ui = fdk_ui_create(g_win, &theme);
    if (!ui) { fprintf(stderr, "ui_create failed\n"); return 1; }

    /* Build UI */
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    FDK_Widget *tb = fdk_titlebar("File Dialog Demo");
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);

    /* Body container with padding for content */
    FDK_Widget *body = fdk_vbox(20, 24);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, body);

    fdk_container_add(body, fdk_label("File Dialog Demo"));
    fdk_container_add(body, fdk_label("Click the button to open a file picker."));

    FDK_Widget *btn = fdk_button("Open File...");
    fdk_widget_set_size(btn, 160, 36);
    fdk_button_on_click(btn, on_open_button, NULL);
    fdk_container_add(body, btn);

    fdk_container_add(body, fdk_separator());

    fdk_container_add(body, fdk_label("Selected file:"));
    g_result_label = fdk_label("(none yet)");
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
    fdk_window_destroy(g_win);
    fdk_shutdown();
    if (font) fdk_font_destroy(font);
    return 0;
}
