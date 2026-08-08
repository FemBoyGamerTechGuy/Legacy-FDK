/*
 * theme.c — FDK .fdktheme file parser and theme system
 *
 * Format overview (.fdktheme):
 *
 *   # comment
 *   ; comment
 *
 *   # Variable declaration
 *   $purple     = #8A63D2
 *   $purple_dim = #8A63D2:60   <- alpha in hex (60/FF)
 *
 *   # Global tokens
 *   bg_window  = $bg0
 *   accent     = $purple
 *   radius_sm  = 6
 *   line_height = 22
 *   font_body  = DejaVu Sans:15         <- name scan in /usr/share/fonts
 *   font_body  = /absolute/path.ttf:15  <- direct path
 *
 *   # Gradient (optional, replaces flat bg where supported)
 *   bg_gradient = linear_v #121218 #1E1E2A
 *
 *   # Shadow
 *   window_shadow = 0 8 24 #00000099
 *
 *   # Per-widget section
 *   [button]
 *   bg        = #2A2A3E
 *   bg_hover  = #3A3A52
 *   radius    = 8
 *   padding   = 8 16      <- vertical horizontal
 *   shadow    = 0 2 8 #00000060
 *
 *   # Variant
 *   [button.danger]
 *   bg        = #8B2020
 *   bg_hover  = #A83030
 *   fg        = #FFFFFF
 *
 * All keys not recognised in a section are silently skipped.
 * Variables are resolved eagerly at parse time.
 */

#define _POSIX_C_SOURCE 200809L
#include "fdk/fdk_widget.h"
#include "test_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __linux__
#  include <sys/inotify.h>
#  include <sys/eventfd.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <pthread.h>
#  include <poll.h>
#  include <errno.h>
#endif

/* ── Variable table ────────────────────────────────────────────────────────── */
#define VAR_MAX 64
#define VAR_NAME_MAX 48
#define VAR_VAL_MAX  16   /* resolved hex string, no longer than #RRGGBBAA */

typedef struct { char name[VAR_NAME_MAX]; char value[VAR_VAL_MAX]; } Var;

typedef struct {
    Var  vars[VAR_MAX];
    int  count;
} VarTable;

static void var_set(VarTable *t, const char *name, const char *val)
{
    /* Update existing */
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->vars[i].name, name) == 0) {
            snprintf(t->vars[i].value, VAR_VAL_MAX, "%s", val);
            return;
        }
    if (t->count >= VAR_MAX) return;
    snprintf(t->vars[t->count].name,  VAR_NAME_MAX, "%s", name);
    snprintf(t->vars[t->count].value, VAR_VAL_MAX,  "%s", val);
    t->count++;
}

static const char *var_get(const VarTable *t, const char *name)
{
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->vars[i].name, name) == 0)
            return t->vars[i].value;
    return NULL;
}

/* ── String helpers ────────────────────────────────────────────────────────── */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\n' || e[-1] == '\r'))
        *--e = '\0';
    return s;
}

/* ── Color parser ──────────────────────────────────────────────────────────── */
/* Accepts:  #RRGGBB  #RRGGBBAA  #RRGGBB:AA  $varname  $varname:AA */
static bool parse_color_str(const char *s, const VarTable *vars, FDK_Color *out)
{
    char resolved[VAR_VAL_MAX] = {0};
    char extra_alpha[4]        = {0};   /* from :AA suffix */

    /* Resolve variable */
    if (s[0] == '$') {
        char vname[VAR_NAME_MAX];
        const char *colon = strchr(s, ':');
        if (colon) {
            int nlen = (int)(colon - s) - 1;
            snprintf(vname, VAR_NAME_MAX, "%.*s", nlen, s + 1);
            snprintf(extra_alpha, sizeof extra_alpha, "%s", colon + 1);
        } else {
            snprintf(vname, VAR_NAME_MAX, "%s", s + 1);
        }
        const char *v = var_get(vars, vname);
        if (!v) return false;
        snprintf(resolved, VAR_VAL_MAX, "%s", v);
        s = resolved;
    }

    /* Strip leading # */
    if (*s == '#') s++;

    /* Check for :AA suffix */
    const char *colon = strchr(s, ':');
    char hex[16] = {0};
    if (colon) {
        snprintf(hex, sizeof hex, "%.*s", (int)(colon - s), s);
        snprintf(extra_alpha, sizeof extra_alpha, "%s", colon + 1);
    } else {
        snprintf(hex, sizeof hex, "%s", s);
    }

    unsigned r = 0, g = 0, b = 0, a = 255;
    int len = (int)strlen(hex);
    if      (len == 6) sscanf(hex, "%02x%02x%02x",       &r, &g, &b);
    else if (len == 8) sscanf(hex, "%02x%02x%02x%02x",   &r, &g, &b, &a);
    else               return false;

    /* :AA override */
    if (extra_alpha[0]) {
        unsigned aa = 0;
        sscanf(extra_alpha, "%02x", &aa);
        a = aa;
    }

    out->r = (uint8_t)r;
    out->g = (uint8_t)g;
    out->b = (uint8_t)b;
    out->a = (uint8_t)a;
    return true;
}

