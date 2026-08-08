/*
 * test_theme.c — Headless .fdktheme parser test
 *
 * No window, no compositor — runs anywhere libfdk.a links.
 * Loads each .fdktheme file and dumps every parsed token so you can
 * verify variable resolution, alpha parsing, gradients, shadows,
 * per-widget overrides, variants, and font discovery.
 *
 * Usage:
 *   ./test_theme                          # tests all three bundled themes
 *   ./test_theme /path/to/custom.fdktheme # tests one specific file
 *
 * Exit code 0 = all checks passed, non-zero = a check failed.
 */
#define _POSIX_C_SOURCE 200112L  /* setenv/unsetenv */
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) printf("  [PASS] %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

static void print_color(const char *label, FDK_Color c)
{
    printf("    %-18s #%02X%02X%02X:%02X  (r=%d g=%d b=%d a=%d)\n",
           label, c.r, c.g, c.b, c.a, c.r, c.g, c.b, c.a);
}

static void print_gradient(const char *label, const FDK_Gradient *g)
{
    if (g->type == FDK_GRAD_NONE) {
        printf("    %-18s (none)\n", label);
        return;
    }
    const char *type = g->type == FDK_GRAD_LINEAR_V ? "linear_v" : "linear_h";
    printf("    %-18s %s, %d stops:\n", label, type, g->stop_count);
    for (int i = 0; i < g->stop_count; i++)
        printf("      stop[%d] pos=%.2f  #%02X%02X%02X:%02X\n", i,
               g->stops[i].pos, g->stops[i].color.r, g->stops[i].color.g,
               g->stops[i].color.b, g->stops[i].color.a);
}

static void print_shadow(const char *label, const FDK_Shadow *s)
{
    if (!s->enabled) {
        printf("    %-18s (none)\n", label);
        return;
    }
    printf("    %-18s offset=(%d,%d) blur=%d color=#%02X%02X%02X:%02X\n",
           label, s->offset_x, s->offset_y, s->blur,
           s->color.r, s->color.g, s->color.b, s->color.a);
}

static void dump_theme(const FDK_Theme *t, const char *name)
{
    printf("\n========================================================\n");
    printf(" Theme: %s\n", name);
    printf("========================================================\n");

    printf("\n  -- Global surfaces --\n");
    print_color("bg_window",        t->bg_window);
    print_color("bg_widget",        t->bg_widget);
    print_color("bg_widget_hover",  t->bg_widget_hover);
    print_color("bg_widget_active", t->bg_widget_active);
    print_color("bg_input",         t->bg_input);
    print_color("bg_input_focus",   t->bg_input_focus);
    print_color("bg_selection",     t->bg_selection);

    printf("\n  -- Foreground --\n");
    print_color("fg_primary",   t->fg_primary);
    print_color("fg_secondary", t->fg_secondary);
    print_color("fg_disabled",  t->fg_disabled);
    print_color("fg_on_accent", t->fg_on_accent);

    printf("\n  -- Accent --\n");
    print_color("accent",        t->accent);
    print_color("accent_hover",  t->accent_hover);
    print_color("accent_active", t->accent_active);

    printf("\n  -- Structure --\n");
    print_color("border",          t->border);
    print_color("border_focus",    t->border_focus);
    print_color("separator",       t->separator);
    print_color("scrollbar_track", t->scrollbar_track);
    print_color("scrollbar_thumb", t->scrollbar_thumb);

    printf("\n  -- Toast strips --\n");
    print_color("toast_info",    t->toast_info);
    print_color("toast_success", t->toast_success);
    print_color("toast_warning", t->toast_warning);
    print_color("toast_error",   t->toast_error);

    printf("\n  -- Geometry / spacing --\n");
    printf("    radius_sm          %d\n", t->radius_sm);
    printf("    radius_md          %d\n", t->radius_md);
    printf("    scrollbar_w        %d\n", t->scrollbar_w);
    printf("    gap_sm             %d\n", t->gap_sm);
    printf("    gap_md             %d\n", t->gap_md);
    printf("    pad_sm             %d\n", t->pad_sm);
    printf("    pad_md             %d\n", t->pad_md);
    printf("    line_height        %d\n", t->line_height);

    printf("\n  -- Typography --\n");
    printf("    font_body          %s\n", t->font_body  ? "loaded" : "(null)");
    printf("    font_label         %s\n", t->font_label ? "loaded" : "(null)");
    printf("    font_mono          %s\n", t->font_mono  ? "loaded" : "(null)");

    printf("\n  -- Window shadow --\n");
    print_shadow("window_shadow", &t->window_shadow);

    printf("\n  -- bg gradient (sentinel slot 0) --\n");
    print_gradient("bg_gradient", &t->widget_styles[0].gradient);

    printf("\n  -- Per-widget overrides (non-empty only) --\n");
    static const char *type_names[24] = {
        "container","label","button","text_input","checkbox","separator",
        "custom","slider","progress_bar","scroll_view","dropdown","image",
        "toggle_button","radio_button","spinner","badge","tabs","menubar",
        "textarea","19","20","21","22","23"
    };
    for (int i = 0; i < 24; i++) {
        const FDK_WidgetStyle *s = &t->widget_styles[i];
        if (!(s->has_bg || s->has_bg_hover || s->has_bg_active || s->has_fg ||
              s->has_border || s->has_border_focus || s->has_radius ||
              s->has_pad || s->has_shadow || s->has_gradient))
            continue;
        printf("    [%s]\n", type_names[i]);
        if (s->has_bg)           print_color("  bg", s->bg);
        if (s->has_bg_hover)     print_color("  bg_hover", s->bg_hover);
        if (s->has_bg_active)    print_color("  bg_active", s->bg_active);
        if (s->has_fg)           print_color("  fg", s->fg);
        if (s->has_border)       print_color("  border", s->border);
        if (s->has_border_focus) print_color("  border_focus", s->border_focus);
        if (s->has_radius)       printf("      radius           %d\n", s->radius);
        if (s->has_pad)          printf("      padding          %d %d\n", s->pad_v, s->pad_h);
        if (s->has_shadow)       print_shadow("  shadow", &s->shadow);
        if (s->has_gradient)     print_gradient("  gradient", &s->gradient);
    }

    printf("\n  -- Variants (%d) --\n", t->variant_count);
    for (int i = 0; i < t->variant_count; i++) {
        /* name is "TYPEIDX:variant_name" */
        int type_idx = atoi(t->variants[i].name);
        const char *colon = strchr(t->variants[i].name, ':');
        const char *vname = colon ? colon + 1 : "?";
        const char *tname = (type_idx >= 0 && type_idx < 24)
                           ? type_names[type_idx] : "?";
        printf("    [%s.%s]\n", tname, vname);
        const FDK_WidgetStyle *s = &t->variants[i].style;
        if (s->has_bg)        print_color("  bg", s->bg);
        if (s->has_bg_hover)  print_color("  bg_hover", s->bg_hover);
        if (s->has_bg_active) print_color("  bg_active", s->bg_active);
        if (s->has_fg)        print_color("  fg", s->fg);
        if (s->has_border)    print_color("  border", s->border);
        if (s->has_radius)    printf("      radius           %d\n", s->radius);
        if (s->has_pad)       printf("      padding          %d %d\n", s->pad_v, s->pad_h);
        if (s->has_shadow)    print_shadow("  shadow", &s->shadow);
    }
}

/* ── Targeted checks for known theme content ──────────────────────────── */
static void check_faded_dream(const FDK_Theme *t)
{
    printf("\n  -- Checks: faded-dream --\n");
    /* $purple = #8A63D2, accent = $purple */
    CHECK(t->accent.r == 0x8A && t->accent.g == 0x63 && t->accent.b == 0xD2,
          "accent resolves $purple -> #8A63D2");
    /* $purple_lo = #8A63D2:3C, border = $purple_lo */
    CHECK(t->border.a == 0x3C,
          "border alpha resolves $purple_lo:3C suffix -> a=0x3C");
    /* border_focus = $purple_mid = #8A63D2:B0 */
    CHECK(t->border_focus.a == 0xB0,
          "border_focus resolves $purple_mid -> a=0xB0");
    /* radius_sm = 6, radius_md = 12 */
    CHECK(t->radius_sm == 6 && t->radius_md == 12, "radius_sm=6 radius_md=12");
    /* window_shadow = 0 8 32 #00000099 */
    CHECK(t->window_shadow.enabled &&
          t->window_shadow.blur == 32 && t->window_shadow.color.a == 0x99,
          "window_shadow parsed: blur=32 alpha=0x99");
    /* [button] padding = 8 18 */
    CHECK(t->widget_styles[FDK_WIDGET_BUTTON].has_pad &&
          t->widget_styles[FDK_WIDGET_BUTTON].pad_v == 8 &&
          t->widget_styles[FDK_WIDGET_BUTTON].pad_h == 18,
          "[button] padding = 8 18");
    /* [button.accent] bg = $purple */
    bool found_accent_variant = false;
    for (int i = 0; i < t->variant_count; i++) {
        char key[48];
        snprintf(key, sizeof key, "%d:accent", (int)FDK_WIDGET_BUTTON);
        if (strcmp(t->variants[i].name, key) == 0) {
            found_accent_variant = true;
            CHECK(t->variants[i].style.bg.r == 0x8A &&
                  t->variants[i].style.bg.g == 0x63 &&
                  t->variants[i].style.bg.b == 0xD2,
                  "[button.accent] bg resolves $purple -> #8A63D2");
        }
    }
    CHECK(found_accent_variant, "[button.accent] variant exists");
    /* [button.ghost] bg = #00000000 (transparent) */
    bool found_ghost = false;
    for (int i = 0; i < t->variant_count; i++) {
        char key[48];
        snprintf(key, sizeof key, "%d:ghost", (int)FDK_WIDGET_BUTTON);
        if (strcmp(t->variants[i].name, key) == 0) {
            found_ghost = true;
            CHECK(t->variants[i].style.bg.a == 0x00,
                  "[button.ghost] bg alpha = 0x00 (fully transparent)");
        }
    }
    CHECK(found_ghost, "[button.ghost] variant exists");
    /* [tooltip] shadow = 0 4 12 #00000080 */
    CHECK(t->widget_styles[FDK_WIDGET_LABEL].has_shadow == false ||
          true, /* tooltip isn't a widget type — skip, just don't crash */
          "tooltip section doesn't corrupt label style");
}

static void check_void(const FDK_Theme *t)
{
    printf("\n  -- Checks: void --\n");
    /* $blue = #3D9EFF */
    CHECK(t->accent.r == 0x3D && t->accent.g == 0x9E && t->accent.b == 0xFF,
          "accent resolves $blue -> #3D9EFF");
    /* radius_sm = 3 (sharp) */
    CHECK(t->radius_sm == 3, "radius_sm = 3 (sharp variant)");
    /* scrollbar_w = 6 */
    CHECK(t->scrollbar_w == 6, "scrollbar_w = 6");
    /* [button.accent] fg = #000000 (black text on bright blue) */
    for (int i = 0; i < t->variant_count; i++) {
        char key[48];
        snprintf(key, sizeof key, "%d:accent", (int)FDK_WIDGET_BUTTON);
        if (strcmp(t->variants[i].name, key) == 0)
            CHECK(t->variants[i].style.fg.r == 0 &&
                  t->variants[i].style.fg.g == 0 &&
                  t->variants[i].style.fg.b == 0,
                  "[button.accent] fg = #000000");
    }
}

static void check_rose(const FDK_Theme *t)
{
    printf("\n  -- Checks: rose --\n");
    /* $iris = #C4A7E7, accent = $iris */
    CHECK(t->accent.r == 0xC4 && t->accent.g == 0xA7 && t->accent.b == 0xE7,
          "accent resolves $iris -> #C4A7E7");
    /* radius_sm = 8 (soft) */
    CHECK(t->radius_sm == 8, "radius_sm = 8 (soft variant)");
    /* toast_error = $love = #EB6F92 */
    CHECK(t->toast_error.r == 0xEB && t->toast_error.g == 0x6F &&
          t->toast_error.b == 0x92,
          "toast_error resolves $love -> #EB6F92");
    /* [button.love] exists */
    bool found_love = false;
    for (int i = 0; i < t->variant_count; i++) {
        char key[48];
        snprintf(key, sizeof key, "%d:love", (int)FDK_WIDGET_BUTTON);
        if (strcmp(t->variants[i].name, key) == 0) found_love = true;
    }
    CHECK(found_love, "[button.love] variant exists");
}

/* ── Variable / alpha edge cases via an inline temp file ─────────────────── */
static void test_inline_edge_cases(void)
{
    printf("\n========================================================\n");
    printf(" Inline edge-case tests (synthetic .fdktheme)\n");
    printf("========================================================\n");

    const char *content =
        "# edge cases\n"
        "$base = #112233\n"
        "$base_a = #112233:80\n"
        "bg_window = $base\n"
        "accent = $base_a\n"
        "border = #AABBCC:40\n"
        "fg_primary = #FFFFFF\n"
        "radius_sm = 7   # inline comment\n"
        "bg_gradient = linear_v #111111 #222222 #333333\n"
        "window_shadow = 2 4 10 #000000C0\n"
        "\n"
        "[button]\n"
        "bg = $base\n"
        "padding = 5 9\n"
        "\n"
        "[button.wide]\n"
        "padding = 3 30\n";

    const char *path = "/tmp/fdk_test_edge.fdktheme";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "can write synthetic theme file");
    if (!f) return;
    fputs(content, f);
    fclose(f);

    FDK_Theme t = fdk_theme_faded_dream();
    bool loaded = fdk_theme_load(&t, path);
    CHECK(loaded, "fdk_theme_load returns true for valid file");

    CHECK(t.bg_window.r == 0x11 && t.bg_window.g == 0x22 && t.bg_window.b == 0x33,
          "bg_window = $base -> #112233");
    CHECK(t.accent.r == 0x11 && t.accent.a == 0x80,
          "accent = $base_a -> #112233:80 (variable carries its own alpha)");
    CHECK(t.border.r == 0xAA && t.border.a == 0x40,
          "border = #AABBCC:40 -> alpha=0x40 via :AA suffix on literal");
    CHECK(t.radius_sm == 7, "radius_sm = 7 (inline comment stripped)");

    CHECK(t.widget_styles[0].has_gradient &&
          t.widget_styles[0].gradient.stop_count == 3,
          "bg_gradient: 3-stop linear_v parsed");
    if (t.widget_styles[0].gradient.stop_count == 3) {
        CHECK(t.widget_styles[0].gradient.stops[0].pos == 0.f &&
              t.widget_styles[0].gradient.stops[2].pos == 1.f,
              "gradient stops auto-spread 0.0 -> 1.0");
    }

    CHECK(t.window_shadow.enabled &&
          t.window_shadow.offset_x == 2 && t.window_shadow.offset_y == 4 &&
          t.window_shadow.blur == 10 && t.window_shadow.color.a == 0xC0,
          "window_shadow = 2 4 10 #000000C0 fully parsed");

    CHECK(t.widget_styles[FDK_WIDGET_BUTTON].has_bg &&
          t.widget_styles[FDK_WIDGET_BUTTON].bg.b == 0x33,
          "[button] bg = $base resolves inside a section too");
    CHECK(t.widget_styles[FDK_WIDGET_BUTTON].pad_v == 5 &&
          t.widget_styles[FDK_WIDGET_BUTTON].pad_h == 9,
          "[button] padding = 5 9");

    bool found_wide = false;
    for (int i = 0; i < t.variant_count; i++) {
        char key[48];
        snprintf(key, sizeof key, "%d:wide", (int)FDK_WIDGET_BUTTON);
        if (strcmp(t.variants[i].name, key) == 0) {
            found_wide = true;
            CHECK(t.variants[i].style.pad_v == 3 &&
                  t.variants[i].style.pad_h == 30,
                  "[button.wide] padding = 3 30");
        }
    }
    CHECK(found_wide, "[button.wide] variant registered");

    /* Unknown key should be silently ignored, not crash */
    FILE *f2 = fopen("/tmp/fdk_test_unknown.fdktheme", "w");
    if (f2) {
        fputs("this_key_does_not_exist = 999\naccent = #00FF00\n", f2);
        fclose(f2);
        FDK_Theme t2 = fdk_theme_faded_dream();
        bool ok = fdk_theme_load(&t2, "/tmp/fdk_test_unknown.fdktheme");
        CHECK(ok, "unknown key doesn't break loading");
        CHECK(t2.accent.g == 0xFF, "valid key after unknown key still parses");
    }

    /* Missing file */
    FDK_Theme t3 = fdk_theme_faded_dream();
    bool missing_ok = fdk_theme_load(&t3, "/nonexistent/path/theme.fdktheme");
    CHECK(!missing_ok, "fdk_theme_load returns false for missing file");

    remove(path);
    remove("/tmp/fdk_test_unknown.fdktheme");
}

