/*
 * software.c — FDK software (CPU) render backend
 *
 * Renders into a uint32_t pixel buffer (XRGB8888).
 * The platform backend owns the buffer; we ask for it via
 * window_get_framebuffer() and write into it each frame.
 *
 * Implements:
 *   - Alpha-blended filled/stroked rectangles (including rounded corners)
 *   - Filled circles (anti-aliased via coverage)
 *   - Lines (Bresenham with thickness)
 *   - Text via FreeType (FTL-licensed) — renders glyphs to alpha bitmaps
 *     and alpha-blends them into the pixel buffer
 *   - Scissor clip stack
 */
#include "render_internal.h"
#include "../core/core_internal.h"
#include "../platform/platform_internal.h"
#include "shape.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include "font_internal.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* ─── UTF-8 decoder ──────────────────────────────────────────────────────────
 * Decodes one codepoint from *p, advances *p past the consumed bytes.
 * Returns 0xFFFD on invalid sequences.
 *
 * Still used by callers that need to walk UTF-8 codepoint-by-codepoint
 * without shaping (rare); the shaping path uses utf8_to_utf32() in
 * shape.c instead.
 */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;

    if (s[0] < 0x80)        { cp = s[0];               extra = 0; }
    else if (s[0] < 0xC0)   { cp = 0xFFFD;             extra = 0; } /* stray continuation */
    else if (s[0] < 0xE0)   { cp = s[0] & 0x1F;        extra = 1; }
    else if (s[0] < 0xF0)   { cp = s[0] & 0x0F;        extra = 2; }
    else                    { cp = s[0] & 0x07;        extra = 3; }

    for (int i = 0; i < extra; i++) {
        if ((s[i+1] & 0xC0) != 0x80) { cp = 0xFFFD; extra = i; break; }
        cp = (cp << 6) | (s[i+1] & 0x3F);
    }

    *p += 1 + extra;
    return cp;
}


/* ─── Font (shared between render backends) ──────────────────────────────── */


static FT_Library s_ft = NULL;
static int        s_ft_refs = 0;

static bool ensure_ft(void)
{
    if (s_ft) { s_ft_refs++; return true; }
    if (FT_Init_FreeType(&s_ft)) return false;
    s_ft_refs = 1;
    return true;
}

static void release_ft(void)
{
    if (--s_ft_refs == 0) {
        FT_Done_FreeType(s_ft);
        s_ft = NULL;
    }
}

FDK_Font *fdk_font_load(const char *path, float size_px)
{
    if (!ensure_ft()) return NULL;
    FDK_Font *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    if (FT_New_Face(s_ft, path, 0, &f->face)) { free(f); return NULL; }
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)size_px);
    f->size_px = size_px;
    fdk_font_compute_metrics(f);
    fdk__font_init_shaping(f);
    return f;
}

FDK_Font *fdk_font_load_memory(const uint8_t *data, size_t len, float size_px)
{
    if (!ensure_ft()) return NULL;
    FDK_Font *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    if (FT_New_Memory_Face(s_ft, data, (FT_Long)len, 0, &f->face)) {
        free(f); return NULL;
    }
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)size_px);
    f->size_px = size_px;
    fdk_font_compute_metrics(f);
    fdk__font_init_shaping(f);
    return f;
}

void fdk_font_destroy(FDK_Font *f)
{
    if (!f) return;
    fdk__font_fini_shaping(f);
    FT_Done_Face(f->face);
    release_ft();
    free(f);
}

FDK_Size fdk_measure_text(FDK_Font *font, const char *utf8)
{
    if (!font || !utf8) return (FDK_Size){0,0};
    /* Use the shaping path so measurement matches rendering for
     * complex scripts (Arabic, Indic, ligatures, BiDi). */
    int advance = fdk__shape_measure(font, utf8, -1);
    /* Use real ascender+descender for height — no more approximations */
    return (FDK_Size){ advance, font->ascender + font->descender };
}

/* ─── Render context ─────────────────────────────────────────────────────── */
#define CLIP_STACK_MAX 32

struct FDK_RenderCtx {
    FDK_Window *win;
    uint32_t   *pixels;
    int         stride_px;
    int         w, h;

    FDK_Rect    clip_stack[CLIP_STACK_MAX];
    int         clip_depth;
    FDK_Rect    clip;
};

/* ─── Pixel helpers ──────────────────────────────────────────────────────── */
static inline uint32_t color_to_xrgb(FDK_Color c)
{
    return ((uint32_t)c.r << 16) |
           ((uint32_t)c.g <<  8) |
            (uint32_t)c.b;
}