/* ── Gradient parser ──────────────────────────────────────────────────────── */
/* linear_v #RRGGBB #RRGGBB [#RRGGBB #RRGGBB] */
static bool parse_gradient(const char *val, const VarTable *vars, FDK_Gradient *out)
{
    memset(out, 0, sizeof *out);
    char buf[256];
    snprintf(buf, sizeof buf, "%s", val);
    char *tok = strtok(buf, " \t");
    if (!tok) return false;

    if      (strcmp(tok, "linear_v") == 0) out->type = FDK_GRAD_LINEAR_V;
    else if (strcmp(tok, "linear_h") == 0) out->type = FDK_GRAD_LINEAR_H;
    else return false;

    int n = 0;
    while ((tok = strtok(NULL, " \t")) && n < 4) {
        FDK_Color c = {0};
        if (!parse_color_str(tok, vars, &c)) break;
        out->stops[n].color = c;
        out->stops[n].pos   = (n == 0) ? 0.f : 1.f;  /* auto-spread later */
        n++;
    }
    /* Evenly spread stops */
    if (n > 1)
        for (int i = 0; i < n; i++)
            out->stops[i].pos = (float)i / (float)(n - 1);
    out->stop_count = n;
    return n >= 2;
}

/* ── Shadow parser ────────────────────────────────────────────────────────── */
/* offset_x offset_y blur #color */
static bool parse_shadow(const char *val, const VarTable *vars, FDK_Shadow *out)
{
    memset(out, 0, sizeof *out);
    char buf[128];
    snprintf(buf, sizeof buf, "%s", val);
    char *tok = strtok(buf, " \t");
    if (!tok) return false;
    out->offset_x = atoi(tok);
    tok = strtok(NULL, " \t"); if (!tok) return false;
    out->offset_y = atoi(tok);
    tok = strtok(NULL, " \t"); if (!tok) return false;
    out->blur     = atoi(tok);
    tok = strtok(NULL, " \t"); if (!tok) return false;
    if (!parse_color_str(tok, vars, &out->color)) return false;
    out->enabled = true;
    return true;
}

/* ── Font finder ──────────────────────────────────────────────────────────── */
/* Recursively scan a directory for a .ttf/.otf whose filename contains name */
/* Normalize a font name for matching: lowercase, strip spaces/hyphens/
 * underscores. "DejaVu Sans" and "DejaVu-Sans" and "dejavu_sans" all
 * normalize to "dejavusans". */
static void normalize_font_name(const char *in, char *out, int out_size)
{
    int i = 0;
    for (const char *p = in; *p && i < out_size - 1; p++) {
        if (*p == ' ' || *p == '-' || *p == '_') continue;
        out[i++] = (char)tolower((unsigned char)*p);
    }
    out[i] = '\0';
}

/* mode 0 = exact match only (normalized basename == normalized name)
 * mode 1 = substring fallback (normalized name is a substring of
 *          normalized basename) — used only if mode 0 finds nothing. */
static bool font_scan_dir(const char *dir, const char *name,
                           char *out_path, int out_size, int mode)
{
    DIR *d = opendir(dir);
    if (!d) return false;

    char lname[256];
    normalize_font_name(name, lname, sizeof lname);

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (font_scan_dir(path, name, out_path, out_size, mode)) {
                closedir(d); return true;
            }
            continue;
        }

        const char *ext = strrchr(e->d_name, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".ttf") != 0 && strcasecmp(ext, ".otf") != 0) continue;

        /* Normalize the basename (filename without extension) */
        char base[256];
        int blen = (int)(ext - e->d_name);
        if (blen >= (int)sizeof base) blen = (int)sizeof base - 1;
        char raw_base[256];
        snprintf(raw_base, sizeof raw_base, "%.*s", blen, e->d_name);
        char lbase[256];
        normalize_font_name(raw_base, lbase, sizeof lbase);

        bool match = (mode == 0) ? (strcmp(lbase, lname) == 0)
                                  : (lname[0] && strstr(lbase, lname));
        if (match) {
            snprintf(out_path, out_size, "%s", path);
            closedir(d); return true;
        }
    }
    closedir(d);
    return false;
}

static FDK_Font *load_font_spec(const char *spec)
{
    /* spec format:  "/path/to/font.ttf:size"  or  "Font Name:size" */
    const char *colon = strrchr(spec, ':');
    int size = 15;
    char path_or_name[512] = {0};

    if (colon) {
        int nlen = (int)(colon - spec);
        snprintf(path_or_name, sizeof path_or_name, "%.*s", nlen, spec);
        size = atoi(colon + 1);
        if (size < 6 || size > 128) size = 15;
    } else {
        snprintf(path_or_name, sizeof path_or_name, "%s", spec);
    }

    /* Direct path? */
    if (path_or_name[0] == '/') {
        return fdk_font_load(path_or_name, size);
    }

    /* Name scan */
    static const char *scan_dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        NULL
    };

    /* User-installed fonts. Per the XDG Base Directory Spec, the
     * user's data directory is $XDG_DATA_HOME if set, otherwise
     * $HOME/.local/share. Fonts live under <datadir>/fonts.
     *
     * We always compute both candidates and scan whichever is set:
     * when $XDG_DATA_HOME is explicitly set to something other than
     * the default (e.g. on a system where the user keeps their dotfiles
     * in a version-controlled tree), only that path is scanned. When
     * $XDG_DATA_HOME is unset, we fall back to the default
     * $HOME/.local/share/fonts (preserving the original behaviour). */
    char home_fonts[256] = {0};
    const char *xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && xdg_data[0] == '/') {
        /* Explicit absolute XDG_DATA_HOME — use it directly */
        snprintf(home_fonts, sizeof home_fonts,
                 "%s/fonts", xdg_data);
    } else {
        const char *home = getenv("HOME");
        if (home) snprintf(home_fonts, sizeof home_fonts,
                           "%s/.local/share/fonts", home);
    }

    char found[512] = {0};
    /* Pass 1: exact normalized-basename match (preferred — avoids
     * matching "DejaVuSans-Bold.ttf" when the spec is "DejaVu Sans"). */
    for (int i = 0; scan_dirs[i] && !found[0]; i++)
        font_scan_dir(scan_dirs[i], path_or_name, found, sizeof found, 0);
    if (!found[0] && home_fonts[0])
        font_scan_dir(home_fonts, path_or_name, found, sizeof found, 0);

    /* Pass 2: substring fallback (e.g. spec "DejaVu" matches
     * "DejaVuSans.ttf" when no font is named exactly "DejaVu"). */
    if (!found[0]) {
        for (int i = 0; scan_dirs[i] && !found[0]; i++)
            font_scan_dir(scan_dirs[i], path_or_name, found, sizeof found, 1);
        if (!found[0] && home_fonts[0])
            font_scan_dir(home_fonts, path_or_name, found, sizeof found, 1);
    }

    if (found[0]) return fdk_font_load(found, size);
    return NULL;
}

