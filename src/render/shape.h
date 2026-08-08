/*
 * shape.h — FDK text shaping interface (internal)
 *
 * Wraps HarfBuzz for complex-script shaping (Arabic, Hebrew, Indic,
 * ligatures, etc.) and the in-tree bidi.c module for Unicode
 * bidirectional reordering (UAX #9 — implemented from scratch to
 * avoid the LGPL FriBidi dependency).
 *
 * Both the software and OpenGL render backends call fdk__shape_text()
 * to convert a UTF-8 string into an ordered array of positioned
 * glyphs. The render backend then rasterises each glyph via FreeType
 * and composites it at the shaped position.
 *
 * Without HarfBuzz (FDK_HAVE_HARFBUZZ undefined), falls back to
 * codepoint-by-codepoint shaping using FT_Get_Char_Index — the
 * original v0.1.0 behaviour, no complex scripts.
 *
 * BiDi is always available — it's in-tree, no external dep. For
 * pure-LTR text (the common case), it's a no-op (visual == logical).
 */
#ifndef FDK_SHAPE_H
#define FDK_SHAPE_H

#include "fdk/fdk.h"
#include "font_internal.h"

/* A single shaped glyph, ready to rasterise. */
typedef struct {
    uint32_t glyph_id;     /* FreeType glyph index (NOT codepoint) */
    int32_t  x_offset;     /* 26.6 fixed-point, pixels << 6         */
    int32_t  y_offset;     /* 26.6 fixed-point, pixels << 6         */
    int32_t  x_advance;    /* 26.6 fixed-point, pixels << 6         */
    uint32_t cluster;      /* byte offset into original UTF-8       */
} FDK_GlyphInfo;

/* Shape a UTF-8 string into an ordered array of glyphs.
 *
 *   font       — loaded FDK_Font
 *   utf8       — NUL-terminated UTF-8 text
 *   byte_len   — length in bytes (or -1 to use strlen)
 *   out_glyphs — receives a malloc'd array (caller frees with free())
 *
 * Returns the number of glyphs in *out_glyphs, or -1 on error.
 * Returns 0 with *out_glyphs == NULL for empty input.
 *
 * The returned glyphs are in VISUAL order (after BiDi reordering
 * when FriBidi is available) and have absolute font-relative
 * offsets/advances. The renderer is responsible for accumulating
 * x_advance into a pen position and adding x_offset/y_offset to
 * the glyph's bearing.
 *
 * Thread-safety: HarfBuzz font objects are read-only after creation,
 * so it is safe to shape the same FDK_Font concurrently from
 * multiple threads. Each call allocates its own hb_buffer_t. */
int fdk__shape_text(FDK_Font *font,
                    const char *utf8,
                    int byte_len,
                    FDK_GlyphInfo **out_glyphs);

/* Measure the advance width of a UTF-8 string in pixels, using
 * shaping. Returns the same value that a render backend would
 * compute by summing x_advance. Used by fdk_measure_text().
 *
 * Without HarfBuzz, falls back to FT_Load_Char + advance.x for
 * each codepoint (the v0.1.0 behaviour). */
int fdk__shape_measure(FDK_Font *font,
                       const char *utf8,
                       int byte_len);

/* Lazily create the per-font HarfBuzz font object. Called from
 * fdk_font_load(); callers should not invoke this directly.
 * Idempotent: if font->hb_font is already set, returns it. */
void fdk__font_init_shaping(FDK_Font *font);

/* Release the per-font HarfBuzz font object. Called from
 * fdk_font_destroy(); callers should not invoke this directly. */
void fdk__font_fini_shaping(FDK_Font *font);

#endif /* FDK_SHAPE_H */
