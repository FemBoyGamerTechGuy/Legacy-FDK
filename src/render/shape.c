/*
 * shape.c — FDK text shaping implementation
 *
 * Routes UTF-8 text through:
 *   1. In-tree bidi.c — Unicode Bidirectional Algorithm (UAX #9),
 *      implemented from scratch in src/render/bidi.c. Replaces the
 *      previous FriBidi (LGPL-2.1+) dependency. Handles pure-LTR
 *      text as a no-op and mixed LTR/RTL text per UAX #9.
 *   2. HarfBuzz (optional) — complex script shaping, so Arabic
 *      letters connect, Indic consonant clusters get their
 *      half-forms, and Latin ligatures (fi, ffi) form.
 *
 * HarfBuzz is optional. Without HarfBuzz, falls back to
 * codepoint-by-codepoint glyph lookup (v0.1.0 behaviour, broken
 * for complex scripts but fine for Latin).
 *
 * HarfBuzz integrates with FreeType via hb_ft_face_create_referenced(),
 * which makes HarfBuzz use FreeType's glyph metrics and outlines
 * directly — no duplicated font loading.
 */
#include "shape.h"

#include <stdlib.h>
#include <string.h>

#ifdef FDK_HAVE_HARFBUZZ
#include <hb.h>
#include <hb-ft.h>
#endif

/* BiDi (UAX #9) is implemented in-tree in bidi.c — no external dep. */
#include "bidi.h"

/* ─── Per-font HarfBuzz setup ─────────────────────────────────────────────── */

void fdk__font_init_shaping(FDK_Font *font)
{
#ifdef FDK_HAVE_HARFBUZZ
    if (!font || font->hb_font) return;
    /* hb_ft_font_create_referenced is the recommended one-step API:
     * it creates an hb_face from the FT_Face, creates an hb_font from
     * that face, AND wires up the font funcs to read metrics from
     * FreeType. Most importantly, it caches the FT_Face's CURRENT
     * size at creation time — which is exactly what we want, since
     * fdk_font_load() already called FT_Set_Pixel_Sizes() before us.
     *
     * The hb_font takes its own reference to the FT_Face, so it stays
     * alive until both the hb_font is destroyed AND the caller has
     * called FT_Done_Face. */
    hb_font_t *hb_font = hb_ft_font_create_referenced(font->face);
    if (!hb_font) return;
    font->hb_font = hb_font;
#else
    (void)font;
#endif
}

void fdk__font_fini_shaping(FDK_Font *font)
{
#ifdef FDK_HAVE_HARFBUZZ
    if (!font) return;
    if (font->hb_font) {
        hb_font_destroy(font->hb_font);
        font->hb_font = NULL;
    }
#else
    (void)font;
#endif
}

/* ─── BiDi reordering (in-tree UAX #9) ──────────────────────────────────────
 *
 * fdk__bidi_reorder() in bidi.c implements the Unicode Bidirectional
 * Algorithm. It takes a logical-order UTF-32 string and produces a
 * visual-order UTF-32 string plus a visual-to-logical index map.
 *
 * For pure-LTR text (English, CJK, etc.), visual order == logical
 * order. For pure-RTL text, the string is reversed. For mixed LTR/RTL
 * text, the output is in correct visual order, ready for HarfBuzz
 * shaping.
 *
 * Returns the paragraph embedding level (0=LTR, 1=RTL) or -1 on error. */

/* ─── UTF-8 → UTF-32 decoder ──────────────────────────────────────────────── */
static int utf8_to_utf32(const char *utf8, int byte_len,
                          uint32_t **out_utf32, int **out_byte_offsets)
{
    if (byte_len < 0) byte_len = (int)strlen(utf8);
    if (byte_len == 0) {
        *out_utf32 = NULL;
        *out_byte_offsets = NULL;
        return 0;
    }

    /* Worst case: 1 byte per codepoint */
    uint32_t *cps    = malloc(sizeof(uint32_t) * (size_t)(byte_len + 1));
    int       *boffs = malloc(sizeof(int)       * (size_t)(byte_len + 1));
    if (!cps || !boffs) { free(cps); free(boffs); return -1; }

    int n = 0;
    int i = 0;
    while (i < byte_len) {
        const unsigned char *s = (const unsigned char *)(utf8 + i);
        uint32_t cp;
        int extra;
        if (s[0] < 0x80)       { cp = s[0];        extra = 0; }
        else if (s[0] < 0xC0)  { cp = 0xFFFD;      extra = 0; } /* stray continuation */
        else if (s[0] < 0xE0)  { cp = s[0] & 0x1F; extra = 1; }
        else if (s[0] < 0xF0)  { cp = s[0] & 0x0F; extra = 2; }
        else                   { cp = s[0] & 0x07; extra = 3; }
        for (int j = 0; j < extra; j++) {
            if (i + 1 + j >= byte_len) { cp = 0xFFFD; extra = j; break; }
            if ((s[j+1] & 0xC0) != 0x80) { cp = 0xFFFD; extra = j; break; }
            cp = (cp << 6) | (s[j+1] & 0x3F);
        }
        cps[n]    = cp;
        boffs[n]  = i;
        n++;
        i += 1 + extra;
    }
    cps[n]   = 0;
    boffs[n] = byte_len;
    *out_utf32 = cps;
    *out_byte_offsets = boffs;
    return n;
}