/* ── Widget type name → index ─────────────────────────────────────────────── */
static int widget_type_from_name(const char *name)
{
    static const struct { const char *n; int t; } map[] = {
        { "button",       FDK_WIDGET_BUTTON       },
        { "label",        FDK_WIDGET_LABEL        },
        { "input",        FDK_WIDGET_TEXT_INPUT   },
        { "checkbox",     FDK_WIDGET_CHECKBOX     },
        { "slider",       FDK_WIDGET_SLIDER       },
        { "progress",     FDK_WIDGET_PROGRESS_BAR },
        { "scrollview",   FDK_WIDGET_SCROLL_VIEW  },
        { "dropdown",     FDK_WIDGET_DROPDOWN     },
        { "image",        FDK_WIDGET_IMAGE        },
        { "toggle",       FDK_WIDGET_TOGGLE_BUTTON},
        { "radio",        FDK_WIDGET_RADIO_BUTTON },
        { "spinner",      FDK_WIDGET_SPINNER      },
        { "badge",        FDK_WIDGET_BADGE        },
        { "tabs",         FDK_WIDGET_TABS         },
        { "menubar",      FDK_WIDGET_MENUBAR      },
        { "textarea",     FDK_WIDGET_TEXTAREA     },
        { NULL, -1 }
    };
    for (int i = 0; map[i].n; i++)
        if (strcmp(map[i].n, name) == 0) return map[i].t;
    return -1;
}

/* ── Style block key applier ──────────────────────────────────────────────── */
static void apply_style_key(FDK_WidgetStyle *s, const char *key,
                              const char *val, const VarTable *vars)
{
#define COLOR(field, flag, name) \
    if (strcmp(key, name) == 0) { \
        if (parse_color_str(val, vars, &s->field)) { \
            s->flag = true; \
        } \
        return; \
    }

    COLOR(bg,           has_bg,           "bg")
    COLOR(bg_hover,     has_bg_hover,     "bg_hover")
    COLOR(bg_active,    has_bg_active,    "bg_active")
    COLOR(fg,           has_fg,           "fg")
    COLOR(border,       has_border,       "border")
    COLOR(border_focus, has_border_focus, "border_focus")

    if (strcmp(key, "radius") == 0) {
        s->radius = atoi(val);
        s->has_radius = true;
        return;
    }
    if (strcmp(key, "padding") == 0) {
        int v = 0, h = 0;
        if (sscanf(val, "%d %d", &v, &h) == 2) { s->pad_v = v; s->pad_h = h; }
        else { v = atoi(val); s->pad_v = s->pad_h = v; }
        s->has_pad = true;
        return;
    }
    if (strcmp(key, "shadow") == 0) {
        if (parse_shadow(val, vars, &s->shadow)) {
            s->has_shadow = true;
        }
        return;
    }
    if (strcmp(key, "gradient") == 0) {
        if (parse_gradient(val, vars, &s->gradient)) {
            s->has_gradient = true;
        }
        return;
    }
#undef COLOR
}

