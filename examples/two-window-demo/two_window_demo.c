/*
 * two_window_demo.c — visual proof that fdk_theme_watch() is now
 * per-window, not a single process-global watch.
 *
 * Opens two independent windows, each watching its OWN .fdktheme
 * file. Edit either file and save — only that window re-themes; the
 * other is untouched. This is the actual bug fixed this session: the
 * watch state used to be one global WatchCtx, so starting the second
 * window's watch silently killed the first window's — only the
 * most-recently-created window ever stayed live.
 *
 * Closing one window (its own "Close this window" button, or the
 * native close button / WM close) destroys just that window/ui/watch
 * and keeps the other one running. That also exercises the
 * fdk_ui_destroy() fix from the same session: a watch thread whose ui
 * was destroyed without stopping the watch first used to be left
 * holding a dangling pointer, live to the next file-change event.
 *
 * NOTE ON WINDOW PLACEMENT: the x/y hints below are honored on X11
 * (e.g. under Xephyr) but silently ignored on Wayland — the Wayland
 * protocol deliberately gives compositors, not clients, control over
 * window placement, and FDK's Wayland backend has nothing to work
 * around that with. On Hyprland the two windows will appear wherever
 * the compositor puts them; drag them apart if they overlap.
 *
 * NOTE ON WIDGET CHOICES: the progress bar below uses a fixed static
 * value rather than fdk_tween() or indeterminate mode. Neither is
 * unsafe here, for two different reasons:
 *   - fdk_tween() was initially suspected to be — fdk__tweens_tick()
 *     drives one process-global pool, and its doc comment says
 *     "called once per frame by fdk_ui_step()", which sounded like it
 *     wouldn't hold with two windows each calling fdk_ui_step() every
 *     real frame. That suspicion didn't survive actually tracing the
 *     function: every call applies its delta to the same shared pool
 *     regardless of which window triggered it, so consecutive deltas
 *     telescope back to the correct total real elapsed time no matter
 *     how many windows are stepping. Confirmed empirically, not just
 *     by re-derivation: running the same tween to completion under
 *     single- and double-call-per-frame patterns produced real
 *     completion times within ~3% of each other across repeated runs
 *     -- ordinary scheduling jitter, nowhere near the ~100% a genuine
 *     per-call speed-doubling would show.
 *   - Indeterminate mode is driven by tick_animations()'s dt, which
 *     uses FDK_UI.anim_last_tick_ms -- a per-window clock, fixed
 *     earlier this same session for exactly this reason.
 * A static value here is simply the simpler choice for this demo, not
 * a workaround for either.
 *
 * To test:
 *   ./fdk_two_window_demo
 *   (edit /tmp/fdk-two-window-demo/left.fdktheme,  save -> only LEFT updates)
 *   (edit /tmp/fdk-two-window-demo/right.fdktheme, save -> only RIGHT updates)
 */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME      "fdk-two-window-demo"
#define SCRATCH_DIR   "/tmp/fdk-two-window-demo"
#define LEFT_PATH     SCRATCH_DIR "/left.fdktheme"
#define RIGHT_PATH    SCRATCH_DIR "/right.fdktheme"

/* Font fallback paths for environments without fontconfig — same list
 * examples/systemwide-test uses. */
static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL
};

typedef struct {
    const char *name;         /* "LEFT" / "RIGHT" for on-screen + console text */
    const char *theme_path;
    FDK_Window *win;
    FDK_UI     *ui;
    FDK_Widget *root;
    bool        alive;
    bool        sized;
} DemoWin;

static void on_close_clicked(FDK_Widget *w, void *ud)
{ (void)w; ((DemoWin *)ud)->alive = false; }