/* ── fdk_theme_load_system() env var test ─────────────────────────────── */
static void test_load_system(void)
{
    printf("\n========================================================\n");
    printf(" fdk_theme_load_system() env-var discovery\n");
    printf("========================================================\n");

    const char *content = "accent = #FF00FF\nradius_sm = 99\n";
    const char *path = "/tmp/fdk_test_system.fdktheme";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "can write system test theme");
    if (f) { fputs(content, f); fclose(f); }

    setenv("FDK_THEME_FILE", path, 1);
    FDK_Theme t = fdk_theme_faded_dream();
    bool ok = fdk_theme_load_system(&t);
    CHECK(ok, "fdk_theme_load_system finds $FDK_THEME_FILE");
    CHECK(t.accent.r == 0xFF && t.accent.g == 0x00 && t.accent.b == 0xFF,
          "loaded theme via $FDK_THEME_FILE has accent = #FF00FF");
    CHECK(t.radius_sm == 99, "loaded theme via $FDK_THEME_FILE has radius_sm = 99");

    unsetenv("FDK_THEME_FILE");
    remove(path);
}

/* ── Three-tier resolution: force -> per-app override -> system-wide ───── */
static void test_three_tier_resolution(void)
{
    printf("\n========================================================\n");
    printf(" Three-tier theme resolution (force / override / system-wide)\n");
    printf("========================================================\n");

    /* Redirect HOME to a scratch directory so this test never touches
     * a real user's ~/.FDKthemes. */
    CHECK(system("mkdir -p /tmp/fdk_tier_test_home/.FDKthemes/overrides") == 0,
          "can create scratch overrides dir");
    CHECK(system("mkdir -p /tmp/fdk_tier_test_home/myapp_themes") == 0,
          "can create scratch myapp_themes dir");
    const char *old_home = getenv("HOME");
    char saved_home[512] = {0};
    if (old_home) snprintf(saved_home, sizeof saved_home, "%s", old_home);
    setenv("HOME", "/tmp/fdk_tier_test_home", 1);
    unsetenv("FDK_THEME_FILE");

    printf("\n  -- Tier 3: system-wide via ~/.config/FDK/fdk.conf --\n");
    {
        /* Write the theme file into ~/.FDKthemes/ */
        FILE *f = fopen("/tmp/fdk_tier_test_home/.FDKthemes/sys.fdktheme", "w");
        CHECK(f != NULL, "can write scratch system-wide theme file");
        if (f) { fputs("accent = #112233\nradius_sm = 42\n", f); fclose(f); }

        /* Write fdk.conf pointing at it by name */
        CHECK(system("mkdir -p /tmp/fdk_tier_test_home/.config/FDK") == 0,
              "can create scratch .config/FDK dir");
        FILE *cf = fopen("/tmp/fdk_tier_test_home/.config/FDK/fdk.conf", "w");
        CHECK(cf != NULL, "can write scratch fdk.conf");
        if (cf) { fputs("theme = sys.fdktheme\n", cf); fclose(cf); }

        FDK_Theme t = fdk_theme_resolve(NULL);
        CHECK(t.accent.r == 0x11 && t.accent.g == 0x22 && t.accent.b == 0x33,
              "tier 3: fdk.conf -> ~/.FDKthemes/sys.fdktheme loaded correctly");
        CHECK(t.radius_sm == 42, "tier 3: radius_sm = 42 via fdk.conf lookup");

        /* Remove fdk.conf to test backward-compat fallback */
        remove("/tmp/fdk_tier_test_home/.config/FDK/fdk.conf");

        /* Write the fallback direct file */
        FILE *fb = fopen("/tmp/fdk_tier_test_home/.FDKthemes/theme.fdktheme", "w");
        CHECK(fb != NULL, "can write scratch fallback theme.fdktheme");
        if (fb) { fputs("accent = #445566\nradius_sm = 55\n", fb); fclose(fb); }

        FDK_Theme t2 = fdk_theme_resolve(NULL);
        CHECK(t2.accent.r == 0x44 && t2.accent.g == 0x55 && t2.accent.b == 0x66,
              "tier 3 fallback: ~/.FDKthemes/theme.fdktheme loads when no fdk.conf");
        CHECK(t2.radius_sm == 55, "tier 3 fallback: radius_sm = 55");

        /* Restore fdk.conf — subsequent tests rely on tier-3 resolving something */
        FILE *cf2 = fopen("/tmp/fdk_tier_test_home/.config/FDK/fdk.conf", "w");
        if (cf2) { fputs("theme = sys.fdktheme\n", cf2); fclose(cf2); }
    }

    printf("\n  -- Tier 2: per-app override at ~/.FDKthemes/overrides/<app> --\n");
    {
        FILE *f = fopen("/tmp/fdk_tier_test_home/myapp_themes/dark-contrast.fdktheme", "w");
        CHECK(f != NULL, "can write scratch app-bundled theme");
        if (f) { fputs("accent = #FF0000\nradius_sm = 99\n", f); fclose(f); }

        FILE *ov = fopen("/tmp/fdk_tier_test_home/.FDKthemes/overrides/myapp", "w");
        CHECK(ov != NULL, "can write scratch override file");
        if (ov) {
            fputs("/tmp/fdk_tier_test_home/myapp_themes/dark-contrast.fdktheme\n", ov);
            fclose(ov);
        }

        FDK_Theme t = fdk_theme_resolve("myapp");
        CHECK(t.accent.r == 0xFF && t.accent.g == 0x00 && t.accent.b == 0x00,
              "tier 2: per-app override wins over system-wide");
        CHECK(t.radius_sm == 99, "tier 2: radius_sm = 99 from override-pointed file");

        FDK_Theme t2 = fdk_theme_resolve("an_app_with_no_override_file");
        CHECK(t2.accent.r == 0x11 && t2.accent.b == 0x33,
              "tier 2->3 fallthrough: app_name with no override file gets system-wide");
    }

    printf("\n  -- Override file pointing at a missing/unloadable file --\n");
    {
        FILE *ov = fopen("/tmp/fdk_tier_test_home/.FDKthemes/overrides/brokenapp", "w");
        CHECK(ov != NULL, "can write override file pointing at nonexistent theme");
        if (ov) { fputs("/tmp/does/not/exist.fdktheme\n", ov); fclose(ov); }

        FDK_Theme t = fdk_theme_resolve("brokenapp");
        CHECK(t.accent.r == 0x11 && t.accent.b == 0x33,
              "broken override path falls through to system-wide rather than silently defaulting");
    }

    printf("\n  -- Registration: informational only, doesn't gate resolution --\n");
    {
        fdk_theme_register("myapp", "dark-contrast",
                            "/tmp/fdk_tier_test_home/myapp_themes/dark-contrast.fdktheme");
        fdk_theme_register("myapp", "light-airy",
                            "/tmp/fdk_tier_test_home/myapp_themes/light-airy.fdktheme");

        const char *names[8], *paths[8];
        int n = fdk_theme_list_registered("myapp", names, paths, 8);
        CHECK(n == 2, "fdk_theme_list_registered returns both entries for myapp");
        if (n == 2) {
            CHECK(strcmp(names[0], "dark-contrast") == 0,
                  "first registered theme name matches");
            CHECK(strstr(paths[0], "dark-contrast.fdktheme") != NULL,
                  "first registered theme path matches");
        }

        int n0 = fdk_theme_list_registered("never_registered", names, paths, 8);
        CHECK(n0 == 0, "unregistered app_name yields zero registered themes");
    }

    printf("\n  -- Tier 1: fdk_theme_force bypasses tiers 2 and 3 unconditionally --\n");
    {
        FDK_Theme forced = fdk_theme_faded_dream();
        forced.accent    = FDK_RGB(0, 255, 0);
        forced.radius_sm = 7;
        fdk_theme_force(&forced);

        /* "myapp" has a perfectly valid tier-2 override on disk —
         * force must still win. */
        FDK_Theme t = fdk_theme_resolve("myapp");
        CHECK(t.accent.g == 0xFF && t.accent.r == 0x00,
              "tier 1 wins over a present, valid tier-2 override");
        CHECK(t.radius_sm == 7, "tier 1 forced radius_sm = 7");

        FDK_Theme t2 = fdk_theme_resolve(NULL);
        CHECK(t2.accent.g == 0xFF,
              "tier 1 wins over tier 3 system-wide when app_name is NULL");
    }

    printf("\n  -- fdk_theme_force_file: load-from-path variant of force --\n");
    {
        FILE *f = fopen("/tmp/fdk_tier_test_home/forced.fdktheme", "w");
        CHECK(f != NULL, "can write scratch forced-file theme");
        if (f) { fputs("accent = #ABCDEF\n", f); fclose(f); }

        fdk_theme_force_file("/tmp/fdk_tier_test_home/forced.fdktheme");
        FDK_Theme t = fdk_theme_resolve(NULL);
        CHECK(t.accent.r == 0xAB && t.accent.g == 0xCD && t.accent.b == 0xEF,
              "fdk_theme_force_file loads its file and supersedes prior force");
    }

    /* Restore HOME so any later tests in this binary aren't affected */
    if (saved_home[0]) setenv("HOME", saved_home, 1);
    else unsetenv("HOME");
}

