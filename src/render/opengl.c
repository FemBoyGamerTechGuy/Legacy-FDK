/*
 * opengl.c — FDK OpenGL 3.3 core profile render backend
 *
 * Strategy:
 *   - All draw calls accumulate vertices into a CPU-side batch buffer.
 *   - end_frame() uploads the batch in one glBufferSubData call and draws.
 *   - Text glyphs are cached in a texture atlas (512×512 greyscale).
 *   - One shader pair handles everything: rects, circles, text.
 *
 * No GLEW — we load function pointers manually via GLX/EGL.
 * No helper libraries — pure OpenGL 3.3 core profile.
 */
#include "render_internal.h"
#include "../core/core_internal.h"
#include "../platform/platform_internal.h"
#include "shape.h"

#ifdef FDK_HAVE_OPENGL

#include <ft2build.h>
#include FT_FREETYPE_H
#include "font_internal.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ─── UTF-8 decoder (still used by callers that don't go through shaping) ─── */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;
    if (s[0] < 0x80)      { cp = s[0];        extra = 0; }
    else if (s[0] < 0xC0) { cp = 0xFFFD;      extra = 0; }
    else if (s[0] < 0xE0) { cp = s[0] & 0x1F; extra = 1; }
    else if (s[0] < 0xF0) { cp = s[0] & 0x0F; extra = 2; }
    else                  { cp = s[0] & 0x07;  extra = 3; }
    for (int i = 0; i < extra; i++) {
        if ((s[i+1] & 0xC0) != 0x80) { cp = 0xFFFD; extra = i; break; }
        cp = (cp << 6) | (s[i+1] & 0x3F);
    }
    *p += 1 + extra;
    return cp;
}


/* ─── Glyph atlas ────────────────────────────────────────────────────────── */
#define ATLAS_W  512
#define ATLAS_H  512
#define MAX_GLYPHS 256

typedef struct {
    uint32_t glyph_id;   /* FreeType glyph index (key) — was 'codepoint' in v0.1 */
    float    u0, v0, u1, v1;    /* UV in [0..1] */
    int      bearing_x, bearing_y;
    int      advance;
    int      w, h;
} GlyphEntry;

/* ─── Batch vertex ───────────────────────────────────────────────────────── */
typedef struct {
    float x, y;
    float u, v;        /* 0,0 = solid colour; set for text glyphs */
    float r, g, b, a;
    float mode;        /* 0 = solid, 1 = textured alpha */
} Vertex;

#define MAX_VERTS (65536 * 4)

/* ─── Render context ─────────────────────────────────────────────────────── */
struct FDK_RenderCtx {
    FDK_Window *win;
    int w, h;

    /* OpenGL objects */
    GLuint vao, vbo;
    GLuint shader;
    GLuint atlas_tex;

    /* Batch */
    Vertex  *batch;
    int      batch_count;  /* in vertices */

    /* Glyph cache */
    GlyphEntry glyphs[MAX_GLYPHS];
    int        glyph_count;
    int        atlas_pen_x, atlas_pen_y, atlas_row_h;

    /* Clip stack */
#define CLIP_STACK_MAX 32
    FDK_Rect clip_stack[CLIP_STACK_MAX];
    int      clip_depth;
    FDK_Rect clip;
};

/* ─── Shaders ────────────────────────────────────────────────────────────── */
static const char *s_vert_src =
"#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec4 aColor;\n"
"layout(location=3) in float aMode;\n"
"uniform vec2 uScreenSize;\n"
"out vec2 vUV;\n"
"out vec4 vColor;\n"
"out float vMode;\n"
"void main() {\n"
"    vec2 ndc = aPos / uScreenSize * 2.0 - 1.0;\n"
"    ndc.y = -ndc.y;\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"    vUV    = aUV;\n"
"    vColor = aColor;\n"
"    vMode  = aMode;\n"
"}\n";

static const char *s_frag_src =
"#version 330 core\n"
"in vec2  vUV;\n"
"in vec4  vColor;\n"
"in float vMode;\n"
"uniform sampler2D uAtlas;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    if (vMode > 0.5) {\n"
"        float a = texture(uAtlas, vUV).r;\n"
"        fragColor = vec4(vColor.rgb, vColor.a * a);\n"
"    } else {\n"
"        fragColor = vColor;\n"
"    }\n"
"}\n";

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[FDK/GL] Shader error: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vert, const char *frag)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vert);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

