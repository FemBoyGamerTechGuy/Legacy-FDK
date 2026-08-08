/*
 * csd_demo.c — minimal FDK app demonstrating client-side decorations
 *
 * Creates a CSD-enabled window (no server-side titlebar/border when the
 * WM/compositor honors the request), with an FDK titlebar widget at the
 * top. The titlebar's minimize/maximize/close buttons are wired up
 * automatically by the widget layer — this demo just declares the
 * widget tree.
 *
 * The empty area between the title text and the buttons is:
 *   - single-click-drag → interactive move (driven by compositor/WM)
 *   - double-click       → toggle maximized state
 *
 * Run on X11:
 *   ./fdk_csd_demo
 * Run on Xephyr (for testing without your real session):
 *   Xephyr -screen 1280x720 :1 &
 *   DISPLAY=:1 ./fdk_csd_demo
 * Run on Wayland:
 *   ./fdk_csd_demo
 */
#define _POSIX_C_SOURCE 200809L

#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* access() */

/* Font fallback paths for environments without fontconfig — covers
 * Debian/Ubuntu (DejaVu), Fedora (DejaVu at different path), Arch
 * (TTF subdir), and Noto on systems that prefer it. */
static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

int main(void)
{
    /* Try a few font paths so the demo works on most distros */
    FDK_Font *font = NULL;
    for (int i = 0; font_paths[i] && !font; i++)
        font = fdk_font_load(font_paths[i], 15);

    FDK_Theme theme = fdk_theme_faded_dream();
    if (font) {
        theme.font_body  = font;
        theme.font_label = font;
        theme.font_mono  = font;
    }

    /* FDK_PLATFORM_AUTO lets FDK pick Wayland when WAYLAND_DISPLAY is set
     * and X11 when DISPLAY is set — so the same binary works on both. */
    if (!fdk_init(&(FDK_InitInfo){
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = "fdk-csd-demo",
    })) {
        fprintf(stderr, "fdk_init failed\n");
        return 1;
    }

    /* CSD opt-in: ask the WM/compositor to suppress its own decorations.
     * On X11 this sets _MOTIF_WM_HINTS with decorations=0; on Wayland
     * this is currently a no-op pending zxdg_decoration_manager_v1
     * support (see ROADMAP.md CSD piece 4). The titlebar widget below
     * works either way — on a Wayland compositor that ignores CSD
     * negotiation you may see both the compositor's titlebar and FDK's. */
    FDK_Window *win = fdk_window_create(&(FDK_WindowDesc){
        .title     = "CSD Demo",
        .x = FDK_WINDOW_POS_CENTER, .y = FDK_WINDOW_POS_CENTER,
        .w = 480, .h = 320,
        .resizable = true,
        .render    = FDK_RENDER_SOFTWARE,
        .csd       = true,
        .min_w     = 320, .min_h = 280,
    });
    if (!win) { fprintf(stderr, "window_create failed\n"); return 1; }
    fdk_window_show(win);

    /* Set the window icon.
     * Try the bundled PNG file first (works on X11 via stb_image).
     * Then try the icon-name approach (works on Wayland via
     * xdg-toplevel-icon-v1 protocol).
     * Both calls are safe — if the file/protocol isn't available,
     * they're no-ops and the WM uses its default icon. */
    {
        /* Try absolute path relative to the executable first, then
         * relative path, then give up. */
        const char *paths[] = {
            "fdk-icon-256.png",
            "./fdk-icon-256.png",
            NULL
        };
        for (int i = 0; paths[i]; i++) {
            if (access(paths[i], R_OK) == 0) {
                fdk_window_set_icon_from_file(win, paths[i]);
                break;
            }
        }
        /* Also set by name for Wayland (compositor looks up in icon theme) */
        fdk_window_set_icon_name(win, "text-editor");
    }

    FDK_UI *ui = fdk_ui_create(win, &theme);
    if (!ui) { fprintf(stderr, "ui_create failed\n"); return 1; }

    /* Root vbox: titlebar on top, content below */
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    /* Titlebar — buttons auto-wired to minimize/maximize/close.
     * Clicks on the button glyphs fire the corresponding window-state
     * action; clicks on the empty titlebar area begin an interactive
     * move driven by the WM/compositor. */
    FDK_Widget *tb = fdk_titlebar("CSD Demo — drag me to move");
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);

    /* Body */
    FDK_Widget *body = fdk_vbox(16, 24);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, body);

    fdk_container_add(body, fdk_label("Client-side decorations demo"));
    fdk_container_add(body, fdk_label(""));
    fdk_container_add(body, fdk_label("Try the titlebar buttons:"));
    fdk_container_add(body, fdk_label("  close (X)     — exits the app"));
    fdk_container_add(body, fdk_label("  maximize ([]) — toggles maximized"));
    fdk_container_add(body, fdk_label("  minimize (_)  — iconifies"));
    fdk_container_add(body, fdk_label(""));
    fdk_container_add(body, fdk_label("Drag the empty titlebar area to move."));
    fdk_container_add(body, fdk_label("Press ESC to quit."));

    /* ── Wait for first EXPOSE/RESIZE before painting ──
     * Without a window manager (e.g. bare Xephyr), the compositor may
     * not send an EXPOSE event immediately, so we have to wait for one
     * explicitly and do the initial layout+paint ourselves. This matches
     * the pattern in systemwide_test.c. Skipping this is what produced
     * the "empty window" symptom on Xephyr. */
    FDK_Event ev;
    bool got_size = false;
    while (!got_size) {
        fdk_wait_event(&ev);
        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) {
            fdk_ui_destroy(ui);
            fdk_window_destroy(win);
            fdk_shutdown();
            if (font) fdk_font_destroy(font);
            return 0;
        }
        if (ev.type == FDK_EVENT_RESIZE || ev.type == FDK_EVENT_EXPOSE) {
            got_size = true;
            fdk_ui_layout(ui, root);
            fdk_ui_paint(ui, root);
        }
    }

    /* Main event loop.
     *
     * The CLOSE check is done BOTH before AND after fdk_ui_step():
     *   - BEFORE: catches CLOSE events sent directly by the WM
     *     (e.g. WM_DELETE_WINDOW from the window's close button if the
     *     WM draws its own titlebar, or Alt+F4)
     *   - AFTER: catches CLOSE events synthesized by the widget layer
     *     when the user clicks the FDK titlebar's close button. The
     *     widget layer can't push events to the platform queue, so it
     *     sets ui->pending_close and fdk_ui_step() converts the
     *     in-flight MOUSE_UP event to CLOSE. The conversion happens
     *     inside fdk_ui_step(), so the check must run AFTER it returns
     *     — otherwise the next fdk_wait_event_timeout() call
     *     overwrites ev and the CLOSE is lost.
     *
     * [BUGFIX v4] Previously this loop only checked CLOSE before
     * fdk_ui_step, so the titlebar close button armed
     * pending_close on MOUSE_UP, fdk_ui_step converted the event to
     * CLOSE, but then the loop iterated and called
     * fdk_wait_event_timeout which overwrote ev before the CLOSE
     * check ran. The app never exited even though the button click
     * was correctly detected. */
    for (;;) {
        bool got = fdk_wait_event_timeout(&ev, 16);
        if (!got) ev.type = FDK_EVENT_NONE;

        if (ev.type == FDK_EVENT_QUIT) break;
        if (ev.type == FDK_EVENT_CLOSE) break;
        if (ev.type == FDK_EVENT_KEY_DOWN && ev.key.key == FDK_KEY_ESCAPE) break;

        fdk_ui_step(ui, root, &ev);

        /* Check CLOSE again AFTER fdk_ui_step — this is what catches
         * the titlebar close button click. The widget layer sets
         * ui->pending_close on the close-button click, and fdk_ui_step
         * converts the in-flight event to FDK_EVENT_CLOSE on return. */
        if (ev.type == FDK_EVENT_CLOSE) break;
    }

    fdk_ui_destroy(ui);
    fdk_widget_destroy(root);
    fdk_window_destroy(win);
    fdk_shutdown();
    if (font) fdk_font_destroy(font);
    return 0;
}
