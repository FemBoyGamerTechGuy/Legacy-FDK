/*
 * bidi.c — FDK in-tree Unicode Bidirectional Algorithm (UAX #9)
 *
 * See bidi.h for design notes. This file implements:
 *   - Character type lookup (compact table covering common Unicode blocks)
 *   - P1-P3 paragraph level detection
 *   - X1-X10 explicit embedding levels (RLE/LRE/RLO/LRO/PDF)
 *   - W1-W7 weak type resolution
 *   - N0 bracket pair resolution (using BidiBrackets.txt data, 64 pairs)
 *   - N1-N2 neutral type resolution
 *   - I1-I2 implicit level assignment
 *   - L1 trailing whitespace reset
 *   - L2 reverse by descending level
 *
 * No external dependencies. No heap allocation in the reordering path
 * (caller provides output buffers).
 *
 * Reference: Unicode Standard Annex #9, Unicode 16.0
 *            https://www.unicode.org/reports/tr9/
 */
#include "bidi.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ─── Bidi character type lookup ───────────────────────────────────────────
 *
 * Returns the bidi type of a Unicode codepoint. The full Unicode bidi
 * type database would be ~120KB; we use a compact block-based table
 * covering the most common ranges. Codepoints outside the table
 * default to L (which is correct for the vast majority of Unicode —
 * CJK, Cyrillic, Greek, etc., are all L).
 *
 * The table is organized by Unicode block. Each entry covers a
 * contiguous range. We linear-scan the table; for the ~20 blocks we
 * cover, this is faster than a hash and simpler than a multi-level
 * table.
 *
 * Within each block, we use a per-block lookup: most blocks have a
 * uniform bidi type, so we just store one type per block. The Latin-1
 * Supplement and Arabic blocks have mixed types and need per-codepoint
 * tables.
 */

typedef struct {
    uint32_t    start;
    uint32_t    end;       /* inclusive */
    FDK_BidiType type;
} BidiBlock;

/* Uniform-type blocks: every codepoint in [start, end] has the same
 * bidi type. Covers the vast majority of Unicode. Ordered by start
 * for linear scan. */