/* ── Global token applier ─────────────────────────────────────────────────── */
static void apply_global_key(FDK_Theme *t, const char *key,
                               const char *val, const VarTable *vars)
{
#define C(field, name) \
    if (strcmp(key, name) == 0) { parse_color_str(val, vars, &t->field); return; }
#define I(field, name) \
    if (strcmp(key, name) == 0) { t->field = atoi(val); return; }

    C(bg_window,       "bg_window")
    C(bg_widget,       "bg_widget")
    C(bg_widget_hover, "bg_widget_hover")
    C(bg_widget_active,"bg_widget_active")
    C(bg_input,        "bg_input")
    C(bg_input_focus,  "bg_input_focus")
    C(bg_selection,    "bg_selection")
    C(fg_primary,      "fg_primary")
    C(fg_secondary,    "fg_secondary")
    C(fg_disabled,     "fg_disabled")
    C(fg_on_accent,    "fg_on_accent")
    C(accent,          "accent")
    C(accent_hover,    "accent_hover")
    C(accent_active,   "accent_active")
    C(border,          "border")
    C(border_focus,    "border_focus")
    C(separator,       "separator")
    C(scrollbar_track, "scrollbar_track")
    C(scrollbar_thumb, "scrollbar_thumb")
    C(toast_info,      "toast_info")
    C(toast_success,   "toast_success")
    C(toast_warning,   "toast_warning")
    C(toast_error,     "toast_error")

    I(radius_sm,   "radius_sm")
    I(radius_md,   "radius_md")
    I(scrollbar_w, "scrollbar_w")
    I(gap_sm,      "gap_sm")
    I(gap_md,      "gap_md")
    I(pad_sm,      "pad_sm")
    I(pad_md,      "pad_md")
    I(line_height, "line_height")

    /* Gradient on window bg */
    if (strcmp(key, "bg_gradient") == 0) {
        /* store in widget_styles[0] as a sentinel — not a real widget type */
        parse_gradient(val, vars, &t->widget_styles[0].gradient);
        t->widget_styles[0].has_gradient = true;
        return;
    }
    /* Window shadow */
    if (strcmp(key, "window_shadow") == 0) {
        parse_shadow(val, vars, &t->window_shadow);
        return;
    }

    /* Font specs */
    if (strcmp(key, "font_body") == 0) {
        FDK_Font *f = load_font_spec(val);
        if (f) { fdk_font_destroy(t->font_body); t->font_body = f; }
        return;
    }
    if (strcmp(key, "font_label") == 0) {
        FDK_Font *f = load_font_spec(val);
        if (f) { fdk_font_destroy(t->font_label); t->font_label = f; }
        return;
    }
    if (strcmp(key, "font_mono") == 0) {
        FDK_Font *f = load_font_spec(val);
        if (f) { fdk_font_destroy(t->font_mono); t->font_mono = f; }
        return;
    }
#undef C
#undef I
}

/* ── Main parser ──────────────────────────────────────────────────────────── */
bool fdk_theme_load(FDK_Theme *out, const char *path)
{
    if (!out || !path) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;

    VarTable vars = {0};

    /* Current section state */
    int  section_type    = -1;   /* FDK_WidgetType, or -1 = global */
    char section_variant[32] = {0};  /* variant name, or "" */

    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!s[0] || s[0] == '#' || s[0] == ';') continue;

        /* ── Section header ── [widget]  or  [widget.variant] */
        if (s[0] == '[') {
            char *close = strchr(s, ']');
            if (!close) continue;
            *close = '\0';
            char *sec = trim(s + 1);
            char *dot = strchr(sec, '.');
            if (dot) {
                *dot = '\0';
                snprintf(section_variant, sizeof section_variant,
                         "%s", trim(dot + 1));
            } else {
                section_variant[0] = '\0';
            }
            section_type = widget_type_from_name(trim(sec));
            continue;
        }

        /* ── Key = value ── */
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        /* Strip a trailing inline comment ("key = value  # comment").
         * A '#' is only a comment marker if:
         *   - it's preceded by whitespace (not glued to the value), AND
         *   - it is NOT immediately followed by 6 or 8 hex digits
         *     (which would make it a #RRGGBB / #RRGGBBAA colour literal —
         *     this matters for multi-token values like
         *     "window_shadow = 0 8 32 #00000099" and
         *     "bg_gradient = linear_v #111111 #222222 #333333",
         *     where every '#' preceded by a space IS part of the value). */
        for (char *com = strchr(val, '#'); com; com = strchr(com + 1, '#')) {
            if (com == val || !(com[-1] == ' ' || com[-1] == '\t'))
                continue;  /* glued to value — not a comment, keep scanning */

            int hexlen = 0;
            while (isxdigit((unsigned char)com[1 + hexlen])) hexlen++;
            if (hexlen == 6 || hexlen == 8)
                continue;  /* looks like a colour literal — not a comment */

            /* Genuine trailing comment — truncate here */
            com[-1] = '\0';
            val = trim(val);
            break;
        }

        /* ── Variable declaration ── */
        if (key[0] == '$') {
            var_set(&vars, key + 1, val);
            continue;
        }

        /* Resolve leading variable in value */
        char resolved_val[256];
        if (val[0] == '$') {
            char vname[VAR_NAME_MAX];
            const char *colon = strchr(val, ':');
            if (colon) {
                snprintf(vname, VAR_NAME_MAX, "%.*s",
                         (int)(colon - val) - 1, val + 1);
                const char *v = var_get(&vars, vname);
                if (v) snprintf(resolved_val, sizeof resolved_val,
                                "%s%s", v, colon);
                else   snprintf(resolved_val, sizeof resolved_val, "%s", val);
            } else {
                snprintf(vname, VAR_NAME_MAX, "%s", val + 1);
                const char *v = var_get(&vars, vname);
                snprintf(resolved_val, sizeof resolved_val,
                         "%s", v ? v : val);
            }
            val = resolved_val;
        }

        if (section_type < 0) {
            /* Global token */
            apply_global_key(out, key, val, &vars);
        } else if (section_variant[0]) {
            /* Named variant  [widget.variant] */
            if (out->variant_count < FDK_VARIANTS_MAX) {
                /* Find or create variant slot */
                int vi = -1;
                char full_name[48];
                /* We store "type_idx:variant_name" in the name field.
                 * section_variant is char[32]; ".31s" bounds that part.
                 * %d of section_type (int) could in theory print up to 11
                 * chars (INT_MIN) per GCC's worst-case analysis, even
                 * though widget_type_from_name() only returns -1..23 —
                 * size both buffers (48) to satisfy that worst case so
                 * -Wformat-truncation has nothing to flag. */
                snprintf(full_name, sizeof full_name, "%d:%.31s",
                         section_type, section_variant);
                for (int i = 0; i < out->variant_count; i++)
                    if (strcmp(out->variants[i].name, full_name) == 0)
                        { vi = i; break; }
                if (vi < 0) {
                    vi = out->variant_count++;
                    snprintf(out->variants[vi].name,
                             sizeof out->variants[vi].name, "%s", full_name);
                }
                apply_style_key(&out->variants[vi].style, key, val, &vars);
            }
        } else {
            /* Per-widget override  [widget] */
            if (section_type < 24)
                apply_style_key(&out->widget_styles[section_type],
                                key, val, &vars);
        }
    }

    fclose(f);
    return true;
}

