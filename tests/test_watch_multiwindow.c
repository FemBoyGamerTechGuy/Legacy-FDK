/*
 * test_watch_multiwindow.c — Regression test for the per-UI theme
 * watch list.
 *
 * theme.c used to hold exactly one watch context in a process-global
 * static (g_watch). Starting a second window's watch silently tore
 * down the first window's watch — multi-window apps only ever kept
 * the most recently created window's watch alive. This test proves
 * that's fixed: two FDK_UI contexts can each hold a live, independent
 * inotify watch at the same time, changes to one theme file don't
 * leak into the other window, and fdk_ui_destroy() cleans up its own
 * watch rather than leaving a dangling ui pointer for the watch
 * thread to dereference later.
 *
 * No window, no compositor — runs anywhere libfdk.a links, same as
 * test_theme.c. Uses NULL for both FDK_UI contexts' window argument:
 * fdk_ui_create/fdk_theme_watch/fdk__ui_copy_theme/fdk_ui_destroy
 * never dereference it, only fdk_ui_set_theme() does (it calls
 * fdk_window_request_redraw(ui->win), which safely no-ops on NULL) --
 * so this stays fully headless without needing a real window at all.
 *
 * Exit code 0 = all checks passed, non-zero = a check failed.
 */
#define _POSIX_C_SOURCE 200809L  /* nanosleep */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include "core/test_internal.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) printf("  [PASS] %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

#define SCRATCH_DIR   "/tmp/fdk_watch_test"
#define PATH_A        SCRATCH_DIR "/a.fdktheme"
#define PATH_B        SCRATCH_DIR "/b.fdktheme"

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void write_theme_radius(const char *path, int radius_sm)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "fatal: cannot write %s\n", path); exit(1); }
    fprintf(f, "radius_sm = %d\n", radius_sm);
    fclose(f);
}

/* fdk__ui_copy_theme() takes ui->theme_lock internally, so this is
 * race-free even while a watch thread is concurrently mid-write
 * inside fdk_ui_set_theme() for the same ui. */
static int radius_of(FDK_UI *ui)
{
    FDK_Theme t;
    if (!fdk__ui_copy_theme(ui, &t)) return -1;
    return t.radius_sm;
}

/* Polls (rather than checking once) because inotify delivery + the
 * watch thread waking up and re-parsing isn't instantaneous. Generous
 * timeout to stay robust under slower/sandboxed CI. */
static bool wait_until_radius(FDK_UI *ui, int expected, int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        if (radius_of(ui) == expected) return true;
        sleep_ms(20);
        waited += 20;
    }
    return false;
}