/* Alpha blend src over dst (both XRGB8888, src has FDK_Color.a) */
static inline uint32_t blend(uint32_t dst, FDK_Color src)
{
    if (src.a == 255)
        return color_to_xrgb(src);
    if (src.a == 0)
        return dst;

    uint32_t a  = src.a;
    uint32_t ia = 255 - a;

    uint32_t dr = (dst >> 16) & 0xff;
    uint32_t dg = (dst >>  8) & 0xff;
    uint32_t db =  dst        & 0xff;

    uint32_t r = (src.r * a + dr * ia) / 255;
    uint32_t g = (src.g * a + dg * ia) / 255;
    uint32_t b = (src.b * a + db * ia) / 255;

    return (r << 16) | (g << 8) | b;
}

/* Plot a single pixel, clip-tested */
static inline void plot(FDK_RenderCtx *ctx, int x, int y, FDK_Color c)
{
    if (!ctx->pixels) return;
    if (x < ctx->clip.x || y < ctx->clip.y ||
        x >= ctx->clip.x + ctx->clip.w ||
        y >= ctx->clip.y + ctx->clip.h)
        return;
    uint32_t *px = ctx->pixels + y * ctx->stride_px + x;
    *px = blend(*px, c);
}

/* ─── Context lifecycle ──────────────────────────────────────────────────── */
static FDK_RenderCtx *sw_ctx_create(FDK_Window *win)
{
    FDK_RenderCtx *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return NULL;
    ctx->win = win;
    return ctx;
}

static void sw_ctx_destroy(FDK_RenderCtx *ctx) { free(ctx); }

static void sw_begin_frame(FDK_RenderCtx *ctx, int w, int h)
{
    if (w <= 0 || h <= 0) { ctx->pixels = NULL; ctx->w = 0; ctx->h = 0; return; }
    ctx->pixels = fdk__platform->window_get_framebuffer(
                      fdk_window_get_pw(ctx->win), &ctx->stride_px);
    ctx->w = w;
    ctx->h = h;
    ctx->clip_depth = 0;
    ctx->clip = (FDK_Rect){ 0, 0, w, h };
}

static void sw_end_frame(FDK_RenderCtx *ctx)
{
    fdk__platform->window_present(fdk_window_get_pw(ctx->win));
}

/* ─── Clip stack ─────────────────────────────────────────────────────────── */
static FDK_Rect rect_intersect(FDK_Rect a, FDK_Rect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = (a.x+a.w) < (b.x+b.w) ? (a.x+a.w) : (b.x+b.w);
    int y1 = (a.y+a.h) < (b.y+b.h) ? (a.y+a.h) : (b.y+b.h);
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    return (FDK_Rect){ x0, y0, x1-x0, y1-y0 };
}

static void sw_push_clip(FDK_RenderCtx *ctx, FDK_Rect r)
{
    if (ctx->clip_depth < CLIP_STACK_MAX)
        ctx->clip_stack[ctx->clip_depth++] = ctx->clip;
    ctx->clip = rect_intersect(ctx->clip, r);
}

static void sw_pop_clip(FDK_RenderCtx *ctx)
{
    if (ctx->clip_depth > 0)
        ctx->clip = ctx->clip_stack[--ctx->clip_depth];
    else
        ctx->clip = (FDK_Rect){ 0, 0, ctx->w, ctx->h };
}

/* ─── Clear ──────────────────────────────────────────────────────────────── */
static void sw_clear(FDK_RenderCtx *ctx, FDK_Color c)
{
    if (!ctx->pixels) return;
    uint32_t px = color_to_xrgb(c);
    for (int y = 0; y < ctx->h; y++) {
        uint32_t *row = ctx->pixels + y * ctx->stride_px;
        for (int x = 0; x < ctx->w; x++)
            row[x] = px;
    }
}

/* ─── Filled rectangle ───────────────────────────────────────────────────── */
static void sw_fill_rect(FDK_RenderCtx *ctx, FDK_Rect r, FDK_Color c)
{
    FDK_Rect cr = rect_intersect(ctx->clip, r);
    if (cr.w <= 0 || cr.h <= 0) return;

    if (c.a == 255) {
        uint32_t px = color_to_xrgb(c);
        for (int y = cr.y; y < cr.y + cr.h; y++) {
            uint32_t *row = ctx->pixels + y * ctx->stride_px + cr.x;
            for (int x = 0; x < cr.w; x++) row[x] = px;
        }
    } else {
        for (int y = cr.y; y < cr.y + cr.h; y++)
            for (int x = cr.x; x < cr.x + cr.w; x++)
                plot(ctx, x, y, c);
    }
}

