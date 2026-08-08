/*
 * hidpi_runtime.c — runtime test for fdk_window_get_scale on X11
 *
 * Opens an FDK window on the X11 backend and verifies that:
 *   - The scale matches what GDK_SCALE env var dictates (when set)
 *   - The scale matches what Xft.dpi resource dictates (when set)
 *
 * This program is NOT a unit test — it needs a running X server.
 * The build system runs it via Xvfb. Outside the build system it's
 * a manual smoke test.
 *
 * NOTE on Xft.dpi testing under Xvfb:
 * Xvfb has a quirk where root window properties set by a short-lived
 * client (like the `set_rm` helper used in the build environment)
 * may not persist after that client disconnects. On a real Linux
 * desktop, RESOURCE_MANAGER is set by the display manager at login
 * and persists for the session. So the Xft.dpi code path is verified
 * by a separate single-process test (test_dpi_parse in the test
 * script), not by this cross-process runtime test. GDK_SCALE, which
 * is read from the environment directly, IS verified here.
 *
 * Usage:
 *   DISPLAY=:99 ./hidpi_runtime
 *   GDK_SCALE=2 DISPLAY=:99 ./hidpi_runtime
 *
 * Exit code 0 = success, non-zero = failure.
 */
#define _POSIX_C_SOURCE 200809L
#include <fdk/fdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    /* Force X11 — Wayland isn't available in this test env */
    FDK_InitInfo info = {
        .platform = FDK_PLATFORM_X11,
        .render   = FDK_RENDER_SOFTWARE,
        .app_name = "hidpi_runtime_test",
    };
    if (!fdk_init(&info)) {
        fprintf(stderr, "fdk_init failed (no DISPLAY?)\n");
        return 1;
    }

    FDK_WindowDesc desc = {
        .title  = "HiDPI runtime test",
        .x = -1, .y = -1, .w = 200, .h = 100,
        .resizable = false,
    };
    FDK_Window *win = fdk_window_create(&desc);
    if (!win) {
        fprintf(stderr, "fdk_window_create failed\n");
        fdk_shutdown();
        return 1;
    }

    float scale = fdk_window_get_scale(win);
    int   dpi   = fdk_window_get_dpi(win);

    printf("FDK features: %s\n", fdk_get_features());
    printf("Window scale: %.3f\n", scale);
    printf("Window DPI:   %d\n", dpi);
    printf("GDK_SCALE:    %s\n", getenv("GDK_SCALE") ? getenv("GDK_SCALE") : "(unset)");

    /* Sanity: scale should be in a reasonable range */
    if (scale < 0.5f || scale > 8.0f) {
        fprintf(stderr, "FAIL: scale out of range: %.3f\n", scale);
        fdk_window_destroy(win);
        fdk_shutdown();
        return 1;
    }

    /* DPI should be round(scale * 96) */
    int expected_dpi = (int)(scale * 96.0f + 0.5f);
    if (dpi != expected_dpi) {
        fprintf(stderr, "FAIL: dpi=%d but expected %d (scale=%.3f * 96)\n",
                dpi, expected_dpi, scale);
        fdk_window_destroy(win);
        fdk_shutdown();
        return 1;
    }

    /* GDK_SCALE, if set to a valid integer, should match scale */
    const char *gs = getenv("GDK_SCALE");
    if (gs && gs[0]) {
        char *end = NULL;
        long v = strtol(gs, &end, 10);
        if (end != gs && v >= 1 && v <= 8) {
            if (scale != (float)v) {
                fprintf(stderr,
                        "FAIL: GDK_SCALE=%ld but scale=%.3f (should match)\n",
                        v, scale);
                fdk_window_destroy(win);
                fdk_shutdown();
                return 1;
            }
            printf("PASS: GDK_SCALE=%ld matched scale=%.3f\n", v, scale);
        }
    }

    fdk_window_destroy(win);
    fdk_shutdown();
    printf("PASS\n");
    return 0;
}