/* ─── Batch helpers ──────────────────────────────────────────────────────── */
static void batch_flush(FDK_RenderCtx *ctx)
{
    if (ctx->batch_count == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    ctx->batch_count * sizeof(Vertex),
                    ctx->batch);

    /* Draw quads as pairs of triangles; every 4 verts = 1 quad */
    /* We use indexed drawing with a persistent index buffer (0,1,2,2,3,0) */
    glDrawArrays(GL_TRIANGLES, 0, ctx->batch_count);
    ctx->batch_count = 0;
}

static Vertex *batch_alloc(FDK_RenderCtx *ctx, int n_verts)
{
    if (ctx->batch_count + n_verts > MAX_VERTS)
        batch_flush(ctx);
    Vertex *v = ctx->batch + ctx->batch_count;
    ctx->batch_count += n_verts;
    return v;
}

/* Emit a solid-colour quad (6 verts = 2 triangles) */
static void emit_quad(FDK_RenderCtx *ctx,
                       float x0, float y0, float x1, float y1,
                       FDK_Color c)
{
    float r = c.r/255.f, g = c.g/255.f,
          b = c.b/255.f, a = c.a/255.f;
    Vertex *v = batch_alloc(ctx, 6);
    /* Triangle 1 */
    v[0] = (Vertex){ x0,y0, 0,0, r,g,b,a, 0 };
    v[1] = (Vertex){ x1,y0, 0,0, r,g,b,a, 0 };
    v[2] = (Vertex){ x1,y1, 0,0, r,g,b,a, 0 };
    /* Triangle 2 */
    v[3] = (Vertex){ x0,y0, 0,0, r,g,b,a, 0 };
    v[4] = (Vertex){ x1,y1, 0,0, r,g,b,a, 0 };
    v[5] = (Vertex){ x0,y1, 0,0, r,g,b,a, 0 };
}

/* ─── Context lifecycle ──────────────────────────────────────────────────── */
static FDK_RenderCtx *gl_ctx_create(FDK_Window *win)
{
    fdk__platform->gl_make_current(fdk_window_get_pw(win));

    FDK_RenderCtx *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return NULL;
    ctx->win   = win;
    ctx->batch = malloc(MAX_VERTS * sizeof(Vertex));

    /* VAO + VBO */
    glGenVertexArrays(1, &ctx->vao);
    glGenBuffers(1, &ctx->vbo);
    glBindVertexArray(ctx->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_VERTS * sizeof(Vertex), NULL, GL_DYNAMIC_DRAW);

    size_t stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, mode));

    /* Shader */
    ctx->shader = link_program(s_vert_src, s_frag_src);

    /* Glyph atlas texture (single-channel R8) */
    glGenTextures(1, &ctx->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, ctx->atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    return ctx;
}

static void gl_ctx_destroy(FDK_RenderCtx *ctx)
{
    if (!ctx) return;
    glDeleteTextures(1, &ctx->atlas_tex);
    glDeleteProgram(ctx->shader);
    glDeleteBuffers(1, &ctx->vbo);
    glDeleteVertexArrays(1, &ctx->vao);
    free(ctx->batch);
    free(ctx);
}