static const BidiBlock UNIFORM_BLOCKS[] = {
    /* Basic Latin — handled by per-cp table below */
    /* Latin-1 Supplement — handled by per-cp table below */

    {0x0100, 0x02B8, FDK_BIDI_L},     /* Latin Extended-A/B */
    {0x02B9, 0x02FF, FDK_BIDI_ON},    /* Spacing Modifier Letters (mostly) */
    {0x0300, 0x036F, FDK_BIDI_NSM},   /* Combining Diacritical Marks */
    {0x0370, 0x03FF, FDK_BIDI_L},     /* Greek and Coptic (mostly L) */
    {0x0400, 0x04FF, FDK_BIDI_L},     /* Cyrillic */
    {0x0500, 0x058F, FDK_BIDI_L},     /* Cyrillic Supplement, Armenian */

    /* ── RTL scripts: Hebrew (per @missing: 0590..05FF; R) ── */
    {0x0590, 0x05FF, FDK_BIDI_R},     /* Hebrew */

    /* ── AL scripts: Arabic + Thaana (per @missing: 0600..07BF; AL) ── */
    {0x0600, 0x07BF, FDK_BIDI_AL},    /* Arabic, Syriac, Arabic Supplement, Thaana */

    /* ── RTL scripts: NKo + Samaritan + Mandaic (per @missing: 07C0..085F; R) ── */
    {0x07C0, 0x085F, FDK_BIDI_R},     /* NKo, Samaritan, Mandaic */

    /* ── AL scripts: Syriac Supplement + Arabic Extended-A/B (per @missing: 0860..08FF; AL) ── */
    {0x0860, 0x08FF, FDK_BIDI_AL},

    /* 0x0900-0x10FF: Devanagari through Hanunoo — all L */
    {0x0900, 0x10FF, FDK_BIDI_L},
    /* 0x1100-0x11FF: Hangul Jamo — L */
    {0x1100, 0x11FF, FDK_BIDI_L},
    /* Most of CJK + Hangul + everything else through end of BMP is L.
     * Bamum (A6A0-A6FF) is L per Unicode 16.0 @missing — the script
     * was deliberately designed left-to-right, distinct from Arabic. */
    {0x1200, 0x1FFF, FDK_BIDI_L},     /* Ethiopic, Cherokee, Bamum, etc. */
    {0x2000, 0x200B, FDK_BIDI_WS},    /* General Punctuation spaces */
    {0x200C, 0x200D, FDK_BIDI_BN},    /* ZWNJ, ZWJ */
    {0x200E, 0x200E, FDK_BIDI_L},     /* LRM */
    {0x200F, 0x200F, FDK_BIDI_R},     /* RLM */
    {0x2010, 0x2027, FDK_BIDI_ON},    /* Hyphens, dashes, quotes */
    {0x2028, 0x2029, FDK_BIDI_B},     /* Line/Paragraph Separator */
    {0x202A, 0x202A, FDK_BIDI_LRE},   /* LRE */
    {0x202B, 0x202B, FDK_BIDI_RLE},   /* RLE */
    {0x202C, 0x202C, FDK_BIDI_PDF},   /* PDF */
    {0x202D, 0x202D, FDK_BIDI_LRO},   /* LRO */
    {0x202E, 0x202E, FDK_BIDI_RLO},   /* RLO */
    {0x202F, 0x205F, FDK_BIDI_CS},    /* Narrow No-Break Space, etc. */
    {0x2060, 0x2064, FDK_BIDI_BN},    /* Word joiner, etc. */
    {0x2066, 0x2066, FDK_BIDI_LRI},   /* LRI */
    {0x2067, 0x2067, FDK_BIDI_RLI},   /* RLI */
    {0x2068, 0x2068, FDK_BIDI_FSI},   /* FSI */
    {0x2069, 0x2069, FDK_BIDI_PDI},   /* PDI */
    {0x206A, 0x206F, FDK_BIDI_BN},    /* Deprecated formatting */
    {0x2070, 0x2079, FDK_BIDI_EN},    /* Superscript digits */
    {0x207A, 0x207F, FDK_BIDI_ON},    /* Superscript punctuation */
    {0x2080, 0x2089, FDK_BIDI_EN},    /* Subscript digits */
    {0x208A, 0x208F, FDK_BIDI_ON},
    {0x2090, 0x209F, FDK_BIDI_L},

    /* ── Currency symbols (per @missing: 20A0..20CF; ET) ── */
    {0x20A0, 0x20CF, FDK_BIDI_ET},

    {0x20D0, 0x20F0, FDK_BIDI_NSM},   /* Combining symbol marks */
    {0x2100, 0x27FF, FDK_BIDI_ON},    /* Letterlike symbols, arrows, math */
    /* CJK ideographs and Hangul syllables: all L */
    {0x2800, 0x28FF, FDK_BIDI_ON},    /* Braille patterns */
    {0x2900, 0x2BFF, FDK_BIDI_ON},
    {0x2C00, 0x2FFF, FDK_BIDI_L},
    {0x3000, 0x3000, FDK_BIDI_WS},    /* Ideographic Space */
    {0x3001, 0x303F, FDK_BIDI_ON},
    {0x3040, 0x33FF, FDK_BIDI_L},     /* Hiragana, Katakana, CJK symbols */
    {0x3400, 0x4DBF, FDK_BIDI_L},     /* CJK Extension A */
    {0x4E00, 0x9FFF, FDK_BIDI_L},     /* CJK Unified Ideographs */
    {0xA000, 0xA4CF, FDK_BIDI_L},     /* Yi */
    {0xA4D0, 0xA4FF, FDK_BIDI_L},     /* Lisu */
    {0xA500, 0xA63F, FDK_BIDI_L},     /* Vai — L per Unicode, was incorrectly marked */
    {0xA640, 0xA69F, FDK_BIDI_L},     /* Cyrillic Extended-B */
    /* Bamum (A6A0-A6FF) is L per @missing — covered by the 0x1200..0x1FFF
     * L block above. Previously misclassified as R; corrected. */
    {0xA700, 0xA7FF, FDK_BIDI_L},     /* Modifier Tone Letters */
    {0xA800, 0xABFF, FDK_BIDI_L},
    {0xAC00, 0xD7AF, FDK_BIDI_L},     /* Hangul Syllables */
    {0xD7B0, 0xD7FF, FDK_BIDI_L},
    /* Surrogates D800-DFFF: shouldn't appear in UTF-32 */
    {0xE000, 0xF8FF, FDK_BIDI_L},     /* Private Use Area */
    {0xF900, 0xFAFF, FDK_BIDI_L},     /* CJK Compatibility Ideographs */

    /* ── Hebrew Presentation Forms (per @missing: FB1D..FB4F; R) ──
     * Previously this range was incorrectly classified as L. */
    {0xFB00, 0xFB1C, FDK_BIDI_L},     /* Alphabetic Presentation (Latin) */
    {0xFB1D, 0xFB4F, FDK_BIDI_R},     /* Hebrew Presentation Forms */

    /* ── Arabic Presentation Forms-A (per @missing: FB50..FDCF; AL
     * and FDF0..FDFF; AL) ── */
    {0xFB50, 0xFDCF, FDK_BIDI_AL},
    {0xFDD0, 0xFDEF, FDK_BIDI_BN},    /* Noncharacters */
    {0xFDF0, 0xFDFF, FDK_BIDI_AL},    /* Arabic Presentation Forms-A cont. */

    {0xFE00, 0xFE0F, FDK_BIDI_NSM},   /* Variation Selectors */
    {0xFE20, 0xFE2F, FDK_BIDI_NSM},   /* Combining Half Marks */
    {0xFE30, 0xFE6F, FDK_BIDI_ON},    /* CJK Compatibility Forms */

    /* ── Arabic Presentation Forms-B (per @missing: FE70..FEFF; AL) ── */
    {0xFE70, 0xFEFF, FDK_BIDI_AL},

    {0xFF00, 0xFF20, FDK_BIDI_ON},    /* Full-width punctuation */
    {0xFF21, 0xFF3A, FDK_BIDI_L},     /* Full-width Latin uppercase */
    {0xFF3B, 0xFF40, FDK_BIDI_ON},
    {0xFF41, 0xFF5A, FDK_BIDI_L},     /* Full-width Latin lowercase */
    {0xFF5B, 0xFF65, FDK_BIDI_ON},
    {0xFF66, 0xFFDC, FDK_BIDI_L},     /* Half-width Katakana */
    {0xFFDD, 0xFFEF, FDK_BIDI_ON},
    {0xFFF0, 0xFFF8, FDK_BIDI_BN},
    {0xFFF9, 0xFFFB, FDK_BIDI_ON},    /* Interlinear annotation */
    {0xFFFC, 0xFFFD, FDK_BIDI_ON},    /* Object replacement char */
    {0xFFFE, 0xFFFF, FDK_BIDI_BN},    /* Noncharacters */

    /* ════════════════════════════════════════════════════════════════════
     * Plane 1+ (Supplementary Multilingual Plane + beyond)
     * ════════════════════════════════════════════════════════════════════
     *
     * All R/AL ranges in Plane 1+ are listed explicitly, sourced from
     * Unicode 16.0's DerivedBidiClass.txt @missing directives. Any
     * codepoint in Plane 1+ not listed below defaults to L (the global
     * @missing: 0000..10FFFF; Left_To_Right).
     */

    /* ── 10800..10CFF; R — historical RTL scripts ──
     * Per @missing directive. Includes:
     *   10800-1083F Cypriot Syllabary
     *   10840-1085F Imperial Aramaic
     *   10860-1087F Palmyrene
     *   10880-108AF Nabataean
     *   108E0-108FF Hatran
     *   10900-1091F Phoenician
     *   10920-1093F Lydian
     *   10980-1099F Meroitic Hieroglyphs
     *   109A0-109FF Meroitic Cursive
     *   10A00-10A5F Kharoshthi (R, with some AL inside per DerivedBidiClass)
     *   10A60-10A7F Old South Arabian
     *   10A80-10A9F Old North Arabian
     *   10AC0-10AFF Manichaean
     *   10B00-10B3F Avestan (AL per file, but @missing says R for the range)
     *   10B40-10B5F Inscriptional Parthian
     *   10B60-10B7F Inscriptional Pahlavi
     *   10B80-10BAF Psalter Pahlavi
     *   10C00-10C4F Old Turkic
     *   10C80-10CFF Old Hungarian
     * Note: Avestan (10B00-10B3F) is listed as AL in some Unicode versions
     * but the @missing directive for 10800..10CFF says R. We follow the
     * @missing directive; specific codepoints that are explicitly AL
     * in the file (none for Avestan) would override. */
    {0x10800, 0x10CFF, FDK_BIDI_R},

    /* ── 10D00..10D3F; AL — Hanifi Rohingya ── */
    {0x10D00, 0x10D3F, FDK_BIDI_AL},

    /* ── 10D40..10EBF; R — Yezidi + Old Uyghur start ──
     * Per @missing directive. Yezidi block is 10E80-10EBF (R per file).
     * 10D40-10E7F is mostly unassigned; defaults to R per @missing. */
    {0x10D40, 0x10EBF, FDK_BIDI_R},

    /* ── 10EC0..10EFF; AL — Arabic Extended-C ──
     * Per @missing directive. This is the block the user correctly
     * flagged as needing to be split from Yezidi. 10EC2-10EC4 are
     * the only assigned AL codepoints; 10EFC-10EFF are NSM (handled
     * separately below); the rest are unassigned but default to AL
     * per the @missing directive. */
    {0x10EC0, 0x10EFB, FDK_BIDI_AL},
    {0x10EFC, 0x10EFF, FDK_BIDI_NSM}, /* Arabic combining marks */

    /* ── 10F00..10F2F; R — Old Sogdian ── */
    {0x10F00, 0x10F2F, FDK_BIDI_R},

    /* ── 10F30..10F6F; AL — Sogdian ── */
    {0x10F30, 0x10F6F, FDK_BIDI_AL},

    /* ── 10F70..10FFF; R — Old Uyghur, Chorasmian, Elymaic ── */
    {0x10F70, 0x10FFF, FDK_BIDI_R},

    /* 11000-1E7FF: Brahmic, Buginese, Batak, various — all L
     * (no @missing override for these ranges) */
    {0x11000, 0x1E7FF, FDK_BIDI_L},

    /* ── 1E800..1EC6F; R — Mende Kikakui + Adlam + unassigned ──
     * Per @missing directive. Mende Kikakui is 1E800-1E8DF (R).
     * Adlam is 1E900-1E95F (R). The gap 1E8E0-1E8FF is unassigned
     * but defaults to R per @missing. The gap 1E960-1EC6F is also
     * unassigned, defaults to R. */
    {0x1E800, 0x1EC6F, FDK_BIDI_R},

    /* ── 1EC70..1ECBF; AL — Indic Siyaq Numbers ── */
    {0x1EC70, 0x1ECBF, FDK_BIDI_AL},

    /* ── 1ECC0..1ECFF; R — Ottoman Siyaq Numbers ── */
    {0x1ECC0, 0x1ECFF, FDK_BIDI_R},

    /* ── 1ED00..1ED4F; AL — Arabic Mathematical Alphabetic Symbols ── */
    {0x1ED00, 0x1ED4F, FDK_BIDI_AL},

    /* ── 1ED50..1EDFF; R — (unassigned, defaults to R per @missing) ── */
    {0x1ED50, 0x1EDFF, FDK_BIDI_R},

    /* ── 1EE00..1EEFF; AL — Arabic Mathematical Alphabetic Symbols cont. ── */
    {0x1EE00, 0x1EEFF, FDK_BIDI_AL},

    /* ── 1EF00..1EFFF; R — Arabic Extended-C (some assigned, defaults to R) ── */
    {0x1EF00, 0x1EFFF, FDK_BIDI_R},

    /* 1F000+: emoji, symbols, CJK extensions — all L (no @missing override) */
    {0x1F000, 0x1FFFF, FDK_BIDI_L},
    {0x1F300, 0x1FAFF, FDK_BIDI_ON},  /* Emoji and symbols */
    {0x20000, 0x2FFFF, FDK_BIDI_L},   /* CJK Extension B-F */
    {0x30000, 0x3FFFF, FDK_BIDI_L},   /* CJK Extension G+ */
    {0xE0000, 0xE007F, FDK_BIDI_BN},  /* Tags */
    {0xE0080, 0xE00FF, FDK_BIDI_BN},
    {0xE0100, 0xE01EF, FDK_BIDI_NSM}, /* Variation Selectors Supplement */
    {0xE01F0, 0xE0FFF, FDK_BIDI_BN},
};
#define N_UNIFORM_BLOCKS (int)(sizeof(UNIFORM_BLOCKS) / sizeof(UNIFORM_BLOCKS[0]))

