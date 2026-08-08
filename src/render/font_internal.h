/*
 * font_internal.h — FDK_Font definition shared between render backends.
 */
#ifndef FDK_FONT_INTERNAL_H
#define FDK_FONT_INTERNAL_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include "fdk/fdk.h"

#ifdef FDK_HAVE_HARFBUZZ
#include <hb.h>
#endif

struct FDK_Font {
    FT_Face  face;
    float    size_px;

    /* Real metrics in pixels, computed at load time from FreeType's
     * scaled metrics. All values are positive integers. */
    int      ascender;    /* distance from baseline up to top of capitals  */
    int      descender;   /* distance from baseline down to bottom (>= 0)  */
    int      line_height; /* ascender + descender + internal leading        */
    int      x_height;    /* height of lowercase 'x'                       */
    int      cap_height;  /* height of uppercase 'H'                       */

#ifdef FDK_HAVE_HARFBUZZ
    /* Per-font HarfBuzz font object. Lazily created by
     * fdk__font_init_shaping() and destroyed by
     * fdk__font_fini_shaping(). NULL if shaping hasn't been
     * initialised yet, or if HarfBuzz is not compiled in. */
    hb_font_t *hb_font;
#endif
};

/* Fills metric fields from face->size->metrics after FT_Set_Pixel_Sizes */
static inline void fdk_font_compute_metrics(FDK_Font *f)
{
    FT_Size_Metrics *m = &f->face->size->metrics;
    /* FreeType metrics are in 26.6 fixed point */
    f->ascender    = (int)( m->ascender  >> 6);
    f->descender   = (int)(-m->descender >> 6); /* descender is negative */
    f->line_height = (int)( m->height    >> 6);
    /* Clamp: descender should never be negative after negation */
    if (f->descender < 0) f->descender = 0;
    /* line_height should be at least ascender + descender */
    if (f->line_height < f->ascender + f->descender)
        f->line_height = f->ascender + f->descender;
}

#endif /* FDK_FONT_INTERNAL_H */