/* ─── Stroked rectangle ──────────────────────────────────────────────────── */
static void sw_stroke_rect(FDK_RenderCtx *ctx, FDK_Rect r,
                            FDK_Color c, int t)
{
    sw_fill_rect(ctx, (FDK_Rect){ r.x,          r.y,          r.w, t }, c);
    sw_fill_rect(ctx, (FDK_Rect){ r.x,          r.y+r.h-t,    r.w, t }, c);
    sw_fill_rect(ctx, (FDK_Rect){ r.x,          r.y+t,        t, r.h-2*t }, c);
    sw_fill_rect(ctx, (FDK_Rect){ r.x+r.w-t,    r.y+t,        t, r.h-2*t }, c);
}

/* ─── Rounded rectangle ──────────────────────────────────────────────────── */
/*
 * We draw rounded corners by testing each pixel against the circle
 * at each corner. Fast enough for typical UI sizes.
 */
static void sw_fill_rect_rounded(FDK_RenderCtx *ctx,
                                  FDK_Rect r, int rad, FDK_Color c)
{
    if (rad <= 0) { sw_fill_rect(ctx, r, c); return; }
    if (rad > r.w/2) rad = r.w/2;
    if (rad > r.h/2) rad = r.h/2;

    FDK_Rect cr = rect_intersect(ctx->clip, r);
    float r2 = (float)(rad * rad);

    for (int y = cr.y; y < cr.y + cr.h; y++) {
        for (int x = cr.x; x < cr.x + cr.w; x++) {
            int lx = x - r.x, ly = y - r.y;
            bool in = true;

            /* Top-left corner */
            if (lx < rad && ly < rad) {
                float dx = lx - rad + 0.5f;
                float dy = ly - rad + 0.5f;
                in = (dx*dx + dy*dy) <= r2;
            }
            /* Top-right corner */
            else if (lx >= r.w-rad && ly < rad) {
                float dx = lx - (r.w-rad) + 0.5f;
                float dy = ly - rad + 0.5f;
                in = (dx*dx + dy*dy) <= r2;
            }
            /* Bottom-left corner */
            else if (lx < rad && ly >= r.h-rad) {
                float dx = lx - rad + 0.5f;
                float dy = ly - (r.h-rad) + 0.5f;
                in = (dx*dx + dy*dy) <= r2;
            }
            /* Bottom-right corner */
            else if (lx >= r.w-rad && ly >= r.h-rad) {
                float dx = lx - (r.w-rad) + 0.5f;
                float dy = ly - (r.h-rad) + 0.5f;
                in = (dx*dx + dy*dy) <= r2;
            }

            if (in) plot(ctx, x, y, c);
        }
    }
}

/* ─── Filled circle ──────────────────────────────────────────────────────── */
static void sw_fill_circle(FDK_RenderCtx *ctx,
                            int cx, int cy, int rad, FDK_Color c)
{
    int x0 = cx - rad, x1 = cx + rad;
    int y0 = cy - rad, y1 = cy + rad;
    float r2 = (float)(rad * rad);

    for (int y = y0; y <= y1; y++) {
        float dy = y - cy + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx + 0.5f;
            if (dx*dx + dy*dy <= r2)
                plot(ctx, x, y, c);
        }
    }
}