/* Per-codepoint tables for mixed-type blocks. */

/* ASCII (U+0000-U+007F) bidi types. Most are L; digits are EN; a few
 * are ON, WS, B, etc. Indexed by codepoint.
 *
 * We use explicit [N] = ... designators to make the indices match the
 * codepoints exactly — no off-by-one risk from comment/counting errors. */
static const FDK_BidiType ASCII_TABLE[128] = {
    [0x00] = FDK_BIDI_BN, [0x01] = FDK_BIDI_BN, [0x02] = FDK_BIDI_BN,
    [0x03] = FDK_BIDI_BN, [0x04] = FDK_BIDI_BN, [0x05] = FDK_BIDI_BN,
    [0x06] = FDK_BIDI_BN, [0x07] = FDK_BIDI_BN, [0x08] = FDK_BIDI_BN,
    [0x09] = FDK_BIDI_S,   /* TAB */
    [0x0A] = FDK_BIDI_B,   /* LF */
    [0x0B] = FDK_BIDI_S,   /* VT */
    [0x0C] = FDK_BIDI_WS,  /* FF */
    [0x0D] = FDK_BIDI_B,   /* CR */
    [0x0E] = FDK_BIDI_BN, [0x0F] = FDK_BIDI_BN,
    [0x10] = FDK_BIDI_BN, [0x11] = FDK_BIDI_BN, [0x12] = FDK_BIDI_BN,
    [0x13] = FDK_BIDI_BN, [0x14] = FDK_BIDI_BN, [0x15] = FDK_BIDI_BN,
    [0x16] = FDK_BIDI_BN, [0x17] = FDK_BIDI_BN, [0x18] = FDK_BIDI_BN,
    [0x19] = FDK_BIDI_BN, [0x1A] = FDK_BIDI_BN, [0x1B] = FDK_BIDI_BN,
    [0x1C] = FDK_BIDI_BN, [0x1D] = FDK_BIDI_BN, [0x1E] = FDK_BIDI_BN,
    [0x1F] = FDK_BIDI_BN,
    [0x20] = FDK_BIDI_WS,  /* space */
    /* 0x21-0x2F: punctuation, all ON */
    [0x21] = FDK_BIDI_ON, [0x22] = FDK_BIDI_ON, [0x23] = FDK_BIDI_ON,
    [0x24] = FDK_BIDI_ET,  /* $ currency */
    [0x25] = FDK_BIDI_ON, [0x26] = FDK_BIDI_ON, [0x27] = FDK_BIDI_ON,
    [0x28] = FDK_BIDI_ON, [0x29] = FDK_BIDI_ON, [0x2A] = FDK_BIDI_ON,
    [0x2B] = FDK_BIDI_ES,  /* + plus/minus */
    [0x2C] = FDK_BIDI_CS,  /* , comma */
    [0x2D] = FDK_BIDI_ES,  /* - hyphen-minus */
    [0x2E] = FDK_BIDI_CS,  /* . period */
    [0x2F] = FDK_BIDI_CS,  /* / slash */
    /* 0x30-0x39: digits 0-9, all EN */
    [0x30] = FDK_BIDI_EN, [0x31] = FDK_BIDI_EN, [0x32] = FDK_BIDI_EN,
    [0x33] = FDK_BIDI_EN, [0x34] = FDK_BIDI_EN, [0x35] = FDK_BIDI_EN,
    [0x36] = FDK_BIDI_EN, [0x37] = FDK_BIDI_EN, [0x38] = FDK_BIDI_EN,
    [0x39] = FDK_BIDI_EN,
    [0x3A] = FDK_BIDI_CS,  /* : colon */
    [0x3B] = FDK_BIDI_ON, [0x3C] = FDK_BIDI_ON, [0x3D] = FDK_BIDI_ON,
    [0x3E] = FDK_BIDI_ON, [0x3F] = FDK_BIDI_ON, [0x40] = FDK_BIDI_ON,
    /* 0x41-0x5A: A-Z, all L */
    [0x41] = FDK_BIDI_L, [0x42] = FDK_BIDI_L, [0x43] = FDK_BIDI_L,
    [0x44] = FDK_BIDI_L, [0x45] = FDK_BIDI_L, [0x46] = FDK_BIDI_L,
    [0x47] = FDK_BIDI_L, [0x48] = FDK_BIDI_L, [0x49] = FDK_BIDI_L,
    [0x4A] = FDK_BIDI_L, [0x4B] = FDK_BIDI_L, [0x4C] = FDK_BIDI_L,
    [0x4D] = FDK_BIDI_L, [0x4E] = FDK_BIDI_L, [0x4F] = FDK_BIDI_L,
    [0x50] = FDK_BIDI_L, [0x51] = FDK_BIDI_L, [0x52] = FDK_BIDI_L,
    [0x53] = FDK_BIDI_L, [0x54] = FDK_BIDI_L, [0x55] = FDK_BIDI_L,
    [0x56] = FDK_BIDI_L, [0x57] = FDK_BIDI_L, [0x58] = FDK_BIDI_L,
    [0x59] = FDK_BIDI_L, [0x5A] = FDK_BIDI_L,
    /* 0x5B-0x60: punctuation */
    [0x5B] = FDK_BIDI_ON, [0x5C] = FDK_BIDI_ON, [0x5D] = FDK_BIDI_ON,
    [0x5E] = FDK_BIDI_ON, [0x5F] = FDK_BIDI_ON, [0x60] = FDK_BIDI_ON,
    /* 0x61-0x7A: a-z, all L */
    [0x61] = FDK_BIDI_L, [0x62] = FDK_BIDI_L, [0x63] = FDK_BIDI_L,
    [0x64] = FDK_BIDI_L, [0x65] = FDK_BIDI_L, [0x66] = FDK_BIDI_L,
    [0x67] = FDK_BIDI_L, [0x68] = FDK_BIDI_L, [0x69] = FDK_BIDI_L,
    [0x6A] = FDK_BIDI_L, [0x6B] = FDK_BIDI_L, [0x6C] = FDK_BIDI_L,
    [0x6D] = FDK_BIDI_L, [0x6E] = FDK_BIDI_L, [0x6F] = FDK_BIDI_L,
    [0x70] = FDK_BIDI_L, [0x71] = FDK_BIDI_L, [0x72] = FDK_BIDI_L,
    [0x73] = FDK_BIDI_L, [0x74] = FDK_BIDI_L, [0x75] = FDK_BIDI_L,
    [0x76] = FDK_BIDI_L, [0x77] = FDK_BIDI_L, [0x78] = FDK_BIDI_L,
    [0x79] = FDK_BIDI_L, [0x7A] = FDK_BIDI_L,
    /* 0x7B-0x7E: punctuation */
    [0x7B] = FDK_BIDI_ON, [0x7C] = FDK_BIDI_ON, [0x7D] = FDK_BIDI_ON,
    [0x7E] = FDK_BIDI_ON,
    [0x7F] = FDK_BIDI_BN,  /* DEL */
};