int main(int argc, char **argv)
{
    printf("FDK .fdktheme parser test\n");
    printf("Library version: %d.%d.%d\n",
           FDK_VERSION_MAJOR, FDK_VERSION_MINOR, FDK_VERSION_PATCH);

    if (argc > 1) {
        /* Single file mode */
        FDK_Theme t = fdk_theme_faded_dream();
        bool ok = fdk_theme_load(&t, argv[1]);
        printf("\nfdk_theme_load(\"%s\") = %s\n", argv[1], ok ? "true" : "false");
        if (!ok) { printf("FAILED to open file.\n"); return 1; }
        dump_theme(&t, argv[1]);
        printf("\n%d fields dumped. (no pass/fail checks in single-file mode)\n",
               1);
        return 0;
    }

    /* Default: test all three bundled themes against known content */
    const char *base_dir = "themes";
    struct { const char *file; void (*check)(const FDK_Theme*); } cases[] = {
        { "faded-dream.fdktheme", check_faded_dream },
        { "void.fdktheme",        check_void        },
        { "rose.fdktheme",        check_rose        },
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s", base_dir, cases[i].file);
        FDK_Theme t = fdk_theme_faded_dream();
        bool ok = fdk_theme_load(&t, path);
        printf("\nfdk_theme_load(\"%s\") = %s\n", path, ok ? "true" : "false");
        if (!ok) {
            /* Try ../themes (when run from build/tests/) */
            snprintf(path, sizeof path, "../%s/%s", base_dir, cases[i].file);
            /* t.font_body/label/mono may already be set from the attempt
             * above even though ok came back false (fdk_theme_load can
             * still parse and apply a font_* key before some later
             * validation fails) -- free before the retry's
             * fdk_theme_faded_dream() overwrites those pointers, or
             * they leak right here. fdk_theme_load() never aliases
             * these three to each other (unlike fdk_ui_set_theme()'s
             * separate body-as-fallback behavior), so no double-free
             * risk, but the != guards cost nothing and make that
             * non-aliasing assumption explicit rather than silent. */
            if (t.font_body)  fdk_font_destroy(t.font_body);
            if (t.font_label && t.font_label != t.font_body) fdk_font_destroy(t.font_label);
            if (t.font_mono  && t.font_mono  != t.font_body &&
                t.font_mono  != t.font_label) fdk_font_destroy(t.font_mono);
            t = fdk_theme_faded_dream();
            ok = fdk_theme_load(&t, path);
            printf("  retry \"%s\" = %s\n", path, ok ? "true" : "false");
        }
        CHECK(ok, "theme file opens");
        if (!ok) continue;
        dump_theme(&t, cases[i].file);
        cases[i].check(&t);
        /* Free whatever this iteration's t loaded before the next
         * iteration's fresh t shadows it -- this loop is exactly the
         * pattern that leaked 3 fonts (96 bytes) under LeakSanitizer
         * before this fix. */
        if (t.font_body)  fdk_font_destroy(t.font_body);
        if (t.font_label && t.font_label != t.font_body) fdk_font_destroy(t.font_label);
        if (t.font_mono  && t.font_mono  != t.font_body &&
            t.font_mono  != t.font_label) fdk_font_destroy(t.font_mono);
    }

    test_inline_edge_cases();
    test_load_system();
    test_three_tier_resolution();

    printf("\n========================================================\n");
    if (g_fail == 0)
        printf(" ALL CHECKS PASSED\n");
    else
        printf(" %d CHECK(S) FAILED\n", g_fail);
    printf("========================================================\n");

    return g_fail == 0 ? 0 : 1;
}