/* ─── Shape text (HarfBuzz path) ──────────────────────────────────────────── */

#ifdef FDK_HAVE_HARFBUZZ
static int shape_with_harfbuzz(FDK_Font *font,
                                const uint32_t *cps, int n_cps,
                                const int *byte_offsets,
                                FDK_GlyphInfo **out_glyphs)
{
    hb_buffer_t *buf = hb_buffer_create();
    if (!hb_buffer_pre_allocate(buf, (unsigned)n_cps)) {
        hb_buffer_destroy(buf);
        return -1;
    }

    hb_buffer_add_utf32(buf, cps, (unsigned)n_cps, 0, (unsigned)n_cps);
    /* Let HarfBuzz guess script, language, direction from content.
     * This is what Pango's itemize-then-shape does at the paragraph
     * level; for a single run we accept its guess. Apps that need
     * finer control can be added later via a per-widget API. */
    hb_buffer_guess_segment_properties(buf);

    hb_shape(font->hb_font, buf, NULL, 0);

    unsigned int n_glyphs = 0;
    hb_glyph_info_t     *infos = hb_buffer_get_glyph_infos(buf, &n_glyphs);
    hb_glyph_position_t *poss  = hb_buffer_get_glyph_positions(buf, &n_glyphs);

    if (n_glyphs == 0) {
        hb_buffer_destroy(buf);
        *out_glyphs = NULL;
        return 0;
    }

    FDK_GlyphInfo *out = malloc(sizeof(FDK_GlyphInfo) * n_glyphs);
    if (!out) {
        hb_buffer_destroy(buf);
        return -1;
    }

    for (unsigned int i = 0; i < n_glyphs; i++) {
        out[i].glyph_id  = infos[i].codepoint;  /* after shaping, this is a glyph ID */
        out[i].x_offset  = poss[i].x_offset;
        out[i].y_offset  = poss[i].y_offset;
        out[i].x_advance = poss[i].x_advance;
        /* cluster is a UTF-32 index in our case — map back to byte offset */
        uint32_t cluster_cp_idx = infos[i].cluster;
        if (cluster_cp_idx > (uint32_t)n_cps) cluster_cp_idx = (uint32_t)n_cps;
        out[i].cluster = (uint32_t)byte_offsets[cluster_cp_idx];
    }

    hb_buffer_destroy(buf);
    *out_glyphs = out;
    return (int)n_glyphs;
}
#endif /* FDK_HAVE_HARFBUZZ */

/* ─── Shape text (fallback path — no HarfBuzz) ────────────────────────────── */

static int shape_without_harfbuzz(FDK_Font *font,
                                   const uint32_t *cps, int n_cps,
                                   const int *byte_offsets,
                                   FDK_GlyphInfo **out_glyphs)
{
    if (n_cps == 0) { *out_glyphs = NULL; return 0; }
    FDK_GlyphInfo *out = malloc(sizeof(FDK_GlyphInfo) * (size_t)n_cps);
    if (!out) return -1;

    for (int i = 0; i < n_cps; i++) {
        FT_UInt gid = FT_Get_Char_Index(font->face, cps[i]);
        /* Use FreeType's advance-only load to get the metrics.
         * We don't render here — the renderer will FT_Load_Glyph later. */
        FT_Error err = FT_Load_Glyph(font->face, gid, FT_LOAD_ADVANCE_ONLY);
        if (err) {
            out[i].glyph_id  = 0;
            out[i].x_advance = 0;
            out[i].x_offset  = 0;
            out[i].y_offset  = 0;
        } else {
            out[i].glyph_id  = gid;
            out[i].x_advance = font->face->glyph->advance.x;
            out[i].x_offset  = 0;
            out[i].y_offset  = 0;
        }
        out[i].cluster = (uint32_t)byte_offsets[i];
    }
    *out_glyphs = out;
    return n_cps;
}

