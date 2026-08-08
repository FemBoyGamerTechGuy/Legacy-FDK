/*
 * core_internal.h — FDK_Window struct exposed to internal backends only.
 */
#ifndef FDK_CORE_INTERNAL_H
#define FDK_CORE_INTERNAL_H

#include "fdk/fdk.h"
#include "../platform/platform_internal.h"
#include "../render/render_internal.h"

struct FDK_Window {
    FDK_PlatformWindow *pw;
    FDK_RenderCtx      *rctx;
    int                 w, h;
};

FDK_PlatformWindow *fdk_window_get_pw(FDK_Window *win);

/* Get root-relative mouse position (for CSD resize tracking) */
void fdk__get_root_mouse(int *rx, int *ry);

/* The app_name passed to fdk_init() via FDK_InitInfo, stashed here so
 * fdk_ui_create() (in widgets/widget.c) can use it for tier-2 per-app
 * theme override lookups without changing fdk_ui_create()'s signature.
 * Returns "" if fdk_init() was never called or app_name was NULL. */
const char *fdk__get_app_name(void);

#endif /* FDK_CORE_INTERNAL_H */