/* ── System-wide theme location ────────────────────────────────────────────
 *
 * Resolution order:
 *   1. $FDK_THEME_FILE — absolute override for testing or power users
 *   2. ~/.config/FDK/fdk.conf  — contains "theme = <name>.fdktheme",
 *      the name is looked up in ~/.FDKthemes/<name>.fdktheme
 *   3. ~/.FDKthemes/theme.fdktheme — direct file, backward-compatible
 *      with the old convention and usable without fdk.conf at all
 *
 * FDK deliberately does NOT use ~/.themes (legacy GTK/icon-theme
 * convention), D-Bus, GSettings, or any Red Hat ecosystem mechanism.
 * Everything is plain text files read with fopen(). */

/* Parse ~/.config/FDK/fdk.conf and extract the value of "theme = ..."
 * Returns true and fills out_name if the key is found, false otherwise. */
static bool read_fdk_conf_theme(const char *home, char *out_name, size_t out_size)
{
    char conf_path[512];
    snprintf(conf_path, sizeof conf_path, "%s/.config/FDK/fdk.conf", home);

    FILE *f = fopen(conf_path, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        /* Strip trailing newline/whitespace */
        char *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' ||
                               end[-1] == ' '  || end[-1] == '\t'))
            *--end = '\0';

        /* Skip blank lines and comments */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!s[0] || s[0] == '#' || s[0] == ';') continue;

        /* Look for "theme = <value>" */
        if (strncmp(s, "theme", 5) != 0) continue;
        s += 5;
        while (*s == ' ' || *s == '\t') s++;
        if (*s != '=') continue;
        s++;
        while (*s == ' ' || *s == '\t') s++;
        if (!s[0]) continue;

        snprintf(out_name, out_size, "%s", s);
        found = true;
        break;
    }
    fclose(f);
    return found;
}

static bool get_systemwide_path(char *out, size_t out_size)
{
    /* 1. Explicit env override — bypasses everything, useful for testing */
    const char *env = getenv("FDK_THEME_FILE");
    if (env && env[0]) {
        snprintf(out, out_size, "%s", env);
        return true;
    }

    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;

    /* 2. ~/.config/FDK/fdk.conf — "theme = <name>.fdktheme"
     *    The name is resolved to ~/.FDKthemes/<name>
     *    If the name already contains a '/' it's treated as an absolute
     *    or relative path (power-user escape hatch, same as $FDK_THEME_FILE
     *    but stored persistently in conf). */
    char theme_name[256];
    if (read_fdk_conf_theme(home, theme_name, sizeof theme_name)) {
        if (strchr(theme_name, '/')) {
            /* Absolute or relative path stored directly in conf */
            snprintf(out, out_size, "%s", theme_name);
        } else {
            /* Just a filename — look it up in ~/.FDKthemes/ */
            snprintf(out, out_size, "%s/.FDKthemes/%s", home, theme_name);
        }
        return true;
    }

    /* 3. Direct ~/.FDKthemes/theme.fdktheme — backward compat, also
     *    works fine without any fdk.conf at all */
    snprintf(out, out_size, "%s/.FDKthemes/theme.fdktheme", home);
    return true;
}

bool fdk_theme_load_system(FDK_Theme *base_theme)
{
    char path[512];
    if (!get_systemwide_path(path, sizeof path)) return false;
    return fdk_theme_load(base_theme, path);
}

/* ── Tier 1: hard developer force ──────────────────────────────────────────
 * Once set, fdk_theme_resolve() returns this unconditionally — no
 * system-wide lookup, no user-override lookup, no fallback. */
static bool      g_forced_set  = false;
static FDK_Theme g_forced_theme;

void fdk_theme_force(const FDK_Theme *theme)
{
    if (!theme) return;
    g_forced_theme = *theme;
    g_forced_set   = true;
}

void fdk_theme_force_file(const char *path)
{
    if (!path) return;
    FDK_Theme t = fdk_theme_faded_dream();
    if (fdk_theme_load(&t, path)) {
        g_forced_theme = t;
        g_forced_set   = true;
    }
}

/* ── Tier 2: developer-registered alternatives + user override file ──────────
 * Registration is informational only (lets a future settings UI list
 * what's available) — it does NOT gate what the user override file is
 * allowed to point at. The override file's path always wins if present,
 * registered or not. */
typedef struct {
    char app_name[64];
    char theme_name[48];
    char path[400];
} RegistryEntry;

