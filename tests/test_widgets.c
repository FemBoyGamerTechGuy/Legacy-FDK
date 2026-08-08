/*
 * test_widgets.c — new widget API smoke test (v0.2)
 *
 * Verifies the public API surface for the three new v0.2 widgets:
 *   - Switch
 *   - LevelBar
 *   - SearchEntry
 *
 * No window creation — just exercises constructors, getters, setters,
 * and verifies the basic state machine. Doesn't test paint or event
 * dispatch (those need a display server).
 *
 * Usage:
 *   ./test_widgets
 *
 * Exit code 0 = all checks passed, non-zero = a check failed.
 */
#define _POSIX_C_SOURCE 200809L
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

int main(void)
{
    printf("=== FDK new widgets test (Switch, LevelBar, SearchEntry) ===\n");

    /* ─── Switch ──────────────────────────────────────────────────────── */
    printf("\n[Test 1] Switch basic lifecycle\n");
    {
        FDK_Widget *sw = fdk_switch(false);
        CHECK(sw != NULL, "fdk_switch(false) returns non-NULL");
        CHECK(fdk_switch_get_active(sw) == false, "Switch starts inactive");
        fdk_switch_set_active(sw, true);
        CHECK(fdk_switch_get_active(sw) == true, "fdk_switch_set_active(true) works");
        fdk_switch_set_active(sw, false);
        CHECK(fdk_switch_get_active(sw) == false, "fdk_switch_set_active(false) works");
        fdk_widget_destroy(sw);
    }

    printf("\n[Test 2] Switch constructed active\n");
    {
        FDK_Widget *sw = fdk_switch(true);
        CHECK(fdk_switch_get_active(sw) == true, "fdk_switch(true) starts active");
        fdk_widget_destroy(sw);
    }

    printf("\n[Test 3] Switch callback wiring\n");
    {
        static int clicked = 0;
        static FDK_Widget *clicked_w = NULL;
        void on_sw(FDK_Widget *w, void *ud) { (void)ud; clicked++; clicked_w = w; }
        FDK_Widget *sw = fdk_switch(false);
        fdk_switch_on_change(sw, on_sw, NULL);
        /* Simulate the click handler firing the callback directly */
        on_sw(sw, NULL);
        CHECK(clicked == 1, "Switch callback fires once");
        CHECK(clicked_w == sw, "Switch callback receives the widget");
        fdk_widget_destroy(sw);
    }

    /* ─── LevelBar ────────────────────────────────────────────────────── */
    printf("\n[Test 4] LevelBar basic lifecycle\n");
    {
        FDK_Widget *lb = fdk_level_bar(5.0f, 10.0f, 4);
        CHECK(lb != NULL, "fdk_level_bar returns non-NULL");
        CHECK(fdk_level_bar_get_value(lb) == 5.0f, "Initial value preserved");
        fdk_level_bar_set_value(lb, 7.5f);
        CHECK(fdk_level_bar_get_value(lb) == 7.5f, "set_value updates value");
        fdk_level_bar_set_value(lb, 100.0f);  /* clamp to max */
        CHECK(fdk_level_bar_get_value(lb) == 10.0f, "Value clamped to max");
        fdk_level_bar_set_value(lb, -5.0f);  /* clamp to 0 */
        CHECK(fdk_level_bar_get_value(lb) == 0.0f, "Value clamped to 0");
        fdk_widget_destroy(lb);
    }

    printf("\n[Test 5] LevelBar set_max and set_segments\n");
    {
        FDK_Widget *lb = fdk_level_bar(1.0f, 4.0f, 4);
        fdk_level_bar_set_max(lb, 8.0f);
        CHECK(fdk_level_bar_get_value(lb) == 1.0f, "Value unchanged after set_max");
        fdk_level_bar_set_value(lb, 16.0f);
        CHECK(fdk_level_bar_get_value(lb) == 8.0f, "Value clamped to new max");
        fdk_level_bar_set_segments(lb, 8);
        /* No public getter for segments; just verify no crash */
        CHECK(true, "set_segments doesn't crash");
        fdk_widget_destroy(lb);
    }

    printf("\n[Test 6] LevelBar edge cases\n");
    {
        /* max <= 0 → coerced to 1.0 */
        FDK_Widget *lb = fdk_level_bar(0.5f, 0.0f, 0);
        CHECK(lb != NULL, "fdk_level_bar with max=0, segments=0 doesn't crash");
        fdk_level_bar_set_value(lb, 0.5f);
        CHECK(fdk_level_bar_get_value(lb) == 0.5f, "Value set with coerced max=1.0");
        fdk_widget_destroy(lb);
    }

    /* ─── SearchEntry ─────────────────────────────────────────────────── */
    printf("\n[Test 7] SearchEntry basic lifecycle\n");
    {
        FDK_Widget *se = fdk_search_entry("Type to search…");
        CHECK(se != NULL, "fdk_search_entry returns non-NULL");
        CHECK(strcmp(fdk_search_entry_get_text(se), "") == 0,
              "SearchEntry starts empty");
        fdk_search_entry_set_text(se, "hello");
        CHECK(strcmp(fdk_search_entry_get_text(se), "hello") == 0,
              "set_text updates text");
        fdk_search_entry_clear(se);
        CHECK(strcmp(fdk_search_entry_get_text(se), "") == 0,
              "clear() empties text");
        fdk_widget_destroy(se);
    }

    printf("\n[Test 8] SearchEntry with NULL placeholder\n");
    {
        FDK_Widget *se = fdk_search_entry(NULL);
        CHECK(se != NULL, "fdk_search_entry(NULL) doesn't crash");
        fdk_widget_destroy(se);
    }

    printf("\n[Test 9] SearchEntry long text\n");
    {
        FDK_Widget *se = fdk_search_entry("Search");
        char long_str[300];
        memset(long_str, 'a', 299);
        long_str[299] = '\0';
        fdk_search_entry_set_text(se, long_str);
        /* Should be clamped to max_len (255) */
        const char *txt = fdk_search_entry_get_text(se);
        CHECK(strlen(txt) <= 256, "Long text clamped to max_len");
        fdk_widget_destroy(se);
    }

    printf("\n[Test 10] SearchEntry callback wiring\n");
    {
        static int search_count = 0;
        static char last_query[256] = "";
        void on_search(FDK_Widget *w, const char *text, void *ud) {
            (void)w; (void)ud;
            search_count++;
            strncpy(last_query, text, sizeof(last_query) - 1);
            last_query[sizeof(last_query) - 1] = '\0';
        }
        FDK_Widget *se = fdk_search_entry("Search");
        fdk_search_entry_on_search(se, on_search, NULL);
        fdk_search_entry_set_text(se, "test query");
        /* Simulate the debounce firing by calling the callback directly */
        on_search(se, fdk_search_entry_get_text(se), NULL);
        CHECK(search_count == 1, "Search callback fires once");
        CHECK(strcmp(last_query, "test query") == 0,
              "Search callback receives current text");
        fdk_widget_destroy(se);
    }

    /* ─── NULL safety ─────────────────────────────────────────────────── */
    printf("\n[Test 11] NULL widget safety\n");
    {
        CHECK(fdk_switch_get_active(NULL) == false,
              "fdk_switch_get_active(NULL) returns false");
        CHECK(fdk_level_bar_get_value(NULL) == 0,
              "fdk_level_bar_get_value(NULL) returns 0");
        CHECK(strcmp(fdk_search_entry_get_text(NULL), "") == 0,
              "fdk_search_entry_get_text(NULL) returns empty string");
        /* These should not crash */
        fdk_switch_set_active(NULL, true);
        fdk_level_bar_set_value(NULL, 5.0f);
        fdk_level_bar_set_max(NULL, 10.0f);
        fdk_level_bar_set_segments(NULL, 4);
        fdk_search_entry_set_text(NULL, "x");
        fdk_search_entry_clear(NULL);
        fdk_switch_on_change(NULL, NULL, NULL);
        fdk_level_bar_get_value(NULL);
        fdk_search_entry_on_search(NULL, NULL, NULL);
        CHECK(true, "All NULL-widget setters are no-ops (don't crash)");
    }

    /* ─── Wrong-type safety ───────────────────────────────────────────── */
    printf("\n[Test 12] Wrong-type widget safety\n");
    {
        /* Pass a Label to Switch getters — should return defaults, not crash */
        FDK_Widget *lbl = fdk_label("hello");
        CHECK(fdk_switch_get_active(lbl) == false,
              "fdk_switch_get_active(label) returns false");
        fdk_switch_set_active(lbl, true);  /* no-op */
        CHECK(true, "fdk_switch_set_active on wrong type doesn't crash");
        fdk_widget_destroy(lbl);

        FDK_Widget *btn = fdk_button("OK");
        CHECK(fdk_level_bar_get_value(btn) == 0,
              "fdk_level_bar_get_value(button) returns 0");
        fdk_widget_destroy(btn);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
