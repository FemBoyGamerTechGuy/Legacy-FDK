/*
 * bidi.h — FDK in-tree Unicode Bidirectional Algorithm (UAX #9)
 *
 * This is a from-scratch implementation of the Unicode Bidirectional
 * Algorithm, replacing the previous FriBidi (LGPL-2.1+) dependency.
 * FDK is proprietary-licensed, so we cannot link LGPL code.
 *
 * The implementation covers the common cases:
 *   - P1-P3: paragraph level detection (auto from first strong char)
 *   - X1-X10: explicit embedding/override (RLE/LRE/RLO/LRO/PDF)
 *   - W1-W7: weak type resolution
 *   - N0: bracket pair resolution (using BidiBrackets.txt data)
 *   - N1-N2: neutral type resolution
 *   - I1-I2: implicit level assignment
 *   - L1: trailing whitespace reset
 *   - L2: reverse by descending level
 *
 * NOT implemented (deferred to a future version):
 *   - X5a-X9: isolate sequences (FSI/LRI/RLI/PDI) — these are
 *     increasingly common in modern text but add ~200 LOC
 *   - L3: combining mark ordering (handled by HarfBuzz already)
 *   - L4: glyph mirroring for RTL context (handled by HarfBuzz
 *     already via hb_shape with mirroring feature)
 *
 * For pure-LTR text (English, Chinese, Japanese, Korean, etc.), the
 * output is identical to the input. For pure-RTL text (Arabic, Hebrew)
 * the output is the reversed string. For mixed LTR/RTL text the output
 * is in correct visual order, suitable for HarfBuzz shaping.
 *
 * Reference: Unicode Standard Annex #9, Unicode 16.0
 *            https://www.unicode.org/reports/tr9/
 */
#ifndef FDK_BIDI_H
#define FDK_BIDI_H

#include <stdint.h>
#include <stddef.h>

/* Bidi character types (UAX #9 BD1-BD7). Subset of the full set;
 * the values match Unicode's standard abbreviations. */
typedef enum {
    FDK_BIDI_L    = 0,   /* Left-to-Right           (strong) */
    FDK_BIDI_R    = 1,   /* Right-to-Left           (strong) */
    FDK_BIDI_AL   = 2,   /* Right-to-Left Arabic    (strong) */
    FDK_BIDI_EN   = 3,   /* European Number         (weak)  */
    FDK_BIDI_ES   = 4,   /* European Separator      (weak)  */
    FDK_BIDI_ET   = 5,   /* European Terminator     (weak)  */
    FDK_BIDI_AN   = 6,   /* Arabic Number           (weak)  */
    FDK_BIDI_CS   = 7,   /* Common Separator        (weak)  */
    FDK_BIDI_NSM  = 8,   /* Nonspacing Mark         (weak)  */
    FDK_BIDI_BN   = 9,   /* Boundary Neutral        (weak)  */
    FDK_BIDI_B    = 10,  /* Paragraph Separator     (neutral) */
    FDK_BIDI_S    = 11,  /* Segment Separator       (neutral) */
    FDK_BIDI_WS   = 12,  /* Whitespace              (neutral) */
    FDK_BIDI_ON   = 13,  /* Other Neutral           (neutral) */
    FDK_BIDI_LRE  = 14,  /* Left-to-Right Embedding (formatting) */
    FDK_BIDI_RLE  = 15,  /* Right-to-Left Embedding (formatting) */
    FDK_BIDI_LRO  = 16,  /* Left-to-Right Override  (formatting) */
    FDK_BIDI_RLO  = 17,  /* Right-to-Left Override  (formatting) */
    FDK_BIDI_PDF  = 18,  /* Pop Directional Format  (formatting) */
    /* FSI/LRI/RLI/PDI (isolate formatting) — recognized but treated
     * as ON for v0.1. Full isolate support is a future item. */
    FDK_BIDI_LRI  = 19,
    FDK_BIDI_RLI  = 20,
    FDK_BIDI_FSI  = 21,
    FDK_BIDI_PDI  = 22,
    FDK_BIDI_UNKNOWN = 23,
} FDK_BidiType;

/* Look up the bidi type of a Unicode codepoint. Implemented in bidi.c
 * with a compact lookup table covering the most common Unicode blocks. */
FDK_BidiType fdk__bidi_type(uint32_t cp);

/* Reorder a logical-order UTF-32 string into visual order.
 *
 *   cps        — input codepoints in logical order (N codepoints)
 *   n          — number of codepoints
 *   out_visual — receives the reordered codepoints (N elements, caller-
 *                allocated, may equal cps for in-place reordering)
 *   out_v2l    — receives the visual-to-logical index map (N elements,
 *                caller-allocated, may be NULL if not needed).
 *                out_v2l[i] = j means visual position i corresponds to
 *                logical position j.
 *
 * Returns the paragraph embedding level (0 for LTR, 1 for RTL), or -1
 * on error (NULL cps, NULL out_visual, or n < 0).
 *
 * The caller owns out_visual and out_v2l — this function does not
 * allocate. For most use cases, pre-allocate on the stack:
 *
 *   uint32_t visual[n];
 *   int v2l[n];
 *   int level = fdk__bidi_reorder(cps, n, visual, v2l);
 *
 * The algorithm is O(n) in time and O(n) in auxiliary stack space. */
int fdk__bidi_reorder(const uint32_t *cps, int n,
                       uint32_t *out_visual, int *out_v2l);

/* Convenience: just compute the paragraph embedding level without
 * reordering. Useful when the caller only wants to know if the text
 * is predominantly RTL. Returns 0 (LTR) or 1 (RTL). */
int fdk__bidi_paragraph_level(const uint32_t *cps, int n);

#endif /* FDK_BIDI_H */