static RegistryEntry g_registry[FDK_THEME_REGISTRY_MAX];
static int            g_registry_count = 0;

void fdk_theme_register(const char *app_name, const char *theme_name,
                          const char *path)
{
    if (!app_name || !theme_name || !path) return;
    /* Update existing entry for the same app_name + theme_name pair */
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].app_name, app_name) == 0 &&
            strcmp(g_registry[i].theme_name, theme_name) == 0) {
            snprintf(g_registry[i].path, sizeof g_registry[i].path, "%s", path);
            return;
        }
    }
    if (g_registry_count >= FDK_THEME_REGISTRY_MAX) return;
    RegistryEntry *e = &g_registry[g_registry_count++];
    snprintf(e->app_name,   sizeof e->app_name,   "%s", app_name);
    snprintf(e->theme_name, sizeof e->theme_name, "%s", theme_name);
    snprintf(e->path,       sizeof e->path,       "%s", path);
}

int fdk_theme_list_registered(const char *app_name,
                                const char *out_names[],
                                const char *out_paths[],
                                int max_count)
{
    if (!app_name) return 0;
    int n = 0;
    for (int i = 0; i < g_registry_count && n < max_count; i++) {
        if (strcmp(g_registry[i].app_name, app_name) != 0) continue;
        if (out_names) out_names[n] = g_registry[i].theme_name;
        if (out_paths) out_paths[n] = g_registry[i].path;
        n++;
    }
    return n;
}

/* The user-managed override file: ~/.FDKthemes/overrides/<app_name>,
 * a single line containing the path to the .fdktheme file the user
 * wants that specific app to use instead of the system-wide theme. */
static bool get_override_path(const char *app_name, char *out, size_t out_size)
{
    if (!app_name || !app_name[0]) return false;
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;

    char override_file[512];
    snprintf(override_file, sizeof override_file,
             "%s/.FDKthemes/overrides/%s", home, app_name);

    FILE *f = fopen(override_file, "r");
    if (!f) return false;

    char line[512] = {0};
    bool ok = (fgets(line, sizeof line, f) != NULL);
    fclose(f);
    if (!ok) return false;

    /* Trim trailing newline/whitespace */
    char *e = line + strlen(line);
    while (e > line && (e[-1] == '\n' || e[-1] == '\r' ||
                         e[-1] == ' '  || e[-1] == '\t'))
        *--e = '\0';
    if (!line[0]) return false;

    snprintf(out, out_size, "%s", line);
    return true;
}

/* ── Master resolver: tier 1 -> tier 2 -> tier 3 ──────────────────────────── */
/* Public API (declared in fdk_widget.h): resolves the theme AND reports
 * which file backed it via out_path (empty string for tier-1 forced
 * themes, which have no backing file). fdk_ui_create() uses this
 * internally to decide whether to auto-start fdk_theme_watch(), and
 * apps can call it directly too — e.g. to show "currently using: X" in
 * a settings UI, or to start their own watch on the same file when
 * they need to pass an explicit (non-NULL) theme to fdk_ui_create()
 * for some other reason. */
FDK_Theme fdk_theme_resolve_ex(const char *app_name, char *out_path, size_t out_size)
{
    if (out_path && out_size) out_path[0] = '\0';

    /* Tier 1: hard force — unconditional, no fallback, no watch file */
    if (g_forced_set) return g_forced_theme;

    FDK_Theme t = fdk_theme_faded_dream();

    /* Tier 2: user override file for this specific app */
    char override_path[512];
    if (app_name && get_override_path(app_name, override_path, sizeof override_path)) {
        if (fdk_theme_load(&t, override_path)) {
            if (out_path) snprintf(out_path, out_size, "%s", override_path);
            return t;
        }
        /* Override file existed but pointed at something unloadable —
         * fall through to system-wide rather than silently using
         * faded-dream defaults, so a typo doesn't look like a crash. */
    }

    /* Tier 3: system-wide ~/.FDKthemes/theme.fdktheme (or $FDK_THEME_FILE) */
    char sys_path[512];
    if (get_systemwide_path(sys_path, sizeof sys_path) &&
        fdk_theme_load(&t, sys_path)) {
        if (out_path) snprintf(out_path, out_size, "%s", sys_path);
    }
    return t;
}

FDK_Theme fdk_theme_resolve(const char *app_name)
{
    return fdk_theme_resolve_ex(app_name, NULL, 0);
}


/* ── inotify hot-reload ──────────────────────────────────────────────────── */
#ifdef __linux__

typedef struct WatchCtx {
    struct WatchCtx *next;        /* intrusive singly-linked list */
    FDK_UI     *ui;
    FDK_Widget *root;
    char        path[512];
    bool        watch_conf;       /* if true, re-resolve via fdk_theme_resolve_ex
                                   * on change rather than loading path directly */
    char        app_name[128];    /* used when watch_conf = true */
    int         ifd;
    int         wfd;
    int         stop_efd;  /* eventfd, written by whichever thread stops
                            * this watch, polled alongside ifd by
                            * watch_thread so it can wake and exit
                            * cleanly. Using a dedicated fd for the stop
                            * signal — rather than closing ifd out from
                            * under a thread blocked reading it — avoids
                            * a real, TSan-confirmed race: close() and a
                            * blocked read() on the same fd from
                            * different threads have no synchronization
                            * between them, so a third thread could in
                            * principle open a new fd that reuses the
                            * just-closed number before the blocked
                            * read() wakes, and the read() then observes
                            * an unrelated fd's data.
                            *
                            * Ownership: watch_thread is the only thread
                            * that ever calls read()/poll() on ifd, and
                            * the only one that closes ifd (right before
                            * it returns). stop_efd is different: it is
                            * only ever written by the stopping thread,
                            * never by watch_thread, and — importantly —
                            * it is also *closed* by the stopping thread
                            * (in watch_stop_and_remove_locked, right
                            * after pthread_join() returns), not by
                            * watch_thread. Closing it from watch_thread
                            * instead was this file's first attempt and
                            * still raced under TSan, because a poll()
                            * wakeup isn't a happens-before edge TSan's
                            * interceptors recognize between the write()
                            * that caused it and a close() on another
                            * thread. Same-thread write-then-close,
                            * ordered strictly after pthread_join(),
                            * gives a real happens-before edge instead. */
    pthread_t   thread;
} WatchCtx;