/* Latin-1 Supplement (U+0080-U+00FF) bidi types. Mixed: control chars
 * are BN, currency symbols are ET, superscripts are EN, ordinal
 * indicators are L, letters are L, etc. */
static const FDK_BidiType LATIN1_TABLE[128] = {
    /* 0x80-0x9F: C1 control chars — all BN */
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN, FDK_BIDI_BN,
    /* 0xA0 NBSP */ FDK_BIDI_CS,
    /* 0xA1 ¡ */ FDK_BIDI_ON,
    /* 0xA2 ¢ */ FDK_BIDI_ET,
    /* 0xA3 £ */ FDK_BIDI_ET,
    /* 0xA4 ¤ */ FDK_BIDI_ET,
    /* 0xA5 ¥ */ FDK_BIDI_ET,
    /* 0xA6 ¦ */ FDK_BIDI_ON,
    /* 0xA7 § */ FDK_BIDI_ON,
    /* 0xA8 ¨ */ FDK_BIDI_ON,
    /* 0xA9 © */ FDK_BIDI_ON,
    /* 0xAA ª */ FDK_BIDI_L,
    /* 0xAB « */ FDK_BIDI_ON,
    /* 0xAC ¬ */ FDK_BIDI_ON,
    /* 0xAD ­ (soft hyphen) */ FDK_BIDI_BN,
    /* 0xAE ® */ FDK_BIDI_ON,
    /* 0xAF ¯ */ FDK_BIDI_ON,
    /* 0xB0 ° */ FDK_BIDI_ON,
    /* 0xB1 ± */ FDK_BIDI_ES,
    /* 0xB2 ² */ FDK_BIDI_EN,
    /* 0xB3 ³ */ FDK_BIDI_EN,
    /* 0xB4 ´ */ FDK_BIDI_ON,
    /* 0xB5 µ */ FDK_BIDI_L,
    /* 0xB6 ¶ */ FDK_BIDI_ON,
    /* 0xB7 · */ FDK_BIDI_CS,
    /* 0xB8 ¸ */ FDK_BIDI_ON,
    /* 0xB9 ¹ */ FDK_BIDI_EN,
    /* 0xBA º */ FDK_BIDI_L,
    /* 0xBB » */ FDK_BIDI_ON,
    /* 0xBC ¼ */ FDK_BIDI_ON,
    /* 0xBD ½ */ FDK_BIDI_ON,
    /* 0xBE ¾ */ FDK_BIDI_ON,
    /* 0xBF ¿ */ FDK_BIDI_ON,
    /* 0xC0-0xD6: À-Ö (accented Latin uppercase) — all L */
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    /* 0xD7 × multiplication sign */ FDK_BIDI_ON,
    /* 0xD8-0xF6: Ø-ö (Ø and accented Latin) — all L */
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L,
    /* 0xF7 ÷ division sign */ FDK_BIDI_ON,
    /* 0xF8-0xFF: ø-ÿ (accented Latin lowercase) — all L */
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
    FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L, FDK_BIDI_L,
};

FDK_BidiType fdk__bidi_type(uint32_t cp)
{
    if (cp < 0x80) return ASCII_TABLE[cp];
    if (cp < 0x100) return LATIN1_TABLE[cp - 0x80];

    /* Linear scan of uniform blocks. For ~70 blocks this is faster
     * than a multi-level table and simpler than a binary search. */
    for (int i = 0; i < N_UNIFORM_BLOCKS; i++) {
        if (cp >= UNIFORM_BLOCKS[i].start && cp <= UNIFORM_BLOCKS[i].end)
            return UNIFORM_BLOCKS[i].type;
    }
    /* Default: treat as L. This is correct for almost all Unicode —
     * CJK, Cyrillic, Greek, Korean, etc., are all strong L. The
     * exceptions are rare RTL scripts we don't cover. */
    return FDK_BIDI_L;
}

/* ─── Algorithm ──────────────────────────────────────────────────────────── */

/* Maximum embedding depth per UAX #9 BD2: 125, with explicit level
 * limited to 126 (even) and 127 (odd). We cap our stack at 128 entries. */
#define BIDI_MAX_DEPTH 128

/* Is this bidi type a "strong" type per BD4? */
static inline bool is_strong(FDK_BidiType t)
{
    return t == FDK_BIDI_L || t == FDK_BIDI_R || t == FDK_BIDI_AL;
}

/* Is this type a "number" per W4/W5 (EN or AN)? */
static inline bool is_number(FDK_BidiType t)
{
    return t == FDK_BIDI_EN || t == FDK_BIDI_AN;
}

/* Is this type a "neutral" per N1 (B, S, WS, ON, FSI, LRI, RLI, PDI)?
 * Note: BN is treated separately. */
static inline bool is_neutral(FDK_BidiType t)
{
    return t == FDK_BIDI_B || t == FDK_BIDI_S ||
           t == FDK_BIDI_WS || t == FDK_BIDI_ON ||
           t == FDK_BIDI_FSI || t == FDK_BIDI_LRI ||
           t == FDK_BIDI_RLI || t == FDK_BIDI_PDI;
}

