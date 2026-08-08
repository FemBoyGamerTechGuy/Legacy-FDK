/*
 * test_dpi_parse.c — Xft.dpi parser logic test (single-process)
 *
 * Verifies that FDK's X11 backend's Xft.dpi parsing logic correctly
 * extracts the DPI value from the RESOURCE_MANAGER property. We
 * cannot use fdk_window_get_scale() directly here because that
 * requires running fdk_init() with a real X server (Xvfb), and the
 * cross-process property persistence is flaky under Xvfb.
 *
 * Instead, we replicate the parser logic inline (copy of the function
 * from src/platform/x11.c's x11_detect_scale) and verify it against
 * a sequence of RESOURCE_MANAGER strings set on a real X display.
 *
 * If FDK's parser logic is changed, this test should be updated to
 * match (the function body here is intentionally a verbatim copy).
 *
 * Usage:
 *   DISPLAY=:99 ./test_dpi_parse
 *
 * Exit code 0 = all checks passed, non-zero = a check failed.
 * Skips cleanly (exit 0) if no X display is available.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

/* ─── Verbatim copy of x11_detect_scale's RESOURCE_MANAGER parser ──────── */
static float parse_rm_for_dpi(Display *dpy, int screen)
{
    Atom res_mgr = XInternAtom(dpy, "RESOURCE_MANAGER", True);
    if (res_mgr == None) return 1.0f;
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, RootWindow(dpy, screen),
                            res_mgr, 0, 65536, False, XA_STRING,
                            &actual_type, &actual_format,
                            &nitems, &bytes_after, &data) != Success) {
        return 1.0f;
    }
    if (!data || actual_type != XA_STRING || nitems == 0) {
        if (data) XFree(data);
        return 1.0f;
    }
    float scale = 1.0f;
    const char *p = (const char *)data;
    const char *end = p + nitems;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (p >= end) break;
        if (p + 7 <= end && strncmp(p, "Xft.dpi", 7) == 0) {
            const char *q = p + 7;
            while (q < end && (*q == ':' || *q == ' ' || *q == '\t')) q++;
            char numbuf[64];
            int copy_len = (int)(end - q);
            if (copy_len > (int)sizeof(numbuf) - 1) copy_len = (int)sizeof(numbuf) - 1;
            memcpy(numbuf, q, copy_len);
            numbuf[copy_len] = '\0';
            char *strtod_end = NULL;
            double dpi = strtod(numbuf, &strtod_end);
            if (strtod_end != numbuf && dpi > 0.0 && dpi < 1000.0) {
                scale = (float)(dpi / 96.0);
                if (scale < 0.5f) scale = 0.5f;
                if (scale > 8.0f) scale = 8.0f;
                break;
            }
        }
        while (p < end && *p != '\n') p++;
    }
    XFree(data);
    return scale;
}

static void set_rm(Display *dpy, int screen, const char *content)
{
    Atom res_mgr = XInternAtom(dpy, "RESOURCE_MANAGER", False);
    XChangeProperty(dpy, RootWindow(dpy, screen),
                    res_mgr, XA_STRING, 8, PropModeReplace,
                    (unsigned char *)content, strlen(content));
    XSync(dpy, False);
}

int main(void)
{
    printf("=== FDK Xft.dpi parser test ===\n");

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("[SKIP] No X display available — skipping.\n");
        printf("[NOTE] Run with DISPLAY=:99 (Xvfb) to execute this test.\n");
        return 0;
    }
    int screen = DefaultScreen(dpy);

    /* ─── Test 1: no RESOURCE_MANAGER — should default to 1.0 ─────────── */
    printf("\n[Test 1] No RESOURCE_MANAGER property\n");
    {
        /* Delete the property entirely */
        Atom res_mgr = XInternAtom(dpy, "RESOURCE_MANAGER", False);
        XDeleteProperty(dpy, RootWindow(dpy, screen), res_mgr);
        XSync(dpy, False);
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.000)\n", s);
        CHECK(s == 1.0f, "no RESOURCE_MANAGER → scale 1.0");
    }

    /* ─── Test 2: Xft.dpi = 96 (1×) ───────────────────────────────────── */
    printf("\n[Test 2] Xft.dpi=96 (1×)\n");
    {
        set_rm(dpy, screen, "Xft.dpi:\t96\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.000)\n", s);
        CHECK(s == 1.0f, "Xft.dpi=96 → scale 1.0");
    }

    /* ─── Test 3: Xft.dpi = 192 (2×) ──────────────────────────────────── */
    printf("\n[Test 3] Xft.dpi=192 (2×)\n");
    {
        set_rm(dpy, screen, "Xft.dpi:\t192\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 2.000)\n", s);
        CHECK(s == 2.0f, "Xft.dpi=192 → scale 2.0");
    }

    /* ─── Test 4: Xft.dpi = 144 (1.5×) ────────────────────────────────── */
    printf("\n[Test 4] Xft.dpi=144 (1.5×)\n");
    {
        set_rm(dpy, screen, "Xft.dpi:\t144\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.500)\n", s);
        CHECK(s >= 1.49f && s <= 1.51f, "Xft.dpi=144 → scale 1.5");
    }

    /* ─── Test 5: Xft.dpi = 120 (1.25×) ───────────────────────────────── */
    printf("\n[Test 5] Xft.dpi=120 (1.25×)\n");
    {
        set_rm(dpy, screen, "Xft.dpi:\t120\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.250)\n", s);
        CHECK(s >= 1.24f && s <= 1.26f, "Xft.dpi=120 → scale 1.25");
    }

    /* ─── Test 6: Xft.dpi buried among other resources ────────────────── */
    printf("\n[Test 6] Xft.dpi=168 buried among other Xft.* keys\n");
    {
        set_rm(dpy, screen,
               "Xft.antialias:\t1\nXft.hinting:\t1\nXft.dpi:\t168\nXft.rgba:\tnone\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.750)\n", s);
        CHECK(s >= 1.74f && s <= 1.76f, "Xft.dpi=168 in middle → scale 1.75");
    }

    /* ─── Test 7: no Xft.dpi key at all ───────────────────────────────── */
    printf("\n[Test 7] RESOURCE_MANAGER set but no Xft.dpi key\n");
    {
        set_rm(dpy, screen, "Xft.antialias:\t1\nXft.hinting:\t1\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.000)\n", s);
        CHECK(s == 1.0f, "no Xft.dpi key → scale 1.0");
    }

    /* ─── Test 8: malformed Xft.dpi value ─────────────────────────────── */
    printf("\n[Test 8] Malformed Xft.dpi value\n");
    {
        set_rm(dpy, screen, "Xft.dpi:\tnot-a-number\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 1.000 — fallback)\n", s);
        CHECK(s == 1.0f, "malformed Xft.dpi → scale 1.0 (fallback)");
    }

    /* ─── Test 9: spaces instead of tab as separator ──────────────────── */
    printf("\n[Test 9] Xft.dpi with spaces (not tabs) as separator\n");
    {
        set_rm(dpy, screen, "Xft.dpi: 192\n");
        float s = parse_rm_for_dpi(dpy, screen);
        printf("    scale = %.3f (expect 2.000)\n", s);
        CHECK(s == 2.0f, "Xft.dpi: 192 (space sep) → scale 2.0");
    }

    XCloseDisplay(dpy);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