/* One watch per FDK_UI, not one watch per process. g_watches is a
 * linked list so multi-window apps (each with their own FDK_UI) can
 * each hold a live watch simultaneously — starting a new watch for a
 * given ui only tears down that ui's previous watch, not anyone
 * else's. All list mutation happens under g_watches_lock; the watch
 * threads themselves never touch the list, only their own ctx, so no
 * locking is needed inside watch_thread(). */
static WatchCtx        *g_watches      = NULL;
static pthread_mutex_t  g_watches_lock = PTHREAD_MUTEX_INITIALIZER;

static void *watch_thread(void *arg)
{
    WatchCtx *ctx = arg;
    char buf[sizeof(struct inotify_event) + 256];

    struct pollfd pfds[2];
    pfds[0].fd = ctx->ifd;
    pfds[0].events = POLLIN;
    pfds[1].fd = ctx->stop_efd;
    pfds[1].events = POLLIN;

    for (;;) {
        int pret = poll(pfds, 2, -1);   /* block until either fd is ready */
        if (pret < 0) {
            if (errno == EINTR) continue;
            break;   /* unexpected poll() failure — exit defensively */
        }
        if (pfds[1].revents & POLLIN) break;   /* stop signal */
        if (!(pfds[0].revents & POLLIN)) continue;

        ssize_t n = read(ctx->ifd, buf, sizeof buf);
        if (n <= 0) continue;   /* spurious wakeup or transient error */

        FDK_Theme t;
        if (ctx->watch_conf) {
            /* fdk.conf changed — re-run full three-tier resolution so
             * whatever name fdk.conf now points at gets loaded. */
            t = fdk_theme_resolve_ex(ctx->app_name[0] ? ctx->app_name : NULL,
                                      NULL, 0);
        } else {
            /* A specific theme file changed — reload it directly. */
            t = fdk_theme_faded_dream();
            fdk_theme_load(&t, ctx->path);
        }
        fdk_ui_set_theme(ctx->ui, ctx->root, &t);
    }

    /* ctx->ifd is exclusively owned by this thread from creation until
     * here — safe to close without racing anything. ctx->stop_efd is
     * deliberately NOT closed here: see watch_stop_and_remove_locked
     * for why. */
    inotify_rm_watch(ctx->ifd, ctx->wfd);
    close(ctx->ifd);
    return NULL;
}

/* Stop and free the watch (if any) registered for `ui`, unlinking it
 * from g_watches first. Must be called with g_watches_lock held.
 * A no-op if no watch exists for `ui`. */
static void watch_stop_and_remove_locked(FDK_UI *ui)
{
    WatchCtx **link = &g_watches;
    while (*link) {
        WatchCtx *ctx = *link;
        if (ctx->ui == ui) {
            *link = ctx->next;   /* unlink first — once unlinked, nothing
                                   * else can find this ctx */
            /* Signal watch_thread to stop via its dedicated eventfd
             * rather than closing ctx->ifd out from under it — see the
             * ownership note on WatchCtx.stop_efd. */
            uint64_t one = 1;
            ssize_t w = write(ctx->stop_efd, &one, sizeof one);
            (void)w;   /* best-effort: an 8-byte eventfd write this far
                        * below UINT64_MAX practically cannot fail; if it
                        * somehow did, pthread_join below would hang,
                        * which is no worse than the old design's
                        * failure mode and not worth complicating this
                        * path to guard against */
            pthread_join(ctx->thread, NULL);
            /* Close stop_efd here, from this same thread, after the
             * join — not from inside watch_thread. pthread_join()
             * guarantees watch_thread already made its last poll()
             * touch of stop_efd and has now exited, so this close()
             * is strictly ordered after every access watch_thread
             * ever made to this fd. Closing it from watch_thread's
             * own exit path instead (this file's first attempt) still
             * raced under TSan: a poll() wakeup isn't a happens-before
             * edge TSan's interceptors recognize between the write()
             * that caused it and a close() on a different thread, even
             * though the kernel ordering is genuinely safe — so the
             * close has to happen on the same thread that wrote,
             * strictly after the join, to get a real, TSan-visible
             * happens-before edge. */
            close(ctx->stop_efd);
            free(ctx);
            return;
        }
        link = &ctx->next;
    }
}