int main(void)
{
    printf("FDK per-UI theme watch regression test\n\n");

    if (system("mkdir -p " SCRATCH_DIR) != 0) {
        fprintf(stderr, "fatal: could not create " SCRATCH_DIR "\n");
        return 1;
    }
    write_theme_radius(PATH_A, 11);
    write_theme_radius(PATH_B, 22);

    FDK_Theme base = fdk_theme_faded_dream();  /* radius_sm = 6 by default —
                                                 * distinct from every value
                                                 * this test writes, so a
                                                 * poll can never false-
                                                 * positive against the
                                                 * untouched default. */

    /* Explicit theme => fdk_ui_create does NOT auto-start a watch, so
     * we get full manual control over exactly when watches start. */
    /* NULL, not a fake non-NULL pointer like (FDK_Window*)0x1: this
     * test never dereferences ui->win or relies on its value, only on
     * the FDK_UI* pointers themselves for identity -- but
     * fdk_ui_set_theme() now calls fdk_window_request_redraw(ui->win)
     * (see this session's redraw-nudge fix), which safely no-ops on
     * NULL via its existing "if (w)" guard. A fake non-NULL pointer
     * would instead crash trying to actually dereference it. */
    FDK_UI *ui_a = fdk_ui_create(NULL, &base);
    FDK_UI *ui_b = fdk_ui_create(NULL, &base);
    CHECK(ui_a && ui_b, "both placeholder UIs created");

    printf("\n-- Two windows can each hold a live watch simultaneously --\n");
    fdk_theme_watch(ui_a, NULL, PATH_A);
    CHECK(fdk__theme_watch_count() == 1, "count = 1 after starting ui_a's watch");
    CHECK(fdk__theme_watch_exists(ui_a), "ui_a's watch is registered");

    fdk_theme_watch(ui_b, NULL, PATH_B);
    /* Under the old single-g_watch bug, this call would have torn
     * down ui_a's watch — count would be 1 and ui_a would be gone. */
    CHECK(fdk__theme_watch_count() == 2,
          "count = 2 after starting ui_b's watch (ui_a's watch survived)");
    CHECK(fdk__theme_watch_exists(ui_a), "ui_a's watch is still registered");
    CHECK(fdk__theme_watch_exists(ui_b), "ui_b's watch is registered");

    /* fdk_theme_watch() only reacts to *future* changes — it doesn't
     * synchronously load the watched file's current content. Load
     * each window's starting state explicitly here, same as a real
     * app loading once up front and then relying on the watch for
     * whatever changes from that point on. */
    FDK_Theme theme_a = fdk_theme_faded_dream();
    FDK_Theme theme_b = fdk_theme_faded_dream();
    fdk_theme_load(&theme_a, PATH_A);
    fdk_theme_load(&theme_b, PATH_B);
    fdk_ui_set_theme(ui_a, NULL, &theme_a);
    fdk_ui_set_theme(ui_b, NULL, &theme_b);
    CHECK(radius_of(ui_a) == 11, "ui_a starts at a.fdktheme's radius_sm (11)");
    CHECK(radius_of(ui_b) == 22, "ui_b starts at b.fdktheme's radius_sm (22)");

    printf("\n-- Real file edits reach the correct window only --\n");
    write_theme_radius(PATH_A, 99);
    CHECK(wait_until_radius(ui_a, 99, 2000),
          "editing a.fdktheme updates ui_a's live theme");
    CHECK(radius_of(ui_b) == 22, "ui_b's theme is untouched by ui_a's file changing");

    write_theme_radius(PATH_B, 77);
    CHECK(wait_until_radius(ui_b, 77, 2000),
          "editing b.fdktheme updates ui_b's live theme");
    CHECK(radius_of(ui_a) == 99, "ui_a's theme is untouched by ui_b's file changing");

    printf("\n-- Stopping one window's watch doesn't affect the other --\n");
    fdk_theme_watch(ui_a, NULL, NULL);   /* stop ui_a's watch */
    CHECK(fdk__theme_watch_count() == 1, "count = 1 after stopping ui_a's watch");
    CHECK(!fdk__theme_watch_exists(ui_a), "ui_a's watch is gone");
    CHECK(fdk__theme_watch_exists(ui_b), "ui_b's watch is still registered");

    printf("\n-- fdk_ui_destroy() cleans up a watch the app forgot to stop --\n");
    fdk_theme_watch(ui_a, NULL, PATH_A);           /* re-arm ui_a's watch */
    CHECK(fdk__theme_watch_count() == 2, "count = 2 after re-arming ui_a's watch");

    FDK_UI *stale_key = ui_a;   /* save the pointer VALUE only — never
                                 * dereferenced after destroy below,
                                 * used purely as an opaque lookup key
                                 * to confirm the list entry is gone */
    fdk_ui_destroy(ui_a);       /* deliberately NOT calling
                                 * fdk_theme_watch(ui_a, NULL, NULL)
                                 * first — this is exactly the scenario
                                 * that used to leave a watch thread
                                 * holding a dangling ui pointer */
    CHECK(fdk__theme_watch_count() == 1,
          "count = 1 after fdk_ui_destroy(ui_a) — its watch was cleaned up");
    CHECK(!fdk__theme_watch_exists(stale_key),
          "ui_a's watch entry is gone from the list post-destroy");
    CHECK(fdk__theme_watch_exists(ui_b),
          "ui_b's watch is unaffected by ui_a's destruction");

    /* Tidy exit */
    fdk_theme_watch(ui_b, NULL, NULL);
    fdk_ui_destroy(ui_b);
    CHECK(fdk__theme_watch_count() == 0, "count = 0 after cleaning up ui_b");

    printf("\n========================================================\n");
    if (g_fail == 0) {
        printf(" ALL CHECKS PASSED\n");
        printf("========================================================\n");
        return 0;
    }
    printf(" %d CHECK(S) FAILED\n", g_fail);
    printf("========================================================\n");
    return 1;
}