/* ─── Line (Bresenham) ───────────────────────────────────────────────────── */
static void sw_draw_line(FDK_RenderCtx *ctx,
                          int x0, int y0, int x1, int y1,
                          FDK_Color c, int thickness)
{
    int dx  =  abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy  = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
    int err = dx + dy;
    int t2  = thickness / 2;

    for (;;) {
        if (thickness <= 1) {
            plot(ctx, x0, y0, c);
        } else {
            for (int ty = -t2; ty <= t2; ty++)
                for (int tx = -t2; tx <= t2; tx++)
                    plot(ctx, x0+tx, y0+ty, c);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ─── Text rendering via FreeType + HarfBuzz shaping ──────────────────────── */
static void sw_draw_text(FDK_RenderCtx *ctx,
                          FDK_Font *font,
                          const char *utf8,
                          int x, int y,
                          FDK_Color c)
{
    if (!font || !utf8) return;
    FT_Face face = font->face;

    /* Shape the text. Without HarfBuzz this is a codepoint-by-codepoint
     * fallback (v0.1.0 behaviour). With HarfBuzz, complex scripts shape
     * correctly. Without FriBidi, no BiDi reordering. */
    FDK_GlyphInfo *glyphs = NULL;
    int n_glyphs = fdk__shape_text(font, utf8, -1, &glyphs);
    if (n_glyphs < 0) return;
    if (n_glyphs == 0) { free(glyphs); return; }

    int pen_x    = x;
    int baseline = y + font->ascender;

    for (int i = 0; i < n_glyphs; i++) {
        /* FT_Load_Glyph uses the glyph ID returned by HarfBuzz (or by
         * FT_Get_Char_Index in the fallback path). FT_LOAD_RENDER asks
         * FreeType to rasterise to a bitmap. */
        if (FT_Load_Glyph(face, glyphs[i].glyph_id, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        FT_Bitmap   *bm = &g->bitmap;

        /* Position: pen_x accumulates x_advance; add per-glyph
         * x_offset (HarfBuzz) and the glyph's bitmap_left bearing.
         * y_offset is rare (vertical scripts, mark positioning) but
         * we honour it when present. */
        int bx = pen_x + (glyphs[i].x_offset >> 6) + g->bitmap_left;
        int by = baseline - (glyphs[i].y_offset >> 6) - g->bitmap_top;

        for (int row = 0; row < (int)bm->rows; row++) {
            for (int col = 0; col < (int)bm->width; col++) {
                uint8_t alpha = bm->buffer[row * bm->pitch + col];
                if (alpha == 0) continue;
                FDK_Color gc = c;
                gc.a = (uint8_t)((alpha * c.a) / 255);
                plot(ctx, bx + col, by + row, gc);
            }
        }

        pen_x += (glyphs[i].x_advance >> 6);
    }

    free(glyphs);
}

/* ─── Gradient fill (linear_v / linear_h) ────────────────────────────────── */
static FDK_Color grad_sample(const FDK_Gradient *g, float t)
{
    /* Clamp */
    if (t <= g->stops[0].pos)               return g->stops[0].color;
    if (t >= g->stops[g->stop_count-1].pos) return g->stops[g->stop_count-1].color;

    for (int i = 0; i < g->stop_count - 1; i++) {
        float a = g->stops[i].pos, b = g->stops[i+1].pos;
        if (t >= a && t <= b) {
            float f = (b > a) ? (t - a) / (b - a) : 0.f;
            FDK_Color ca = g->stops[i].color;
            FDK_Color cb = g->stops[i+1].color;
            return (FDK_Color){
                (uint8_t)(ca.r + f*(cb.r - ca.r)),
                (uint8_t)(ca.g + f*(cb.g - ca.g)),
                (uint8_t)(ca.b + f*(cb.b - ca.b)),
                (uint8_t)(ca.a + f*(cb.a - ca.a)),
            };
        }
    }
    return g->stops[g->stop_count-1].color;
}

static void sw_fill_rect_gradient(FDK_RenderCtx *ctx,
                                   FDK_Rect r, int rad,
                                   const FDK_Gradient *g)
{
    if (!g || g->stop_count < 2 || g->type == FDK_GRAD_NONE) return;
    if (rad > r.w/2) rad = r.w/2;
    if (rad > r.h/2) rad = r.h/2;

    FDK_Rect cr = rect_intersect(ctx->clip, r);
    float r2 = (float)(rad * rad);
    bool use_rad = (rad > 0);

    for (int y = cr.y; y < cr.y + cr.h; y++) {
        float t = (g->type == FDK_GRAD_LINEAR_V)
                ? (r.h > 1 ? (float)(y - r.y) / (float)(r.h - 1) : 0.f)
                : 0.f;  /* horizontal handled per-x below */
        FDK_Color row_c = (g->type == FDK_GRAD_LINEAR_V) ? grad_sample(g, t)
                                                           : (FDK_Color){0};
        for (int x = cr.x; x < cr.x + cr.w; x++) {
            FDK_Color c;
            if (g->type == FDK_GRAD_LINEAR_H) {
                float tx = r.w > 1 ? (float)(x - r.x) / (float)(r.w - 1) : 0.f;
                c = grad_sample(g, tx);
            } else {
                c = row_c;
            }

            if (use_rad) {
                int lx = x - r.x, ly = y - r.y;
                bool in = true;
                if      (lx < rad && ly < rad) {
                    float dx=lx-rad+.5f, dy=ly-rad+.5f;
                    in = dx*dx+dy*dy <= r2;
                } else if (lx >= r.w-rad && ly < rad) {
                    float dx=lx-(r.w-rad)+.5f, dy=ly-rad+.5f;
                    in = dx*dx+dy*dy <= r2;
                } else if (lx < rad && ly >= r.h-rad) {
                    float dx=lx-rad+.5f, dy=ly-(r.h-rad)+.5f;
                    in = dx*dx+dy*dy <= r2;
                } else if (lx >= r.w-rad && ly >= r.h-rad) {
                    float dx=lx-(r.w-rad)+.5f, dy=ly-(r.h-rad)+.5f;
                    in = dx*dx+dy*dy <= r2;
                }
                if (!in) continue;
            }
            plot(ctx, x, y, c);
        }
    }
}

/* ─── Box shadow (3-pass approximated Gaussian) ──────────────────────────── */
static void sw_draw_shadow(FDK_RenderCtx *ctx,
                            FDK_Rect r, int radius,
                            const FDK_Shadow *s)
{
    if (!s || !s->enabled || s->blur <= 0) return;

    int b      = s->blur;
    int sx     = r.x + s->offset_x - b;
    int sy_top = r.y + s->offset_y - b;
    int sw     = r.w + b * 2;
    int sh     = r.h + b * 2;

    /* Allocate a small alpha buffer for the shadow */
    int buf_w = sw + b * 2, buf_h = sh + b * 2;
    if (buf_w <= 0 || buf_h <= 0) return;
    float *alpha = calloc((size_t)(buf_w * buf_h), sizeof(float));
    if (!alpha) return;

    /* Stamp a filled rect into the alpha buffer */
    for (int y = b; y < sh - b; y++)
        for (int x = b; x < sw - b; x++)
            alpha[y * buf_w + x] = 1.f;

    /* 3-pass box blur approximation of Gaussian */
    float *tmp = calloc((size_t)(buf_w * buf_h), sizeof(float));
    if (!tmp) { free(alpha); return; }

    for (int pass = 0; pass < 3; pass++) {
        /* Horizontal blur */
        for (int y = 0; y < buf_h; y++) {
            float sum = 0;
            for (int x = -b; x < buf_w; x++) {
                if (x + b < buf_w) sum += alpha[y*buf_w + x + b];
                if (x >= 0 && x - b - 1 >= 0) sum -= alpha[y*buf_w + x - b - 1];
                if (x >= 0) tmp[y*buf_w + x] = sum / (2*b + 1);
            }
        }
        /* Vertical blur */
        for (int x = 0; x < buf_w; x++) {
            float sum = 0;
            for (int y = -b; y < buf_h; y++) {
                if (y + b < buf_h) sum += tmp[(y+b)*buf_w + x];
                if (y - b - 1 >= 0) sum -= tmp[(y-b-1)*buf_w + x];
                if (y >= 0) alpha[y*buf_w + x] = sum / (2*b + 1);
            }
        }
    }

    /* Blit alpha buffer as shadow pixels */
    FDK_Color sc = s->color;
    for (int y = 0; y < buf_h; y++) {
        for (int x = 0; x < buf_w; x++) {
            float a = alpha[y * buf_w + x];
            if (a < 0.004f) continue;
            FDK_Color px = sc;
            px.a = (uint8_t)(sc.a * a);
            plot(ctx, sx - b + x, sy_top - b + y, px);
        }
    }

    free(alpha);
    free(tmp);
}

/* ─── VTable ─────────────────────────────────────────────────────────────── */
const FDK_RenderVTable fdk_render_software = {
    .name                = "software",
    .ctx_create          = sw_ctx_create,
    .ctx_destroy         = sw_ctx_destroy,
    .begin_frame         = sw_begin_frame,
    .end_frame           = sw_end_frame,
    .clear               = sw_clear,
    .fill_rect           = sw_fill_rect,
    .stroke_rect         = sw_stroke_rect,
    .fill_rect_rounded   = sw_fill_rect_rounded,
    .fill_rect_gradient  = sw_fill_rect_gradient,
    .draw_shadow         = sw_draw_shadow,
    .fill_circle         = sw_fill_circle,
    .draw_line           = sw_draw_line,
    .draw_text           = sw_draw_text,
    .push_clip           = sw_push_clip,
    .pop_clip            = sw_pop_clip,
};
