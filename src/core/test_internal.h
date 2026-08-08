/*
 * test_internal.h — introspection hooks for the headless test suite only.
 *
 * NOT part of the public API: not declared in any include/fdk header,
 * not installed, not meant to be called from application code. Application
 * code that needs to react to a theme change should use
 * fdk_ui_set_theme()'s effect on rendering, not peek at FDK_UI
 * internals directly.
 *
 * Exists so tests/test_watch_multiwindow.c can verify, from outside
 * theme.c and widget.c's private structs, that:
 *   - each FDK_UI gets its own independent watch (the g_watches list
 *     fix — see theme.c), rather than all windows fighting over one
 *     process-global watch, and
 *   - fdk_ui_destroy() actually tears down that ui's watch rather than
 *     leaving a dangling ui pointer for a live watch thread to later
 *     dereference.
 */
#ifndef FDK_TEST_INTERNAL_H
#define FDK_TEST_INTERNAL_H

#include "fdk/fdk_widget.h"
#include <stddef.h>
#include <stdbool.h>

/* Implemented in theme.c. Number of currently-live theme watches
 * (one per FDK_UI with an active fdk_theme_watch/watch_conf). */
size_t fdk__theme_watch_count(void);

/* Implemented in theme.c. True if `ui` currently has a live watch
 * registered against it. */
bool fdk__theme_watch_exists(FDK_UI *ui);

/* Implemented in widget.c. Copies ui's current theme into *out under
 * the same lock fdk_ui_set_theme() uses, so it's race-free against a
 * live watch thread. Returns false (leaves *out untouched) if ui or
 * out is NULL. */
bool fdk__ui_copy_theme(FDK_UI *ui, FDK_Theme *out);

#endif /* FDK_TEST_INTERNAL_H */