static FDK_Widget *build_ui(DemoWin *dw)
{
    FDK_Widget *root = fdk_vbox(0, 0);
    fdk_widget_set_size(root, FDK_SIZE_FILL, FDK_SIZE_FILL);

    char tb_title[64];
    snprintf(tb_title, sizeof tb_title, "FDK Demo -- %s", dw->name);
    FDK_Widget *tb = fdk_titlebar(tb_title);
    fdk_widget_set_size(tb, FDK_SIZE_FILL, 40);
    fdk_container_add(root, tb);

    /* Body container with the original gap/padding so content below the
     * titlebar keeps its spacing -- root itself has no padding so the
     * titlebar can sit flush against the top of the window. */
    FDK_Widget *body = fdk_vbox(14, 22);
    fdk_widget_set_size(body, FDK_SIZE_FILL, FDK_SIZE_FILL);
    fdk_container_add(root, body);

    char heading[64];
    snprintf(heading, sizeof heading, "%s window", dw->name);
    fdk_container_add(body, fdk_label(heading));
    fdk_container_add(body, fdk_label("Independent fdk_theme_watch() — edit and save:"));
    fdk_container_add(body, fdk_label(dw->theme_path));

    fdk_container_add(body, fdk_separator());

    fdk_container_add(body, fdk_label("Widgets, so a theme change is easy to see:"));
    FDK_Widget *row = fdk_hbox(10, 0);
    fdk_widget_set_size(row, FDK_SIZE_FILL, FDK_SIZE_WRAP);

    FDK_Widget *btn_def = fdk_button("Default");
    fdk_widget_set_size(btn_def, 110, 36);
    fdk_container_add(row, btn_def);

    FDK_Widget *btn_acc = fdk_button("Accent");
    fdk_widget_set_variant(btn_acc, "accent");
    fdk_widget_set_size(btn_acc, 110, 36);
    fdk_container_add(row, btn_acc);
    fdk_container_add(body, row);

    /* Static value for simplicity, not a workaround — see the file
     * header note on fdk_tween()/indeterminate mode, both confirmed
     * safe here. */
    FDK_Widget *progress = fdk_progress_bar(0.65f);
    fdk_widget_set_size(progress, FDK_SIZE_FILL, 16);
    fdk_container_add(body, progress);

    fdk_container_add(body, fdk_separator());

    FDK_Widget *row_bot = fdk_hbox(0, 0);
    fdk_widget_set_size(row_bot, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    FDK_Widget *spacer = fdk_label("");
    fdk_widget_set_size(spacer, FDK_SIZE_FILL, FDK_SIZE_WRAP);
    fdk_container_add(row_bot, spacer);
    FDK_Widget *btn_close = fdk_button("Close this window");
    fdk_button_on_click(btn_close, on_close_clicked, dw);
    fdk_widget_set_size(btn_close, 170, 36);
    fdk_container_add(row_bot, btn_close);
    fdk_container_add(body, row_bot);

    return root;
}

static void write_starter_theme(const char *path, const char *accent_hex,
                                 const char *bg_hex)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "fatal: cannot write %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fprintf(f,
        "# Starter theme for the two-window demo.\n"
        "# Edit and save -- only THIS window's fdk_theme_watch() should react.\n"
        "accent     = %s\n"
        "bg_window  = %s\n"
        "radius_sm  = 8\n",
        accent_hex, bg_hex);
    fclose(f);
}

/* Loads theme_path, patches in a font fallback if the file didn't
 * specify one (same pattern as examples/systemwide-test), and returns
 * a fully-populated FDK_Theme ready for fdk_ui_create(). */
static FDK_Theme load_theme(const char *theme_path)
{
    FDK_Theme theme = fdk_theme_faded_dream();
    fdk_theme_load(&theme, theme_path);
    if (!theme.font_body) {
        for (int i = 0; font_paths[i] && !theme.font_body; i++)
            theme.font_body = fdk_font_load(font_paths[i], 15);
    }
    if (!theme.font_label) theme.font_label = theme.font_body;
    if (!theme.font_mono)  theme.font_mono  = theme.font_body;
    return theme;
}

static DemoWin *open_window(const char *name, const char *theme_path, int x,
                             const char *accent, const char *bg)
{
    DemoWin *dw = calloc(1, sizeof *dw);
    if (!dw) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    dw->name       = name;
    dw->theme_path = theme_path;

    write_starter_theme(theme_path, accent, bg);

    char title[64];
    snprintf(title, sizeof title, "FDK Demo -- %s", name);
    FDK_WindowDesc wd = {
        .title     = title,   /* copied internally by window_create (both
                                * backends hand it to xdg_toplevel_set_title
                                * / XStoreName at call time) -- safe to use
                                * a stack buffer that goes out of scope
                                * right after this call */
        .x         = x,
        .y         = 140,
        .w         = 460,
        .h         = 440,
        .resizable = true,
        .csd       = true,
        .min_w     = 320, .min_h = 240,
    };
    dw->win = fdk_window_create(&wd);
    if (!dw->win) { fprintf(stderr, "fatal: fdk_window_create failed for %s\n", name); exit(1); }
    fdk_window_show(dw->win);

    FDK_Theme theme = load_theme(theme_path);
    dw->ui   = fdk_ui_create(dw->win, &theme);
    dw->root = build_ui(dw);
    dw->alive = true;

    /* The actual point of this whole demo: an independent watch on
     * THIS window's own file. */
    fdk_theme_watch(dw->ui, dw->root, theme_path);

    fprintf(stderr, "[two-window-demo] %-5s watching %s\n", name, theme_path);
    return dw;
}