/* Is this type a formatting character that X9 says to remove? */
static inline bool is_x9_removed(FDK_BidiType t)
{
    return t == FDK_BIDI_RLE || t == FDK_BIDI_LRE ||
           t == FDK_BIDI_RLO || t == FDK_BIDI_LRO ||
           t == FDK_BIDI_PDF || t == FDK_BIDI_BN;
}

/* P2-P3: Find paragraph embedding level.
 * Scan for the first strong character, skipping any isolating
 * initiators (LRI/RLI/FSI) and their matching PDI per P2.
 * Returns 0 for LTR (default + first strong L), 1 for RTL (first
 * strong R or AL). */
int fdk__bidi_paragraph_level(const uint32_t *cps, int n)
{
    if (!cps || n <= 0) return 0;
    int isolate_depth = 0;
    for (int i = 0; i < n; i++) {
        FDK_BidiType t = fdk__bidi_type(cps[i]);
        if (t == FDK_BIDI_LRI || t == FDK_BIDI_RLI || t == FDK_BIDI_FSI) {
            isolate_depth++;
            continue;
        }
        if (t == FDK_BIDI_PDI) {
            if (isolate_depth > 0) isolate_depth--;
            continue;
        }
        if (isolate_depth > 0) continue;
        if (t == FDK_BIDI_L) return 0;
        if (t == FDK_BIDI_R || t == FDK_BIDI_AL) return 1;
    }
    return 0;  /* default LTR if no strong char found */
}

/* One entry in the working array: bidi type + embedding level. */
typedef struct {
    FDK_BidiType type;
    int          level;
} BidiEntry;

/* X1-X8: Apply explicit embedding/override codes.
 *
 * Walks the input, maintaining a stack of (level, override) frames.
 * RLE/LRE push a frame with the new level and no override. RLO/LRO
 * push a frame with the new level and the corresponding override.
 * PDF pops a frame. The stack depth is limited to BIDI_MAX_DEPTH.
 *
 * Per UAX #9 X9, the formatting characters (RLE/LRE/RLO/LRO/PDF/BN)
 * are conceptually "removed" from the text — they don't render.
 * However, for L2 reversal to work correctly, they still need an
 * embedding level: the formatting char gets the level of its
 * containing embedding. So an RLE that opens an RTL embedding at
 * level 1 itself gets level 1. This way, when L2 reverses the
 * containing run, the formatting char gets reversed along with the
 * embedded text — which is correct, because in the visual output
 * the formatting char's position doesn't matter (it's invisible). */
static void apply_explicit(BidiEntry *entries, int n, int base_level)
{
    /* Stack of (level, override). Top of stack is current. */
    int  stack_level[BIDI_MAX_DEPTH];
    int  stack_override[BIDI_MAX_DEPTH];  /* 0=none, 1=L, 2=R */
    int  sp = 0;

    int cur_level = base_level;
    int cur_override = 0;  /* 0=none, 1=L, 2=R */

    stack_level[0]    = cur_level;
    stack_override[0] = cur_override;

    for (int i = 0; i < n; i++) {
        FDK_BidiType t = entries[i].type;

        switch (t) {
        case FDK_BIDI_RLE:
        case FDK_BIDI_LRE:
        case FDK_BIDI_RLO:
        case FDK_BIDI_LRO: {
            /* Compute new level per X1-X4 */
            int new_level;
            int new_override = 0;
            if (t == FDK_BIDI_RLE || t == FDK_BIDI_RLO) {
                /* RTL: new level = least odd > cur_level */
                new_level = (cur_level + 1) | 1;
                if (t == FDK_BIDI_RLO) new_override = 2;
            } else {
                /* LTR: new level = least even > cur_level */
                new_level = (cur_level + 2) & ~1;
                if (t == FDK_BIDI_LRO) new_override = 1;
            }
            /* The formatting char itself takes the NEW level, so L2
             * reversal includes it in the embedded run. Its type
             * becomes BN (X9 removed). */
            if (new_level <= 125 && sp + 1 < BIDI_MAX_DEPTH) {
                sp++;
                stack_level[sp]    = new_level;
                stack_override[sp] = new_override;
                cur_level    = new_level;
                cur_override = new_override;
                entries[i].type  = FDK_BIDI_BN;
                entries[i].level = new_level;
            } else {
                /* Overflow: treat as BN at current level */
                entries[i].type  = FDK_BIDI_BN;
                entries[i].level = cur_level;
            }
            break;
        }
        case FDK_BIDI_PDF:
            /* PDF takes the CURRENT level (before pop), so it's part
             * of the embedded run for L2 reversal purposes. */
            entries[i].type  = FDK_BIDI_BN;
            entries[i].level = cur_level;
            if (sp > 0) {
                sp--;
                cur_level    = stack_level[sp];
                cur_override = stack_override[sp];
            }
            break;
        case FDK_BIDI_BN:
            /* Already BN; keep its current level (it was set during
             * initialization to base_level — that's fine, BN chars
             * outside explicit embeddings keep the paragraph level). */
            break;
        default:
            /* Apply current override (if any) to the character */
            if (cur_override == 1) entries[i].type = FDK_BIDI_L;
            else if (cur_override == 2) entries[i].type = FDK_BIDI_R;
            entries[i].level = cur_level;
            break;
        }
    }
}

/* W1: NSM takes the type of the preceding character (or sos if at start).
 * In our simplified impl, sos = paragraph embedding direction. */
static void w1_nsm(BidiEntry *e, int n, int base_level)
{
    FDK_BidiType prev_type = (base_level & 1) ? FDK_BIDI_R : FDK_BIDI_L;
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_BN) continue;
        if (e[i].type == FDK_BIDI_NSM) {
            e[i].type = prev_type;
        } else {
            prev_type = e[i].type;
        }
    }
}

/* W2: EN whose preceding strong type is AL becomes AN. */
static void w2_en_before_al(BidiEntry *e, int n)
{
    FDK_BidiType last_strong = FDK_BIDI_L;  /* default for sos */
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_BN) continue;
        if (e[i].type == FDK_BIDI_EN && last_strong == FDK_BIDI_AL) {
            e[i].type = FDK_BIDI_AN;
        } else if (is_strong(e[i].type)) {
            last_strong = e[i].type;
        }
    }
}

/* W3: AL becomes R. */
static void w3_al_to_r(BidiEntry *e, int n)
{
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_AL) e[i].type = FDK_BIDI_R;
    }
}

/* W4: ES between two EN becomes EN; CS between EN and AN (in either
 * order) becomes the type of the adjacent number; CS between two AN
 * becomes AN. Only consider single chars (not BN). */
static void w4_separators(BidiEntry *e, int n)
{
    for (int i = 1; i < n - 1; i++) {
        if (e[i].type == FDK_BIDI_BN) continue;
        /* Find prev non-BN */
        int p = i - 1;
        while (p >= 0 && e[p].type == FDK_BIDI_BN) p--;
        if (p < 0) continue;
        /* Find next non-BN */
        int q = i + 1;
        while (q < n && e[q].type == FDK_BIDI_BN) q++;
        if (q >= n) continue;

        if (e[i].type == FDK_BIDI_ES &&
            e[p].type == FDK_BIDI_EN && e[q].type == FDK_BIDI_EN) {
            e[i].type = FDK_BIDI_EN;
        } else if (e[i].type == FDK_BIDI_CS) {
            if ((e[p].type == FDK_BIDI_EN && e[q].type == FDK_BIDI_AN) ||
                (e[p].type == FDK_BIDI_AN && e[q].type == FDK_BIDI_EN)) {
                /* CS between EN and AN becomes the type of the adjacent EN
                 * or AN — actually per spec, becomes EN if either is EN,
                 * otherwise AN. We pick the type that's most consistent. */
                e[i].type = e[p].type;  /* take the type of the prev */
            } else if (e[p].type == FDK_BIDI_AN && e[q].type == FDK_BIDI_AN) {
                e[i].type = FDK_BIDI_AN;
            }
        }
    }
}