void fdk_theme_watch(FDK_UI *ui, FDK_Widget *root, const char *path)
{
    pthread_mutex_lock(&g_watches_lock);
    watch_stop_and_remove_locked(ui);   /* replace any existing watch for this ui */
    if (!path) {
        pthread_mutex_unlock(&g_watches_lock);
        return;
    }

    int ifd = inotify_init1(IN_NONBLOCK);
    if (ifd < 0) { pthread_mutex_unlock(&g_watches_lock); return; }
    int wfd = inotify_add_watch(ifd, path,
                                 IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wfd < 0) { close(ifd); pthread_mutex_unlock(&g_watches_lock); return; }

    int flags = fcntl(ifd, F_GETFL);
    fcntl(ifd, F_SETFL, flags & ~O_NONBLOCK);

    int stop_efd = eventfd(0, EFD_NONBLOCK);
    if (stop_efd < 0) { close(ifd); pthread_mutex_unlock(&g_watches_lock); return; }

    WatchCtx *ctx = calloc(1, sizeof *ctx);
    if (!ctx) { close(ifd); close(stop_efd); pthread_mutex_unlock(&g_watches_lock); return; }
    ctx->ui         = ui;
    ctx->root       = root;
    ctx->ifd        = ifd;
    ctx->wfd        = wfd;
    ctx->stop_efd   = stop_efd;
    ctx->watch_conf = false;
    snprintf(ctx->path, sizeof ctx->path, "%s", path);

    if (pthread_create(&ctx->thread, NULL, watch_thread, ctx) != 0) {
        inotify_rm_watch(ifd, wfd);
        close(ifd);
        close(stop_efd);
        free(ctx);
        pthread_mutex_unlock(&g_watches_lock);
        return;
    }

    ctx->next = g_watches;
    g_watches = ctx;
    pthread_mutex_unlock(&g_watches_lock);
}

/* Watch ~/.config/FDK/fdk.conf for changes. When fdk-theme set <name>
 * is run, it rewrites fdk.conf — this watch fires, re-runs the full
 * three-tier resolution, and applies whatever theme fdk.conf now names.
 * This is the right watch target for apps that want to respond to the
 * user changing the system-wide theme at runtime via fdk-theme.
 *
 * app_name is used for the tier-2 per-app override check in the
 * re-resolution — pass NULL to skip tier-2 and only respond to tier-3
 * system-wide changes. */
void fdk_theme_watch_conf(FDK_UI *ui, FDK_Widget *root, const char *app_name)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;

    char conf_path[512];
    snprintf(conf_path, sizeof conf_path, "%s/.config/FDK/fdk.conf", home);

    pthread_mutex_lock(&g_watches_lock);
    watch_stop_and_remove_locked(ui);   /* replace any existing watch for this ui */

    int ifd = inotify_init1(IN_NONBLOCK);
    if (ifd < 0) { pthread_mutex_unlock(&g_watches_lock); return; }
    int wfd = inotify_add_watch(ifd, conf_path,
                                 IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wfd < 0) { close(ifd); pthread_mutex_unlock(&g_watches_lock); return; }

    int flags = fcntl(ifd, F_GETFL);
    fcntl(ifd, F_SETFL, flags & ~O_NONBLOCK);

    int stop_efd = eventfd(0, EFD_NONBLOCK);
    if (stop_efd < 0) { close(ifd); pthread_mutex_unlock(&g_watches_lock); return; }

    WatchCtx *ctx = calloc(1, sizeof *ctx);
    if (!ctx) { close(ifd); close(stop_efd); pthread_mutex_unlock(&g_watches_lock); return; }
    ctx->ui         = ui;
    ctx->root       = root;
    ctx->ifd        = ifd;
    ctx->wfd        = wfd;
    ctx->stop_efd   = stop_efd;
    ctx->watch_conf = true;
    snprintf(ctx->path, sizeof ctx->path, "%s", conf_path);
    if (app_name)
        snprintf(ctx->app_name, sizeof ctx->app_name, "%s", app_name);

    if (pthread_create(&ctx->thread, NULL, watch_thread, ctx) != 0) {
        inotify_rm_watch(ifd, wfd);
        close(ifd);
        close(stop_efd);
        free(ctx);
        pthread_mutex_unlock(&g_watches_lock);
        return;
    }

    ctx->next = g_watches;
    g_watches = ctx;
    pthread_mutex_unlock(&g_watches_lock);
}

/* Test-only introspection — see test_internal.h. */
size_t fdk__theme_watch_count(void)
{
    pthread_mutex_lock(&g_watches_lock);
    size_t n = 0;
    for (WatchCtx *c = g_watches; c; c = c->next) n++;
    pthread_mutex_unlock(&g_watches_lock);
    return n;
}

bool fdk__theme_watch_exists(FDK_UI *ui)
{
    pthread_mutex_lock(&g_watches_lock);
    bool found = false;
    for (WatchCtx *c = g_watches; c; c = c->next)
        if (c->ui == ui) { found = true; break; }
    pthread_mutex_unlock(&g_watches_lock);
    return found;
}

#else
void fdk_theme_watch(FDK_UI *ui, FDK_Widget *root, const char *path)
{
    (void)ui; (void)root; (void)path;
    /* inotify unavailable on this platform */
}
void fdk_theme_watch_conf(FDK_UI *ui, FDK_Widget *root, const char *app_name)
{
    (void)ui; (void)root; (void)app_name;
}
size_t fdk__theme_watch_count(void) { return 0; }
bool   fdk__theme_watch_exists(FDK_UI *ui) { (void)ui; return false; }
#endif /* __linux__ */