static void gl_begin_frame(FDK_RenderCtx *ctx, int w, int h)
{
    fdk__platform->gl_make_current(fdk_window_get_pw(ctx->win));
    ctx->w = w; ctx->h = h;
    glViewport(0, 0, w, h);

    glUseProgram(ctx->shader);
    glUniform2f(glGetUniformLocation(ctx->shader, "uScreenSize"),
                (float)w, (float)h);
    glUniform1i(glGetUniformLocation(ctx->shader, "uAtlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->atlas_tex);

    glBindVertexArray(ctx->vao);
    ctx->batch_count  = 0;
    ctx->clip_depth   = 0;
    ctx->clip         = (FDK_Rect){ 0, 0, w, h };
    glDisable(GL_SCISSOR_TEST);
}

static void gl_end_frame(FDK_RenderCtx *ctx)
{
    batch_flush(ctx);
    fdk__platform->gl_swap_buffers(fdk_window_get_pw(ctx->win));
}

/* ─── Clip ───────────────────────────────────────────────────────────────── */
static FDK_Rect rect_intersect(FDK_Rect a, FDK_Rect b)
{
    int x0 = a.x>b.x?a.x:b.x, y0 = a.y>b.y?a.y:b.y;
    int x1 = (a.x+a.w)<(b.x+b.w)?(a.x+a.w):(b.x+b.w);
    int y1 = (a.y+a.h)<(b.y+b.h)?(a.y+a.h):(b.y+b.h);
    if(x1<x0)x1=x0;
    if(y1<y0)y1=y0;
    return (FDK_Rect){x0,y0,x1-x0,y1-y0};
}

static void apply_scissor(FDK_RenderCtx *ctx)
{
    batch_flush(ctx);
    glEnable(GL_SCISSOR_TEST);
    /* GL scissor origin is bottom-left */
    glScissor(ctx->clip.x,
              ctx->h - ctx->clip.y - ctx->clip.h,
              ctx->clip.w, ctx->clip.h);
}

static void gl_push_clip(FDK_RenderCtx *ctx, FDK_Rect r)
{
    if (ctx->clip_depth < CLIP_STACK_MAX)
        ctx->clip_stack[ctx->clip_depth++] = ctx->clip;
    ctx->clip = rect_intersect(ctx->clip, r);
    apply_scissor(ctx);
}

static void gl_pop_clip(FDK_RenderCtx *ctx)
{
    if (ctx->clip_depth > 0)
        ctx->clip = ctx->clip_stack[--ctx->clip_depth];
    else
        ctx->clip = (FDK_Rect){ 0, 0, ctx->w, ctx->h };
    if (ctx->clip_depth == 0)
        glDisable(GL_SCISSOR_TEST);
    else
        apply_scissor(ctx);
}

/* ─── Draw calls ─────────────────────────────────────────────────────────── */
static void gl_clear(FDK_RenderCtx *ctx, FDK_Color c)
{
    batch_flush(ctx);
    glClearColor(c.r/255.f, c.g/255.f, c.b/255.f, c.a/255.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void gl_fill_rect(FDK_RenderCtx *ctx, FDK_Rect r, FDK_Color c)
{
    emit_quad(ctx,
              (float)r.x, (float)r.y,
              (float)(r.x+r.w), (float)(r.y+r.h), c);
}

static void gl_stroke_rect(FDK_RenderCtx *ctx, FDK_Rect r,
                            FDK_Color c, int t)
{
    gl_fill_rect(ctx, (FDK_Rect){r.x,      r.y,       r.w, t}, c);
    gl_fill_rect(ctx, (FDK_Rect){r.x,      r.y+r.h-t, r.w, t}, c);
    gl_fill_rect(ctx, (FDK_Rect){r.x,      r.y+t,     t, r.h-2*t}, c);
    gl_fill_rect(ctx, (FDK_Rect){r.x+r.w-t,r.y+t,     t, r.h-2*t}, c);
}

/* Rounded rect: approximate with many horizontal spans (simple but works) */
static void gl_fill_rect_rounded(FDK_RenderCtx *ctx,
                                  FDK_Rect r, int rad, FDK_Color c)
{
    if (rad <= 0) { gl_fill_rect(ctx, r, c); return; }
    if (rad > r.w/2) rad = r.w/2;
    if (rad > r.h/2) rad = r.h/2;

    /* Centre band */
    emit_quad(ctx,
              (float)r.x, (float)(r.y+rad),
              (float)(r.x+r.w), (float)(r.y+r.h-rad), c);

    /* Scan the corner arcs row by row */
    float r2 = (float)(rad*rad);
    for (int dy = 0; dy < rad; dy++) {
        float fy  = dy - rad + 0.5f;
        float dx  = sqrtf(r2 - fy*fy);
        int   left = (int)(rad - dx);
        /* Top strip */
        emit_quad(ctx,
                  (float)(r.x + left), (float)(r.y + dy),
                  (float)(r.x + r.w - left), (float)(r.y + dy + 1), c);
        /* Bottom strip */
        emit_quad(ctx,
                  (float)(r.x + left), (float)(r.y + r.h - dy - 1),
                  (float)(r.x + r.w - left), (float)(r.y + r.h - dy), c);
    }
}

static void gl_fill_circle(FDK_RenderCtx *ctx,
                            int cx, int cy, int rad, FDK_Color c)
{
    float r2 = (float)(rad*rad);
    for (int dy = -rad; dy <= rad; dy++) {
        float dx = sqrtf(r2 - (float)(dy*dy));
        emit_quad(ctx,
                  (float)(cx - (int)dx), (float)(cy + dy),
                  (float)(cx + (int)dx), (float)(cy + dy + 1), c);
    }
}

static void gl_draw_line(FDK_RenderCtx *ctx,
                          int x0, int y0, int x1, int y1,
                          FDK_Color c, int thickness)
{
    /* Approximate thick line as a rotated rectangle */
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) return;

    float nx = -dy/len * (thickness/2.f);
    float ny =  dx/len * (thickness/2.f);

    float r = c.r/255.f, g = c.g/255.f,
          b = c.b/255.f, a = c.a/255.f;
    Vertex *v = batch_alloc(ctx, 6);
    v[0] = (Vertex){ x0+nx, y0+ny, 0,0, r,g,b,a, 0 };
    v[1] = (Vertex){ x1+nx, y1+ny, 0,0, r,g,b,a, 0 };
    v[2] = (Vertex){ x1-nx, y1-ny, 0,0, r,g,b,a, 0 };
    v[3] = (Vertex){ x0+nx, y0+ny, 0,0, r,g,b,a, 0 };
    v[4] = (Vertex){ x1-nx, y1-ny, 0,0, r,g,b,a, 0 };
    v[5] = (Vertex){ x0-nx, y0-ny, 0,0, r,g,b,a, 0 };
}

/* ─── Glyph rasterisation + atlas upload ───────────────────────────────────
 *
 * Cache key is the FreeType glyph_id (NOT the codepoint). With HarfBuzz
 * shaping, the same codepoint can map to multiple glyphs depending on
 * context (Arabic positional forms, ligatures), so codepoint-keyed
 * caching would return the wrong glyph. Glyph_id is unique per
 * (font, glyph) pair. */
static GlyphEntry *get_glyph(FDK_RenderCtx *ctx,
                               FDK_Font *font,
                               uint32_t glyph_id)
{
    for (int i = 0; i < ctx->glyph_count; i++)
        if (ctx->glyphs[i].glyph_id == glyph_id &&
            ctx->glyphs[i].advance > 0)
            return &ctx->glyphs[i];

    if (ctx->glyph_count >= MAX_GLYPHS) return NULL;

    FT_Face face = font->face;
    /* FT_Load_Glyph takes a glyph_id; FT_Load_Char takes a codepoint.
     * We use the former since shaping gives us glyph IDs. */
    if (FT_Load_Glyph(face, glyph_id, FT_LOAD_RENDER)) return NULL;

    FT_GlyphSlot g = face->glyph;
    FT_Bitmap   *bm = &g->bitmap;

    if (bm->width == 0 || bm->rows == 0) {
        /* Space or invisible char — store metrics only */
        GlyphEntry *e = &ctx->glyphs[ctx->glyph_count++];
        e->glyph_id   = glyph_id;
        e->advance    = (int)(g->advance.x >> 6);
        e->w = e->h   = 0;
        return e;
    }

    /* Check atlas has room in current row */
    if (ctx->atlas_pen_x + (int)bm->width > ATLAS_W) {
        ctx->atlas_pen_x  = 0;
        ctx->atlas_pen_y += ctx->atlas_row_h;
        ctx->atlas_row_h  = 0;
    }
    if (ctx->atlas_pen_y + (int)bm->rows > ATLAS_H)
        return NULL; /* atlas full — would need eviction in a real impl */

    glBindTexture(GL_TEXTURE_2D, ctx->atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    ctx->atlas_pen_x, ctx->atlas_pen_y,
                    (int)bm->width, (int)bm->rows,
                    GL_RED, GL_UNSIGNED_BYTE, bm->buffer);

    GlyphEntry *e = &ctx->glyphs[ctx->glyph_count++];
    e->glyph_id   = glyph_id;
    e->u0 = (float)ctx->atlas_pen_x / ATLAS_W;
    e->v0 = (float)ctx->atlas_pen_y / ATLAS_H;
    e->u1 = (float)(ctx->atlas_pen_x + bm->width)  / ATLAS_W;
    e->v1 = (float)(ctx->atlas_pen_y + bm->rows)   / ATLAS_H;
    e->bearing_x  = g->bitmap_left;
    e->bearing_y  = g->bitmap_top;
    e->advance    = (int)(g->advance.x >> 6);
    e->w          = (int)bm->width;
    e->h          = (int)bm->rows;

    ctx->atlas_pen_x += (int)bm->width + 1;
    if ((int)bm->rows > ctx->atlas_row_h)
        ctx->atlas_row_h = (int)bm->rows;

    return e;
}

static void gl_draw_text(FDK_RenderCtx *ctx,
                          FDK_Font *font,
                          const char *utf8,
                          int x, int y,
                          FDK_Color c)
{
    if (!font || !utf8) return;

    /* Shape the text. Without HarfBuzz this is a codepoint-by-codepoint
     * fallback (v0.1.0 behaviour). With HarfBuzz, complex scripts shape
     * correctly. Without FriBidi, no BiDi reordering. */
    FDK_GlyphInfo *glyphs = NULL;
    int n_glyphs = fdk__shape_text(font, utf8, -1, &glyphs);
    if (n_glyphs <= 0) { free(glyphs); return; }

    int pen_x    = x;
    int baseline = y + font->ascender;
    float fr = c.r/255.f, fg = c.g/255.f,
          fb = c.b/255.f, fa = c.a/255.f;

    for (int i = 0; i < n_glyphs; i++) {
        GlyphEntry *ge = get_glyph(ctx, font, glyphs[i].glyph_id);
        if (!ge) {
            pen_x += (glyphs[i].x_advance >> 6);
            continue;
        }
        if (ge->w > 0) {
            /* Apply HarfBuzz x_offset/y_offset (26.6 fixed → pixels).
             * Most scripts these are 0; vertical scripts and mark
             * positioning use them. */
            float gx0 = (float)(pen_x + (glyphs[i].x_offset >> 6) + ge->bearing_x);
            float gy0 = (float)(baseline - (glyphs[i].y_offset >> 6) - ge->bearing_y);
            float gx1 = gx0 + ge->w;
            float gy1 = gy0 + ge->h;

            Vertex *v = batch_alloc(ctx, 6);
            v[0] = (Vertex){ gx0,gy0, ge->u0,ge->v0, fr,fg,fb,fa, 1 };
            v[1] = (Vertex){ gx1,gy0, ge->u1,ge->v0, fr,fg,fb,fa, 1 };
            v[2] = (Vertex){ gx1,gy1, ge->u1,ge->v1, fr,fg,fb,fa, 1 };
            v[3] = (Vertex){ gx0,gy0, ge->u0,ge->v0, fr,fg,fb,fa, 1 };
            v[4] = (Vertex){ gx1,gy1, ge->u1,ge->v1, fr,fg,fb,fa, 1 };
            v[5] = (Vertex){ gx0,gy1, ge->u0,ge->v1, fr,fg,fb,fa, 1 };
        }
        pen_x += (glyphs[i].x_advance >> 6);
    }

    free(glyphs);
}

/* ─── VTable ─────────────────────────────────────────────────────────────── */
const FDK_RenderVTable fdk_render_opengl = {
    .name              = "OpenGL",
    .ctx_create        = gl_ctx_create,
    .ctx_destroy       = gl_ctx_destroy,
    .begin_frame       = gl_begin_frame,
    .end_frame         = gl_end_frame,
    .clear             = gl_clear,
    .fill_rect         = gl_fill_rect,
    .stroke_rect       = gl_stroke_rect,
    .fill_rect_rounded = gl_fill_rect_rounded,
    .fill_circle       = gl_fill_circle,
    .draw_line         = gl_draw_line,
    .draw_text         = gl_draw_text,
    .push_clip         = gl_push_clip,
    .pop_clip          = gl_pop_clip,
};

#endif /* FDK_HAVE_OPENGL */