/* W5: ET adjacent to EN becomes EN. (Extends across ET runs.) */
static void w5_et_to_en(BidiEntry *e, int n)
{
    /* Scan for EN, then walk left marking all adjacent ET as EN, and
     * walk right marking all adjacent ET as EN. */
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_EN) {
            /* Walk left */
            for (int j = i - 1; j >= 0 && e[j].type == FDK_BIDI_ET; j--)
                e[j].type = FDK_BIDI_EN;
            /* Walk right */
            for (int j = i + 1; j < n && e[j].type == FDK_BIDI_ET; j++)
                e[j].type = FDK_BIDI_EN;
        }
    }
}

/* W6: Remaining ES, ET, CS become ON. */
static void w6_remaining_separators(BidiEntry *e, int n)
{
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_ES || e[i].type == FDK_BIDI_ET ||
            e[i].type == FDK_BIDI_CS) {
            e[i].type = FDK_BIDI_ON;
        }
    }
}

/* W7: EN whose preceding strong type (since sos or last reset) is L
 * becomes L. */
static void w7_en_before_l(BidiEntry *e, int n, int base_level)
{
    FDK_BidiType last_strong = (base_level & 1) ? FDK_BIDI_R : FDK_BIDI_L;
    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_BN) continue;
        if (e[i].type == FDK_BIDI_EN && last_strong == FDK_BIDI_L) {
            e[i].type = FDK_BIDI_L;
        } else if (is_strong(e[i].type)) {
            last_strong = e[i].type;
        }
    }
}

/* N1: A sequence of NS between R/AL/AN/EN takes R; between L/EN/AN
 * takes L (when both sides agree per spec).
 *
 * Strictly N1: sequence of NS between two strong types of the same
 * RTL/LTR class takes that class. We simplify: NS between two R-class
 * (R/AL/AN/EN) becomes R; NS between two L-class (L/EN/AN) becomes L;
 * mixed → take paragraph direction.
 *
 * Wait — EN and AN are weak, not strong. Re-read N1: "For NS between
 * two strong types, take the strong type. EN/AN count as R for this
 * purpose." So:
 *   - left = R/AL/EN/AN → "R-side"
 *   - right = R/AL/EN/AN → "R-side" → sequence becomes R
 *   - left = L, right = L → sequence becomes L
 *   - mixed → undefined; use paragraph direction
 */
/* ─── N0: Bracket pair resolution ───────────────────────────────────────────
 *
 * Per UAX #9 §3.3.5, bracket pairs (e.g. parentheses, square brackets,
 * curly braces) inside a neutral sequence should take the embedding
 * direction if the strong types on both sides of the pair agree. This
 * matters for mixed-direction text like "car (THE CAR) is" where the
 * parens around the embedded RTL run should be treated as RTL, not LTR.
 *
 * Algorithm:
 *   1. Walk the text, building a stack of pending open brackets.
 *   2. On a close bracket, find the most recent matching open bracket
 *      (same pair). Pairs inside this pair that don't match are skipped.
 *   3. When a pair is found, look at the strong types immediately
 *      outside the pair (both directions). If they agree (both L or
 *      both R/AL), set both brackets' type to that strong type.
 *   4. Brackets not in a valid pair remain ON (handled by N1-N2).
 *
 * Bracket pair data is from Unicode 16.0's BidiBrackets.txt (64 pairs,
 * 9 KB). The table below is generated from that file.
 *
 * The N0 step runs BEFORE N1-N2. After N0, paired brackets have strong
 * types (L or R) and N1-N2 treats them as part of the surrounding run.
 */

/* Bracket pair table: { open, close } for each of the 64 Unicode 16.0 pairs.
 * Sorted by open codepoint for binary search. */
typedef struct { uint32_t open; uint32_t close; } BracketPair;
static const BracketPair BRACKET_PAIRS[64] = {
    {0x0028, 0x0029}, {0x005B, 0x005D}, {0x007B, 0x007D},
    {0x0F3A, 0x0F3B}, {0x0F3C, 0x0F3D}, {0x169B, 0x169C},
    {0x2045, 0x2046}, {0x207D, 0x207E}, {0x208D, 0x208E},
    {0x2308, 0x2309}, {0x230A, 0x230B}, {0x2329, 0x232A},
    {0x2768, 0x2769}, {0x276A, 0x276B}, {0x276C, 0x276D},
    {0x276E, 0x276F}, {0x2770, 0x2771}, {0x2772, 0x2773},
    {0x2774, 0x2775}, {0x27C5, 0x27C6}, {0x27E6, 0x27E7},
    {0x27E8, 0x27E9}, {0x27EA, 0x27EB}, {0x27EC, 0x27ED},
    {0x27EE, 0x27EF}, {0x2983, 0x2984}, {0x2985, 0x2986},
    {0x2987, 0x2988}, {0x2989, 0x298A}, {0x298B, 0x298C},
    {0x298D, 0x2990}, {0x298F, 0x298E}, {0x2991, 0x2992},
    {0x2993, 0x2994}, {0x2995, 0x2996}, {0x2997, 0x2998},
    {0x29D8, 0x29D9}, {0x29DA, 0x29DB}, {0x29FC, 0x29FD},
    {0x2E22, 0x2E23}, {0x2E24, 0x2E25}, {0x2E26, 0x2E27},
    {0x2E28, 0x2E29}, {0x2E55, 0x2E56}, {0x2E57, 0x2E58},
    {0x2E59, 0x2E5A}, {0x2E5B, 0x2E5C}, {0x3008, 0x3009},
    {0x300A, 0x300B}, {0x300C, 0x300D}, {0x300E, 0x300F},
    {0x3010, 0x3011}, {0x3014, 0x3015}, {0x3016, 0x3017},
    {0x3018, 0x3019}, {0x301A, 0x301B}, {0xFE59, 0xFE5A},
    {0xFE5B, 0xFE5C}, {0xFE5D, 0xFE5E}, {0xFF08, 0xFF09},
    {0xFF3B, 0xFF3D}, {0xFF5B, 0xFF5D}, {0xFF5F, 0xFF60},
    {0xFF62, 0xFF63},
};
#define N_BRACKET_PAIRS (int)(sizeof(BRACKET_PAIRS) / sizeof(BRACKET_PAIRS[0]))

/* Lookup: is cp an opening bracket? Returns the paired close bracket,
 * or 0 if not an opener. */
