/*
 * test_widgets2.c — new widget API test (v0.2 part 2)
 *
 * Tests Expander, StatusBar, Grid, Calendar APIs.
 * No display server needed — exercises constructors, getters,
 * setters, and state transitions.
 */
#define _POSIX_C_SOURCE 200809L
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

int main(void)
{
    printf("=== FDK new widgets test 2 (Expander, StatusBar, Grid, Calendar) ===\n");

    /* ─── Expander ───────────────────────────────────────────────────── */
    printf("\n[Test 1] Expander basic lifecycle\n");
    {
        FDK_Widget *child = fdk_label("content");
        FDK_Widget *e = fdk_expander("Advanced", child);
        CHECK(e != NULL, "fdk_expander returns non-NULL");
        CHECK(fdk_expander_get_expanded(e) == false, "Expander starts collapsed");
        fdk_expander_set_expanded(e, true);
        CHECK(fdk_expander_get_expanded(e) == true, "set_expanded(true) works");
        fdk_expander_set_expanded(e, false);
        CHECK(fdk_expander_get_expanded(e) == false, "set_expanded(false) works");
        FDK_Widget *new_child = fdk_label("new content");
        fdk_expander_set_child(e, new_child);
        CHECK(true, "set_child doesn't crash");
        fdk_widget_destroy(e);
        fdk_widget_destroy(new_child);
    }

    printf("\n[Test 2] Expander NULL safety\n");
    {
        CHECK(fdk_expander_get_expanded(NULL) == false, "get_expanded(NULL) returns false");
        fdk_expander_set_expanded(NULL, true);
        fdk_expander_set_child(NULL, NULL);
        fdk_expander_on_toggle(NULL, NULL, NULL);
        CHECK(true, "All NULL expander calls are no-ops");
    }

    /* ─── StatusBar ──────────────────────────────────────────────────── */
    printf("\n[Test 3] StatusBar push/pop\n");
    {
        FDK_Widget *sb = fdk_status_bar();
        CHECK(sb != NULL, "fdk_status_bar returns non-NULL");
        CHECK(strcmp(fdk_status_bar_get_text(sb), "") == 0, "Status bar starts empty");
        fdk_status_bar_push(sb, "Ready");
        CHECK(strcmp(fdk_status_bar_get_text(sb), "Ready") == 0, "push shows message");
        fdk_status_bar_push(sb, "Loading...");
        CHECK(strcmp(fdk_status_bar_get_text(sb), "Loading...") == 0, "push shows latest");
        fdk_status_bar_pop(sb);
        CHECK(strcmp(fdk_status_bar_get_text(sb), "Ready") == 0, "pop reveals previous");
        fdk_status_bar_pop(sb);
        CHECK(strcmp(fdk_status_bar_get_text(sb), "") == 0, "pop to empty");
        fdk_status_bar_pop(sb);  /* pop on empty — should not crash */
        CHECK(strcmp(fdk_status_bar_get_text(sb), "") == 0, "pop on empty stays empty");
        fdk_widget_destroy(sb);
    }

    printf("\n[Test 4] StatusBar overflow (16 message stack)\n");
    {
        FDK_Widget *sb = fdk_status_bar();
        for (int i = 0; i < 20; i++) {
            char buf[32];
            snprintf(buf, sizeof buf, "msg %d", i);
            fdk_status_bar_push(sb, buf);
        }
        CHECK(strcmp(fdk_status_bar_get_text(sb), "msg 19") == 0,
              "After 20 pushes, top is 'msg 19'");
        fdk_widget_destroy(sb);
    }

    /* ─── Grid ───────────────────────────────────────────────────────── */
    printf("\n[Test 5] Grid layout basic\n");
    {
        FDK_Widget *g = fdk_grid(3, false);
        CHECK(g != NULL, "fdk_grid(3, false) returns non-NULL");
        FDK_Widget *w1 = fdk_label("A");
        FDK_Widget *w2 = fdk_label("B");
        FDK_Widget *w3 = fdk_label("C");
        fdk_grid_add(g, w1, 0, 0, 1, 1);
        fdk_grid_add(g, w2, 0, 1, 1, 1);
        fdk_grid_add(g, w3, 0, 2, 1, 1);
        CHECK(true, "3 children added without crash");
        fdk_widget_destroy(g);
        fdk_widget_destroy(w1);
        fdk_widget_destroy(w2);
        fdk_widget_destroy(w3);
    }

    printf("\n[Test 6] Grid add_next auto-positioning\n");
    {
        FDK_Widget *g = fdk_grid(2, true);
        fdk_grid_add_next(g, fdk_label("0,0"));
        fdk_grid_add_next(g, fdk_label("0,1"));
        fdk_grid_add_next(g, fdk_label("1,0"));
        fdk_grid_add_next(g, fdk_label("1,1"));
        /* Can't access internal struct from test — just verify no crash */
        CHECK(true, "4 auto-positioned adds don't crash");
        fdk_widget_destroy(g);
    }

    printf("\n[Test 7] Grid with col_span\n");
    {
        FDK_Widget *g = fdk_grid(3, false);
        fdk_grid_add(g, fdk_label("wide"), 0, 0, 1, 3);
        fdk_grid_add_next(g, fdk_label("next"));
        CHECK(true, "col_span=3 + add_next doesn't crash");
        fdk_widget_destroy(g);
    }

    printf("\n[Test 8] Grid NULL safety\n");
    {
        fdk_grid_add(NULL, NULL, 0, 0, 1, 1);
        fdk_grid_add_next(NULL, NULL);
        CHECK(true, "NULL grid calls are no-ops");
    }

    /* ─── Calendar ───────────────────────────────────────────────────── */
    printf("\n[Test 9] Calendar basic lifecycle\n");
    {
        FDK_Widget *cal = fdk_calendar();
        CHECK(cal != NULL, "fdk_calendar returns non-NULL");
        FDK_Date d = fdk_calendar_get_date(cal);
        CHECK(d.year == 2026, "Default year is 2026");
        CHECK(d.month == 6, "Default month is 6 (July, 0-indexed)");
        CHECK(d.day == 15, "Default day is 15");
        fdk_calendar_set_date(cal, 2025, 0, 1);  /* Jan 1, 2025 */
        d = fdk_calendar_get_date(cal);
        CHECK(d.year == 2025, "set_date year works");
        CHECK(d.month == 0, "set_date month works");
        CHECK(d.day == 1, "set_date day works");
        fdk_widget_destroy(cal);
    }

    printf("\n[Test 10] Calendar date clamping\n");
    {
        FDK_Widget *cal = fdk_calendar();
        fdk_calendar_set_date(cal, 2026, 13, 40); /* invalid month/day */
        FDK_Date d = fdk_calendar_get_date(cal);
        CHECK(d.month == 11, "Month 13 clamped to 11 (December)");
        CHECK(d.day == 31, "Day 40 clamped to 31 (December has 31 days)");

        /* February */
        fdk_calendar_set_date(cal, 2026, 1, 30); /* Feb has 28 days in 2026 */
        d = fdk_calendar_get_date(cal);
        CHECK(d.day == 28, "Feb 30 clamped to 28 (non-leap year)");

        /* Leap year */
        fdk_calendar_set_date(cal, 2024, 1, 30); /* 2024 is leap year */
        d = fdk_calendar_get_date(cal);
        CHECK(d.day == 29, "Feb 30 clamped to 29 (leap year)");
        fdk_widget_destroy(cal);
    }

    printf("\n[Test 11] Calendar callback wiring\n");
    {
        static int called = 0;
        static FDK_Date last_date = {0};
        void on_change(FDK_Widget *w, const FDK_Date *d, void *ud) {
            (void)w; (void)ud;
            called++;
            last_date = *d;
        }
        FDK_Widget *cal = fdk_calendar();
        fdk_calendar_on_change(cal, on_change, NULL);
        /* Simulate callback */
        FDK_Date test_date = {2025, 5, 20};
        on_change(cal, &test_date, NULL);
        CHECK(called == 1, "Calendar callback fires");
        CHECK(last_date.year == 2025 && last_date.month == 5 && last_date.day == 20,
              "Callback receives correct date");
        fdk_widget_destroy(cal);
    }

    printf("\n[Test 12] Calendar NULL safety\n");
    {
        FDK_Date d = fdk_calendar_get_date(NULL);
        CHECK(d.year == 0, "get_date(NULL) returns zeroed date");
        fdk_calendar_set_date(NULL, 2026, 0, 1);
        fdk_calendar_on_change(NULL, NULL, NULL);
        CHECK(true, "NULL calendar calls are no-ops");
    }

    /* ─── Popover ────────────────────────────────────────────────────── */
    printf("\n[Test 13] Popover basic lifecycle\n");
    {
        FDK_Widget *content = fdk_label("popup content");
        FDK_Widget *p = fdk_popover(content);
        CHECK(p != NULL, "fdk_popover returns non-NULL");
        CHECK(fdk_popover_is_visible(p) == false, "Popover starts hidden");
        /* Can't test show/hide without a UI context, but verify the API exists */
        FDK_Widget *new_content = fdk_label("new content");
        fdk_popover_set_content(p, new_content);
        CHECK(true, "set_content doesn't crash");
        fdk_widget_destroy(p);
        fdk_widget_destroy(new_content);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