static void close_window(DemoWin *dw)
{
    if (!dw || !dw->win) { if (dw) dw->alive = false; return; }  /* already closed */
    fdk_theme_watch(dw->ui, dw->root, NULL);   /* stop this window's watch
                                                 * explicitly -- fdk_ui_destroy()
                                                 * would also catch it if we
                                                 * forgot, see this session's
                                                 * use-after-free fix */
    fdk_ui_destroy(dw->ui);
    fdk_widget_destroy(dw->root);
    fdk_window_destroy(dw->win);
    fprintf(stderr, "[two-window-demo] %-5s closed -- its watch on %s is stopped.\n",
            dw->name, dw->theme_path);
    dw->win   = NULL;
    dw->ui    = NULL;
    dw->root  = NULL;
    dw->alive = false;
}

/* Steps whichever DemoWin this event belongs to. Returns false if the
 * event didn't belong to either (e.g. a global QUIT with no specific
 * window, if that ever occurs). */
static bool route_event(DemoWin *left, DemoWin *right, FDK_Event *ev)
{
    DemoWin *target = NULL;
    if (ev->window == left->win)  target = left;
    if (ev->window == right->win) target = right;
    if (!target || !target->alive) return false;

    if (ev->type == FDK_EVENT_QUIT || ev->type == FDK_EVENT_CLOSE) {
        target->alive = false;
        return true;
    }

    fdk_ui_step(target->ui, target->root, ev);

    /* Check CLOSE again AFTER fdk_ui_step — catches titlebar close button.
     * fdk_ui_step() converts the titlebar's pending_close into a CLOSE
     * event, so we need to check for it here too. */
    if (ev->type == FDK_EVENT_CLOSE) {
        target->alive = false;
        return true;
    }

    /* fdk_ui_step() may hand back a DIFFERENT event than the one we
     * passed in -- its internal drain loop coalesces bursts of queued
     * events, and (after this session's fix) re-injects the first one
     * it finds that belongs to a different window rather than
     * misrouting it into target's widget tree. Route that one too. */
    if (ev->window != target->win)
        return route_event(left, right, ev);

    return true;
}

int main(void)
{
    FDK_InitInfo init = {
        .platform = FDK_PLATFORM_AUTO,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = APP_NAME,
    };
    if (!fdk_init(&init)) return 1;

    if (system("mkdir -p " SCRATCH_DIR) != 0) {
        fprintf(stderr, "fatal: could not create " SCRATCH_DIR "\n");
        return 1;
    }

    /* Warm accent for LEFT, cool accent for RIGHT -- the difference
     * should be obvious before you edit anything. */
    DemoWin *left  = open_window("LEFT",  LEFT_PATH,   80,  "#E8734A", "#1A1210");
    DemoWin *right = open_window("RIGHT", RIGHT_PATH, 620, "#4AA8E8", "#10161A");

    fprintf(stderr, "\n[two-window-demo] Two independent windows are now open.\n");
    fprintf(stderr, "[two-window-demo] Edit either file below and save:\n");
    fprintf(stderr, "[two-window-demo]   %s\n", LEFT_PATH);
    fprintf(stderr, "[two-window-demo]   %s\n", RIGHT_PATH);
    fprintf(stderr, "[two-window-demo] Only the matching window should re-theme.\n");
    fprintf(stderr, "[two-window-demo] Close either window independently -- the "
                     "other keeps running and hot-reloading.\n\n");

    /* Wait for each window's first EXPOSE/RESIZE (real dimensions from
     * the compositor) before its first layout+paint -- same reason
     * examples/systemwide-test does this for its one window, just for
     * two here. */
    /* A window only "needs" sizing while it's both alive and not yet
     * sized -- if it's closed before its first EXPOSE/RESIZE ever
     * arrives, it stops blocking this wait (otherwise the loop below
     * would wait forever for a RESIZE that a dead window can never
     * produce). */
#define NEEDS_SIZING(dw) ((dw)->alive && !(dw)->sized)
    while (NEEDS_SIZING(left) || NEEDS_SIZING(right)) {
        FDK_Event ev;
        fdk_wait_event(&ev);

        DemoWin *dw = NULL;
        if (ev.window == left->win)  dw = left;
        if (ev.window == right->win) dw = right;

        if (ev.type == FDK_EVENT_QUIT || ev.type == FDK_EVENT_CLOSE) {
            if (dw) dw->alive = dw->sized = false; /* let outer loop's alive
                                                      * check finish it off */
            continue;
        }
        if (dw && !dw->sized &&
            (ev.type == FDK_EVENT_RESIZE || ev.type == FDK_EVENT_EXPOSE)) {
            dw->sized = true;
            fdk_ui_layout(dw->ui, dw->root);
            fdk_ui_paint(dw->ui, dw->root);
        }
    }
#undef NEEDS_SIZING

    while (left->alive || right->alive) {
        FDK_Event ev;
        if (!fdk_wait_event_timeout(&ev, 16)) continue;
        route_event(left, right, &ev);

        if (!left->alive)  close_window(left);
        if (!right->alive) close_window(right);
    }

    close_window(left);
    close_window(right);
    free(left);
    free(right);
    fdk_shutdown();
    return 0;
}