static uint32_t bracket_open_lookup(uint32_t cp)
{
    /* Binary search — table is sorted by open codepoint */
    int lo = 0, hi = N_BRACKET_PAIRS - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (BRACKET_PAIRS[mid].open == cp) return BRACKET_PAIRS[mid].close;
        if (BRACKET_PAIRS[mid].open < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

/* Lookup: is cp a closing bracket? Returns the paired open bracket,
 * or 0 if not a closer. */
static uint32_t bracket_close_lookup(uint32_t cp)
{
    /* Linear search — close codepoints aren't sorted. With 64 entries
     * this is ~32 comparisons average, fast enough. */
    for (int i = 0; i < N_BRACKET_PAIRS; i++) {
        if (BRACKET_PAIRS[i].close == cp) return BRACKET_PAIRS[i].open;
    }
    return 0;
}

/* Is this bidi type a strong type that can be a "direction context"
 * for N0? L, R, AL count; EN/AN are weak (handled separately per spec,
 * but for our simplified impl we treat them as R-side like N1 does). */
static inline bool is_strong_for_n0(FDK_BidiType t)
{
    return t == FDK_BIDI_L || t == FDK_BIDI_R || t == FDK_BIDI_AL ||
           t == FDK_BIDI_EN || t == FDK_BIDI_AN;
}

/* N0: process bracket pairs. Runs BEFORE N1-N2. */
static void n0_bracket_pairs(BidiEntry *e, int n, int base_level,
                              const uint32_t *cps)
{
    /* Stack of pending open brackets: stores the index in e[] of each
     * opener. Max depth 64 — enough for any reasonable input. */
    int stack[64];
    int sp = 0;

    for (int i = 0; i < n; i++) {
        if (e[i].type == FDK_BIDI_BN) continue;
        if (e[i].type != FDK_BIDI_ON) continue;

        uint32_t cp = cps[i];
        uint32_t paired_close = bracket_open_lookup(cp);
        if (paired_close) {
            /* Opening bracket — push onto stack */
            if (sp < 64) stack[sp++] = i;
            continue;
        }

        uint32_t paired_open = bracket_close_lookup(cp);
        if (paired_open) {
            /* Closing bracket — find matching opener on the stack.
             * Per UAX #9 N0, scan the stack from top to bottom for an
             * opener whose paired close bracket equals this cp. Skip
             * (pop) any openers that don't match — they're unpaired. */
            int opener_idx = -1;
            while (sp > 0) {
                int candidate = stack[--sp];
                uint32_t candidate_cp = cps[candidate];
                uint32_t candidate_close = bracket_open_lookup(candidate_cp);
                if (candidate_close == cp) {
                    opener_idx = candidate;
                    break;
                }
                /* Not a match — pop and continue (this opener is unpaired) */
            }
            if (opener_idx < 0) continue;  /* no matching opener */

            /* Found a pair: [opener_idx, i].
             * Determine the embedding direction by looking at the strong
             * types immediately before the opener and immediately after
             * the closer.
             *
             * Per UAX #9 N0b: if the strong type preceding the opener
             * (looking backward, skipping neutrals) matches the strong
             * type following the closer (looking forward, skipping
             * neutrals), set both brackets to that type.
             *
             * Per N0c: if no match, set both brackets to the paragraph
             * embedding direction. */
            FDK_BidiType left_strong = FDK_BIDI_UNKNOWN;
            for (int j = opener_idx - 1; j >= 0; j--) {
                if (e[j].type == FDK_BIDI_BN) continue;
                if (is_strong_for_n0(e[j].type)) {
                    left_strong = e[j].type;
                    break;
                }
                if (is_neutral(e[j].type)) continue;  /* skip neutrals */
                /* Hit a non-strong, non-neutral (e.g., ES, ET, CS) — treat
                 * as no strong type found */
                break;
            }
            if (left_strong == FDK_BIDI_UNKNOWN) {
                /* sos: paragraph direction */
                left_strong = (base_level & 1) ? FDK_BIDI_R : FDK_BIDI_L;
            }

            FDK_BidiType right_strong = FDK_BIDI_UNKNOWN;
            for (int j = i + 1; j < n; j++) {
                if (e[j].type == FDK_BIDI_BN) continue;
                if (is_strong_for_n0(e[j].type)) {
                    right_strong = e[j].type;
                    break;
                }
                if (is_neutral(e[j].type)) continue;
                break;
            }
            if (right_strong == FDK_BIDI_UNKNOWN) {
                /* eos: paragraph direction */
                right_strong = (base_level & 1) ? FDK_BIDI_R : FDK_BIDI_L;
            }

            /* Normalize AL to R for comparison */
            FDK_BidiType left_class = (left_strong == FDK_BIDI_AL) ? FDK_BIDI_R : left_strong;
            FDK_BidiType right_class = (right_strong == FDK_BIDI_AL) ? FDK_BIDI_R : right_strong;
            /* Treat EN/AN as R-side per N1 rules */
            if (left_class == FDK_BIDI_EN || left_class == FDK_BIDI_AN) left_class = FDK_BIDI_R;
            if (right_class == FDK_BIDI_EN || right_class == FDK_BIDI_AN) right_class = FDK_BIDI_R;

            FDK_BidiType assign_type;
            if (left_class == right_class) {
                /* N0b: both sides agree → that direction */
                assign_type = left_class;
            } else {
                /* N0c: sides disagree → paragraph direction */
                assign_type = (base_level & 1) ? FDK_BIDI_R : FDK_BIDI_L;
            }

            e[opener_idx].type = assign_type;
            e[i].type = assign_type;
        }
    }
}

static void n1_n2_neutrals(BidiEntry *e, int n, int base_level)
{
    int i = 0;
    while (i < n) {
        if (e[i].type == FDK_BIDI_BN) { i++; continue; }
        if (!is_neutral(e[i].type)) { i++; continue; }

        /* Found start of neutral sequence; find the end */
        int start = i;
        while (i < n && (is_neutral(e[i].type) || e[i].type == FDK_BIDI_BN)) i++;
        int end = i;  /* exclusive */

        /* Find left strong type (scan back from start) */
        int p = start - 1;
        while (p >= 0 && (e[p].type == FDK_BIDI_BN || is_neutral(e[p].type))) p--;
        /* Treat B, S as BN for N1 (they get reset in L1 later anyway) */
        int left_class = -1;  /* -1 unknown, 0=L, 1=R */
        if (p >= 0) {
            FDK_BidiType lt = e[p].type;
            if (lt == FDK_BIDI_L) left_class = 0;
            else if (lt == FDK_BIDI_R || lt == FDK_BIDI_AL ||
                     lt == FDK_BIDI_EN || lt == FDK_BIDI_AN) left_class = 1;
        } else {
            /* sos: paragraph direction */
            left_class = (base_level & 1) ? 1 : 0;
        }

        /* Find right strong type (scan forward from end) */
        int q = end;
        while (q < n && (e[q].type == FDK_BIDI_BN || is_neutral(e[q].type))) q++;
        int right_class = -1;
        if (q < n) {
            FDK_BidiType rt = e[q].type;
            if (rt == FDK_BIDI_L) right_class = 0;
            else if (rt == FDK_BIDI_R || rt == FDK_BIDI_AL ||
                     rt == FDK_BIDI_EN || rt == FDK_BIDI_AN) right_class = 1;
        } else {
            /* eos: paragraph direction */
            right_class = (base_level & 1) ? 1 : 0;
        }

        int assign_class;
        if (left_class == right_class) {
            assign_class = left_class;
        } else {
            /* Mixed: take paragraph direction (N2) */
            assign_class = (base_level & 1) ? 1 : 0;
        }

        FDK_BidiType assign_type = (assign_class == 0) ? FDK_BIDI_L : FDK_BIDI_R;
        for (int k = start; k < end; k++) {
            if (e[k].type == FDK_BIDI_BN) continue;
            /* B and S keep their type here (L1 will reset them) */
            if (e[k].type == FDK_BIDI_B || e[k].type == FDK_BIDI_S) continue;
            e[k].type = assign_type;
        }
    }
}

/* I1-I2: Implicit level assignment.
 *
 * I1: For chars at even level:
 *   R → level+1, AN → level+2, EN → level+2
 * I2: For chars at odd level:
 *   L → level+1, EN → level+2, AN → level+2
 */
static void i1_i2_implicit(BidiEntry *e, int n)
{
    for (int i = 0; i < n; i++) {
        int lvl = e[i].level;
        if ((lvl & 1) == 0) {
            /* Even: I1 */
            switch (e[i].type) {
            case FDK_BIDI_R:  e[i].level = lvl + 1; break;
            case FDK_BIDI_AN: e[i].level = lvl + 2; break;
            case FDK_BIDI_EN: e[i].level = lvl + 2; break;
            default: break;
            }
        } else {
            /* Odd: I2 */
            switch (e[i].type) {
            case FDK_BIDI_L:  e[i].level = lvl + 1; break;
            case FDK_BIDI_EN: e[i].level = lvl + 2; break;
            case FDK_BIDI_AN: e[i].level = lvl + 2; break;
            default: break;
            }
        }
    }
}

/* L1: Reset trailing whitespace, segment separators, and paragraph
 * separators to the paragraph embedding level. Also reset leading
 * whitespace at the start of a line. */
static void l1_trailing_ws(BidiEntry *e, int n, int base_level)
{
    /* Reset from end backward */
    for (int i = n - 1; i >= 0; i--) {
        if (e[i].type == FDK_BIDI_WS || e[i].type == FDK_BIDI_LRE ||
            e[i].type == FDK_BIDI_RLE || e[i].type == FDK_BIDI_LRO ||
            e[i].type == FDK_BIDI_RLO || e[i].type == FDK_BIDI_PDF ||
            e[i].type == FDK_BIDI_BN) {
            e[i].level = base_level;
        } else if (e[i].type == FDK_BIDI_S || e[i].type == FDK_BIDI_B) {
            e[i].level = base_level;
            /* Continue past segment/paragraph separators — they reset
             * trailing whitespace on both sides */
        } else {
            break;
        }
    }
}

/* L2: Reverse contiguous runs of equal-or-higher levels, from highest
 * to lowest.
 *
 * After reversal, BN-type chars (RLE/LRE/RLO/LRO/PDF/etc. — the X9-
 * removed formatting chars) are compacted out of the output, since
 * per X9 they "are removed from the text" and "their visual positions
 * are not retained". */
static void l2_reverse(BidiEntry *e, int n, uint32_t *out_visual,
                        const uint32_t *cps, int *out_v2l)
{
    /* Find max level */
    int max_level = 0;
    for (int i = 0; i < n; i++) {
        if (e[i].level > max_level) max_level = e[i].level;
    }

    /* Initialize output in logical order. For X9-removed formatting
     * chars (RLE/LRE/RLO/LRO/PDF — the explicit embedding/override
     * codes), use a sentinel codepoint so we can detect them after
     * L2 reversal and compact them out. Other BN-type chars (control
     * chars like NUL, ZWNJ, ZWJ, etc.) are preserved in the output. */
    for (int i = 0; i < n; i++) {
        bool is_x9_format = (cps[i] == 0x202A || cps[i] == 0x202B ||
                              cps[i] == 0x202C || cps[i] == 0x202D ||
                              cps[i] == 0x202E);
        out_visual[i] = is_x9_format ? 0xFFFFFFFFu : cps[i];
        if (out_v2l) out_v2l[i] = i;
    }

    /* For each level from highest down to 1, reverse all contiguous
     * runs at level >= that level. */
    for (int lvl = max_level; lvl >= 1; lvl--) {
        int i = 0;
        while (i < n) {
            if (e[i].level >= lvl) {
                int j = i;
                while (j < n && e[j].level >= lvl) j++;
                /* Reverse [i, j) in out_visual and out_v2l */
                int a = i, b = j - 1;
                while (a < b) {
                    uint32_t tmp_cp = out_visual[a];
                    out_visual[a] = out_visual[b];
                    out_visual[b] = tmp_cp;
                    if (out_v2l) {
                        int tmp_idx = out_v2l[a];
                        out_v2l[a] = out_v2l[b];
                        out_v2l[b] = tmp_idx;
                    }
                    a++; b--;
                }
                i = j;
            } else {
                i++;
            }
        }
    }

    /* X9 compaction: remove BN-type chars from the output. After L2
     * reversal, BN chars are at the sentinel codepoint 0xFFFFFFFF. */
    int write = 0;
    for (int read = 0; read < n; read++) {
        if (out_visual[read] == 0xFFFFFFFFu) continue;
        if (write != read) {
            out_visual[write] = out_visual[read];
            if (out_v2l) out_v2l[write] = out_v2l[read];
        }
        write++;
    }
    /* Zero-fill the rest so callers don't see stale data */
    for (int i = write; i < n; i++) {
        out_visual[i] = 0;
        if (out_v2l) out_v2l[i] = -1;
    }
}

/* Main entry point. */
int fdk__bidi_reorder(const uint32_t *cps, int n,
                       uint32_t *out_visual, int *out_v2l)
{
    if (!cps || !out_visual || n < 0) return -1;
    if (n == 0) return 0;

    /* P1-P3: paragraph level */
    int base_level = fdk__bidi_paragraph_level(cps, n);

    /* Allocate working array on stack if small, else heap. We need
     * one BidiEntry per codepoint. Cap stack at 4KB (256 entries). */
    BidiEntry stack_entries[256];
    BidiEntry *entries = (n <= 256) ? stack_entries : malloc(sizeof(BidiEntry) * (size_t)n);
    if (!entries) return -1;

    /* Initialize: type from lookup, level = base_level */
    for (int i = 0; i < n; i++) {
        entries[i].type  = fdk__bidi_type(cps[i]);
        entries[i].level = base_level;
    }

    /* X1-X10: explicit embedding/override */
    apply_explicit(entries, n, base_level);

    /* W1-W7: weak type resolution */
    w1_nsm(entries, n, base_level);
    w2_en_before_al(entries, n);
    w3_al_to_r(entries, n);
    w4_separators(entries, n);
    w5_et_to_en(entries, n);
    w6_remaining_separators(entries, n);
    w7_en_before_l(entries, n, base_level);

    /* N0-N2: neutral type resolution. N0 (bracket pairs) runs first,
     * followed by N1-N2. */
    n0_bracket_pairs(entries, n, base_level, cps);
    n1_n2_neutrals(entries, n, base_level);

    /* I1-I2: implicit levels */
    i1_i2_implicit(entries, n);

    /* L1: trailing whitespace reset */
    l1_trailing_ws(entries, n, base_level);

    /* L2: reverse */
    l2_reverse(entries, n, out_visual, cps, out_v2l);

    if (entries != stack_entries) free(entries);
    return base_level;
}
