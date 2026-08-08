/*
 * render_internal.h — FDK Render Backend (internal)
 *
 * Both the software and OpenGL backends implement this vtable.
 * The drawing API in fdk.h delegates to whichever backend is active.
 */
#ifndef FDK_RENDER_INTERNAL_H
#define FDK_RENDER_INTERNAL_H

#include "fdk/fdk.h"

typedef struct FDK_RenderCtx FDK_RenderCtx;

typedef struct {
    const char *name;

    /* Create/destroy a per-window render context */
    FDK_RenderCtx *(*ctx_create)(FDK_Window *win);
    void           (*ctx_destroy)(FDK_RenderCtx *ctx);

    /* Frame lifecycle */
    void (*begin_frame)(FDK_RenderCtx *ctx, int w, int h);
    void (*end_frame)(FDK_RenderCtx *ctx);

    /* Draw calls */
    void (*clear)(FDK_RenderCtx *ctx, FDK_Color color);
    void (*fill_rect)(FDK_RenderCtx *ctx, FDK_Rect r, FDK_Color color);
    void (*stroke_rect)(FDK_RenderCtx *ctx, FDK_Rect r,
                        FDK_Color color, int thickness);
    void (*fill_rect_rounded)(FDK_RenderCtx *ctx, FDK_Rect r,
                              int radius, FDK_Color color);
    /* Gradient-filled rounded rect — NULL means unsupported by backend */
    void (*fill_rect_gradient)(FDK_RenderCtx *ctx, FDK_Rect r, int radius,
                               const FDK_Gradient *gradient);
    /* Drop shadow behind a rect — NULL means unsupported */
    void (*draw_shadow)(FDK_RenderCtx *ctx, FDK_Rect r, int radius,
                        const FDK_Shadow *shadow);
    void (*fill_circle)(FDK_RenderCtx *ctx,
                        int cx, int cy, int radius, FDK_Color color);
    void (*draw_line)(FDK_RenderCtx *ctx,
                      int x0, int y0, int x1, int y1,
                      FDK_Color color, int thickness);

    /* Text — font is backend-agnostic (FreeType rasterises to alpha bitmap) */
    void     (*draw_text)(FDK_RenderCtx *ctx, FDK_Font *font,
                          const char *utf8, int x, int y, FDK_Color color);

    /* Scissor */
    void (*push_clip)(FDK_RenderCtx *ctx, FDK_Rect r);
    void (*pop_clip)(FDK_RenderCtx *ctx);

} FDK_RenderVTable;

extern const FDK_RenderVTable fdk_render_software;
extern const FDK_RenderVTable fdk_render_opengl;

extern const FDK_RenderVTable *fdk__render;

#endif /* FDK_RENDER_INTERNAL_H */