/* ─── Public entry point ──────────────────────────────────────────────────── */

int fdk__shape_text(FDK_Font *font,
                    const char *utf8,
                    int byte_len,
                    FDK_GlyphInfo **out_glyphs)
{
    if (!font || !utf8 || !out_glyphs) return -1;
    *out_glyphs = NULL;
    if (byte_len < 0) byte_len = (int)strlen(utf8);
    if (byte_len == 0) return 0;

    /* 1. Decode UTF-8 → UTF-32 + byte offsets (for cluster mapping) */
    uint32_t *cps = NULL;
    int       *byte_offsets = NULL;
    int n_cps = utf8_to_utf32(utf8, byte_len, &cps, &byte_offsets);
    if (n_cps < 0) return -1;
    if (n_cps == 0) { free(cps); free(byte_offsets); return 0; }

    /* 2. BiDi reorder (in-tree UAX #9). For pure-LTR text, visual ==
     * logical. For mixed LTR/RTL, the algorithm reorders per UAX #9. */
    uint32_t *visual_cps = NULL;
    int       *visual_byte_offsets = NULL;
    int       n_visual = n_cps;  /* UAX #9 doesn't change length */

    /* Allocate visual buffers unconditionally — the in-tree reorder
     * always produces a non-NULL output (even for pure-LTR text). */
    visual_cps = malloc(sizeof(uint32_t) * (size_t)(n_cps + 1));
    int *v2l = malloc(sizeof(int) * (size_t)(n_cps + 1));
    if (!visual_cps || !v2l) {
        free(cps); free(byte_offsets);
        free(visual_cps); free(v2l);
        return -1;
    }
    int level = fdk__bidi_reorder(cps, n_cps, visual_cps, v2l);
    if (level < 0) {
        /* BiDi failed — fall back to logical order */
        free(visual_cps); free(v2l);
        visual_cps = NULL;
        n_visual = 0;
    } else {
        /* Build byte-offset map for the visual order */
        visual_byte_offsets = malloc(sizeof(int) * (size_t)(n_visual + 1));
        if (!visual_byte_offsets) {
            free(cps); free(byte_offsets);
            free(visual_cps); free(v2l);
            return -1;
        }
        for (int i = 0; i < n_visual; i++) {
            int logical_idx = v2l[i];
            visual_byte_offsets[i] = (logical_idx >= 0 && logical_idx <= n_cps)
                                     ? byte_offsets[logical_idx] : byte_len;
        }
        visual_byte_offsets[n_visual] = byte_len;
        free(v2l);
    }

    /* If BiDi didn't run, use logical order directly */
    const uint32_t *shape_cps = visual_cps ? visual_cps : cps;
    const int       *shape_offsets = visual_byte_offsets ? visual_byte_offsets : byte_offsets;
    int              shape_n = visual_cps ? n_visual : n_cps;

    /* 3. Shape */
    int n_glyphs;
#ifdef FDK_HAVE_HARFBUZZ
    if (font->hb_font) {
        n_glyphs = shape_with_harfbuzz(font, shape_cps, shape_n,
                                        shape_offsets, out_glyphs);
    } else {
        n_glyphs = shape_without_harfbuzz(font, shape_cps, shape_n,
                                           shape_offsets, out_glyphs);
    }
#else
    n_glyphs = shape_without_harfbuzz(font, shape_cps, shape_n,
                                       shape_offsets, out_glyphs);
#endif

    free(cps);
    free(byte_offsets);
    free(visual_cps);
    free(visual_byte_offsets);
    return n_glyphs;
}

/* ─── Convenience: just measure advance width ─────────────────────────────── */

int fdk__shape_measure(FDK_Font *font,
                       const char *utf8,
                       int byte_len)
{
    FDK_GlyphInfo *glyphs = NULL;
    int n = fdk__shape_text(font, utf8, byte_len, &glyphs);
    if (n < 0) return 0;
    int advance = 0;
    for (int i = 0; i < n; i++) {
        advance += glyphs[i].x_advance;
    }
    free(glyphs);
    /* x_advance is in 26.6 fixed point; convert to integer pixels */
    return advance >> 6;
}
