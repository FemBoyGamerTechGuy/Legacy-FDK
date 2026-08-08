/*
 * fdk-theme.c — companion CLI for FDK's theme system
 *
 * Theme files live in ~/.FDKthemes/ — drop as many .fdktheme files
 * there as you like. ~/.config/FDK/fdk.conf records which one is
 * currently active ("theme = void.fdktheme"). FDK apps read the conf
 * to find the active theme; this tool writes it.
 *
 * Everything is plain text, read/written with fopen()/fprintf().
 * No D-Bus, no GSettings, no daemons, no Red Hat ecosystem deps.
 *
 * Commands:
 *   fdk-theme list                       List themes in ~/.FDKthemes/
 *   fdk-theme set <name>                 Set active system-wide theme
 *   fdk-theme set --app <app> <name>     Set a per-app override
 *   fdk-theme unset --app <app>          Remove a per-app override
 *   fdk-theme show                       Show active theme + all overrides
 *   fdk-theme show --app <name>          Show what <app> would resolve to
 *
 * <name> is a filename inside ~/.FDKthemes/ (with or without the
 * .fdktheme extension), OR an absolute path for themes stored elsewhere.
 *
 * Known gap: there is no 'list-registered' command. fdk_theme_register()
 * stores its registry in-process only — a separate tool has no way to
 * read another process's registry.
 */
#define _POSIX_C_SOURCE 200809L
#include <fdk/fdk.h>
#include <fdk/fdk_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* ── Path helpers ─────────────────────────────────────────────────────── */

static int get_home(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fprintf(stderr, "fdk-theme: $HOME is not set, cannot continue\n");
        return -1;
    }
    snprintf(out, out_size, "%s", home);
    return 0;
}

/* mkdir -p, single level at a time (no external dependency, no shelling out) */
static int mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "fdk-theme: mkdir(%s) failed: %s\n",
                        tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "fdk-theme: mkdir(%s) failed: %s\n", tmp, strerror(errno));
        return -1;
    }
    return 0;
}

static int ensure_fdkthemes_dirs(char *home, size_t home_size)
{
    if (get_home(home, home_size) != 0) return -1;

    char base[1024], overrides[1024];
    snprintf(base, sizeof base, "%s/.FDKthemes", home);
    snprintf(overrides, sizeof overrides, "%s/.FDKthemes/overrides", home);

    if (mkdir_p(base) != 0) return -1;
    if (mkdir_p(overrides) != 0) return -1;
    return 0;
}

/* Ensure ~/.config/FDK/ exists */
static int ensure_fdk_config_dir(const char *home)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/.config/FDK", home);
    return mkdir_p(dir);
}

/* Write (or update) the "theme = <name>" line in ~/.config/FDK/fdk.conf.
 * Preserves any other keys that may exist in the file (future-proofing
 * for when subprojects add their own keys to the same conf file). */
static int write_fdk_conf_theme(const char *home, const char *theme_name)
{
    char conf_path[1024];
    snprintf(conf_path, sizeof conf_path, "%s/.config/FDK/fdk.conf", home);

    /* Read existing content so we can preserve other keys */
    char existing[8192] = {0};
    FILE *rf = fopen(conf_path, "r");
    if (rf) {
        fread(existing, 1, sizeof existing - 1, rf);
        fclose(rf);
    }

    FILE *wf = fopen(conf_path, "w");
    if (!wf) {
        fprintf(stderr, "fdk-theme: cannot write '%s': %s\n",
                conf_path, strerror(errno));
        return -1;
    }

    /* Write the theme line first */
    fprintf(wf, "theme = %s\n", theme_name);

    /* Re-write any other key=value lines (skip old theme lines) */
    char *line = existing;
    while (*line) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char tmp[1024] = {0};
        if (len < sizeof tmp) {
            memcpy(tmp, line, len);
            /* Skip blank, comment, and "theme = ..." lines */
            char *s = tmp;
            while (*s == ' ' || *s == '\t') s++;
            if (s[0] && s[0] != '#' && s[0] != ';' &&
                strncmp(s, "theme", 5) != 0)
                fprintf(wf, "%s\n", tmp);
        }
        line = nl ? nl + 1 : line + len;
        if (!nl) break;
    }

    fclose(wf);
    return 0;
}

/* Read the active theme name from ~/.config/FDK/fdk.conf.
 * Returns true and fills out_name if found, false otherwise. */
static bool read_fdk_conf_theme(const char *home, char *out_name, size_t out_size)
{
    char conf_path[1024];
    snprintf(conf_path, sizeof conf_path, "%s/.config/FDK/fdk.conf", home);
    FILE *f = fopen(conf_path, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        char *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' ||
                               end[-1] == ' '  || end[-1] == '\t'))
            *--end = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!s[0] || s[0] == '#' || s[0] == ';') continue;
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

/* Resolve a theme name/path to a full absolute path.
 * If name contains '/' it's treated as a path directly.
 * Otherwise it's looked up in ~/.FDKthemes/<name> (adding .fdktheme
 * extension if not already present). */
static bool resolve_theme_name(const char *home, const char *name,
                                char *out_path, size_t out_size)
{
    if (strchr(name, '/')) {
        /* Absolute or relative path — resolve to absolute */
        if (name[0] == '/') {
            snprintf(out_path, out_size, "%s", name);
        } else {
            char cwd[1024];
            if (!getcwd(cwd, sizeof cwd)) return false;
            snprintf(out_path, out_size, "%s/%s", cwd, name);
        }
        return true;
    }

    /* Just a name — look it up in ~/.FDKthemes/ */
    const char *ext = strrchr(name, '.');
    if (ext && strcmp(ext, ".fdktheme") == 0) {
        snprintf(out_path, out_size, "%s/.FDKthemes/%s", home, name);
    } else {
        snprintf(out_path, out_size, "%s/.FDKthemes/%s.fdktheme", home, name);
    }
    return true;
}


/* ── Validation ───────────────────────────────────────────────────────── */
/* Confirms the file can be OPENED (fdk_theme_load only returns false on
 * fopen() failure — it's deliberately lenient about content, silently
 * skipping any line it doesn't recognize rather than failing the whole
 * load). So this catches "missing/unreadable file" reliably, but a
 * file full of unrecognized garbage will still "succeed" here and
 * just resolve to plain defaults. */
static int validate_theme_file(const char *path, FDK_Theme *out_theme)
{
    FDK_Theme t = fdk_theme_faded_dream();
    if (!fdk_theme_load(&t, path)) {
        fprintf(stderr, "fdk-theme: '%s' could not be opened\n", path);
        return -1;
    }
    if (out_theme) *out_theme = t;
    return 0;
}

/* ── Commands ─────────────────────────────────────────────────────────── */

static int cmd_list(void)
{
    char home[512];
    if (get_home(home, sizeof home) != 0) return 1;

    char themes_dir[1024];
    snprintf(themes_dir, sizeof themes_dir, "%s/.FDKthemes", home);

    char active_name[256] = {0};
    read_fdk_conf_theme(home, active_name, sizeof active_name);

    printf("Themes in ~/.FDKthemes/");
    if (active_name[0]) printf("  (active: %s)", active_name);
    printf("\n\n");

    DIR *d = opendir(themes_dir);
    if (!d) {
        printf("  ~/.FDKthemes/ doesn't exist yet.\n");
        printf("  Copy some .fdktheme files there, then run:\n");
        printf("    fdk-theme set <name>\n");
        return 0;
    }

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        const char *ext = strrchr(e->d_name, '.');
        if (!ext || strcmp(ext, ".fdktheme") != 0) continue;
        count++;
        bool is_active = (strcmp(e->d_name, active_name) == 0);
        printf("  %s %-30s", is_active ? "*" : " ", e->d_name);
        char full_path[1300];
        snprintf(full_path, sizeof full_path, "%s/%s", themes_dir, e->d_name);
        FDK_Theme t;
        if (validate_theme_file(full_path, &t) == 0)
            printf("  accent=#%02X%02X%02X  radius_sm=%d",
                   t.accent.r, t.accent.g, t.accent.b, t.radius_sm);
        printf("\n");
    }
    closedir(d);

    if (count == 0) {
        printf("  (no .fdktheme files found)\n");
        printf("  Copy .fdktheme files to ~/.FDKthemes/, then run:\n");
        printf("    fdk-theme set <name>\n");
    } else {
        printf("\n  * = currently active\n");
        printf("  Use: fdk-theme set <name>\n");
    }
    return 0;
}

static int cmd_set_systemwide(const char *name)
{
    char home[512];
    if (ensure_fdkthemes_dirs(home, sizeof home) != 0) return 1;
    if (ensure_fdk_config_dir(home) != 0) return 1;

    char resolved[1024];
    if (!resolve_theme_name(home, name, resolved, sizeof resolved)) {
        fprintf(stderr, "fdk-theme: cannot resolve theme name '%s'\n", name);
        return 1;
    }
    if (validate_theme_file(resolved, NULL) != 0) {
        if (access(resolved, F_OK) != 0)
            fprintf(stderr, "fdk-theme: '%s' not found in ~/.FDKthemes/\n"
                            "  Run 'fdk-theme list' to see available themes.\n", name);
        return 1;
    }

    /* Store just the filename when it's inside ~/.FDKthemes/ */
    const char *store_name;
    char prefix[1024];
    snprintf(prefix, sizeof prefix, "%s/.FDKthemes/", home);
    if (strncmp(resolved, prefix, strlen(prefix)) == 0)
        store_name = resolved + strlen(prefix);
    else
        store_name = resolved;

    if (write_fdk_conf_theme(home, store_name) != 0) return 1;

    printf("Active theme: %s\n", store_name);
    printf("  config:     ~/.config/FDK/fdk.conf\n");
    printf("  theme file: %s\n", resolved);
    printf("Running FDK apps will pick this up within ~1 second.\n");
    return 0;
}

static int cmd_set_app(const char *app_name, const char *name)
{
    char home[512];
    if (ensure_fdkthemes_dirs(home, sizeof home) != 0) return 1;

    char resolved[1024];
    if (!resolve_theme_name(home, name, resolved, sizeof resolved)) {
        fprintf(stderr, "fdk-theme: cannot resolve theme name '%s'\n", name);
        return 1;
    }
    if (validate_theme_file(resolved, NULL) != 0) {
        if (access(resolved, F_OK) != 0)
            fprintf(stderr, "fdk-theme: '%s' not found\n"
                            "  Run 'fdk-theme list' to see available themes.\n", name);
        return 1;
    }

    char override_path[1024];
    snprintf(override_path, sizeof override_path,
             "%s/.FDKthemes/overrides/%s", home, app_name);

    FILE *f = fopen(override_path, "w");
    if (!f) {
        fprintf(stderr, "fdk-theme: cannot write '%s': %s\n",
                override_path, strerror(errno));
        return 1;
    }
    fprintf(f, "%s\n", resolved);
    fclose(f);

    printf("Per-app override set for '%s'\n", app_name);
    printf("  theme: %s\n", resolved);
    printf("'%s' will use this theme instead of the system-wide one.\n", app_name);
    return 0;
}

static int cmd_unset_app(const char *app_name)
{
    char home[512];
    if (get_home(home, sizeof home) != 0) return 1;

    char override_path[1024];
    snprintf(override_path, sizeof override_path,
             "%s/.FDKthemes/overrides/%s", home, app_name);

    if (access(override_path, F_OK) != 0) {
        printf("No override exists for '%s' (nothing to remove)\n", app_name);
        return 0;
    }
    if (remove(override_path) != 0) {
        fprintf(stderr, "fdk-theme: cannot remove '%s': %s\n",
                override_path, strerror(errno));
        return 1;
    }
    printf("Removed override for '%s'\n", app_name);
    printf("'%s' will now follow the system-wide theme.\n", app_name);
    return 0;
}

/* Print a one-line summary of a loaded theme */
static void print_theme_summary(const FDK_Theme *t)
{
    printf("    accent=#%02X%02X%02X  radius_sm=%d\n",
           t->accent.r, t->accent.g, t->accent.b, t->radius_sm);
}

static int cmd_show_all(void)
{
    char home[512];
    if (get_home(home, sizeof home) != 0) return 1;

    /* Read active theme from ~/.config/FDK/fdk.conf */
    char active_name[256] = {0};
    char active_path[1300] = {0};
    bool conf_set = read_fdk_conf_theme(home, active_name, sizeof active_name);
    if (conf_set && active_name[0]) {
        char prefix[1024];
        snprintf(prefix, sizeof prefix, "%s/.FDKthemes/", home);
        if (strchr(active_name, '/'))
            snprintf(active_path, sizeof active_path, "%s", active_name);
        else
            snprintf(active_path, sizeof active_path, "%s%s", prefix, active_name);
    }

    printf("System-wide theme:\n");
    if (!conf_set || !active_name[0]) {
        printf("  (not set — run 'fdk-theme list' then 'fdk-theme set <name>')\n");
    } else {
        printf("  name:   %s\n", active_name);
        printf("  file:   %s\n", active_path);
        printf("  config: ~/.config/FDK/fdk.conf\n");
        FDK_Theme t;
        if (validate_theme_file(active_path, &t) == 0)
            print_theme_summary(&t);
        else
            printf("    (WARNING: theme file could not be opened)\n");
    }

    char override_dir[1024];
    snprintf(override_dir, sizeof override_dir, "%s/.FDKthemes/overrides", home);

    printf("\nPer-app overrides:\n");
    DIR *d = opendir(override_dir);
    if (!d) {
        printf("  (none)\n");
        return 0;
    }

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        count++;
        char entry_path[1300];
        snprintf(entry_path, sizeof entry_path, "%s/%s", override_dir, e->d_name);

        FILE *f = fopen(entry_path, "r");
        if (!f) continue;
        char line[1024] = {0};
        if (fgets(line, sizeof line, f)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);

        printf("  %-24s  %s\n", e->d_name, line);
        FDK_Theme t;
        if (line[0] && validate_theme_file(line, &t) == 0)
            print_theme_summary(&t);
        else if (line[0])
            printf("    (WARNING: theme file could not be opened)\n");
    }
    closedir(d);
    if (count == 0) printf("  (none)\n");
    return 0;
}

static int cmd_show_app(const char *app_name)
{
    char home[512];
    if (get_home(home, sizeof home) != 0) return 1;

    char override_path[1024];
    snprintf(override_path, sizeof override_path,
             "%s/.FDKthemes/overrides/%s", home, app_name);

    printf("Resolution for '%s':\n\n", app_name);
    printf("  Tier 1 (developer fdk_theme_force): cannot be checked from\n");
    printf("    outside the app — if '%s' forces its own theme, it wins\n", app_name);
    printf("    regardless of anything shown below.\n\n");

    /* Tier 2: per-app override file */
    FILE *f = fopen(override_path, "r");
    if (f) {
        char line[1024] = {0};
        if (fgets(line, sizeof line, f)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);

        printf("  Tier 2 (per-app override): SET\n");
        printf("    %s\n", line);
        FDK_Theme t;
        if (line[0] && validate_theme_file(line, &t) == 0) {
            printf("  ");
            print_theme_summary(&t);
            printf("\n  => '%s' uses this theme.\n", app_name);
        } else {
            printf("\n  WARNING: override file points at an unreadable theme.\n");
            printf("  '%s' will fall through to tier 3 (system-wide).\n", app_name);
        }
        return 0;
    }

    printf("  Tier 2 (per-app override): not set\n\n");

    /* Tier 3: system-wide via fdk.conf */
    char active_name[256] = {0};
    char active_path[1300] = {0};
    bool conf_set = read_fdk_conf_theme(home, active_name, sizeof active_name);
    if (conf_set && active_name[0]) {
        if (strchr(active_name, '/'))
            snprintf(active_path, sizeof active_path, "%s", active_name);
        else
            snprintf(active_path, sizeof active_path,
                     "%s/.FDKthemes/%s", home, active_name);
    }

    if (conf_set && active_name[0]) {
        FDK_Theme t;
        if (validate_theme_file(active_path, &t) == 0) {
            printf("  Tier 3 (system-wide): %s\n", active_name);
            printf("    file: %s\n", active_path);
            printf("  ");
            print_theme_summary(&t);
            printf("\n  => '%s' uses this theme.\n", app_name);
            return 0;
        }
        printf("  Tier 3 (system-wide): set to '%s' but file is unreadable.\n",
               active_name);
    } else {
        printf("  Tier 3 (system-wide): not set\n");
    }

    printf("\n  => '%s' gets FDK's built-in faded-dream defaults.\n", app_name);
    printf("     Run 'fdk-theme set <name>' to set a system-wide theme.\n");
    return 0;
}

/* ── main / argument parsing ─────────────────────────────────────────── */

static void print_usage(void)
{
    fprintf(stderr,
        "fdk-theme — manage FDK themes\n"
        "\n"
        "Usage:\n"
        "  fdk-theme list                        List themes in ~/.FDKthemes/\n"
        "  fdk-theme set <name>                  Set the active system-wide theme\n"
        "  fdk-theme set --app <app> <name>      Set a per-app override\n"
        "  fdk-theme unset --app <app>           Remove a per-app override\n"
        "  fdk-theme show                        Show active theme + all overrides\n"
        "  fdk-theme show --app <app>            Show what <app> would resolve to\n"
        "\n"
        "<name> can be:\n"
        "  void              (looks up ~/.FDKthemes/void.fdktheme)\n"
        "  void.fdktheme     (same, extension optional)\n"
        "  /path/to/file.fdktheme  (absolute path, for themes outside ~/.FDKthemes/)\n"
        "\n"
        "Drop .fdktheme files into ~/.FDKthemes/ and use 'fdk-theme list'\n"
        "to see them, then 'fdk-theme set <name>' to activate one.\n"
        "\n"
        "Active theme is recorded in ~/.config/FDK/fdk.conf (plain text).\n"
        "All running FDK apps pick up a theme change within ~1 second.\n"
        "\n"
        "Note: there is no 'list-registered' command — fdk_theme_register()\n"
        "stores its data in-process only; a separate tool can't read it.\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { print_usage(); return 1; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    }

    if (strcmp(cmd, "set") == 0) {
        if (argc == 3) {
            return cmd_set_systemwide(argv[2]);
        }
        if (argc == 5 && strcmp(argv[2], "--app") == 0) {
            return cmd_set_app(argv[3], argv[4]);
        }
        fprintf(stderr, "fdk-theme: usage:\n"
                        "  fdk-theme set <name>\n"
                        "  fdk-theme set --app <app> <name>\n");
        return 1;
    }

    if (strcmp(cmd, "unset") == 0) {
        if (argc == 4 && strcmp(argv[2], "--app") == 0) {
            return cmd_unset_app(argv[3]);
        }
        fprintf(stderr, "fdk-theme: usage: fdk-theme unset --app <app>\n");
        return 1;
    }

    if (strcmp(cmd, "show") == 0) {
        if (argc == 2) return cmd_show_all();
        if (argc == 4 && strcmp(argv[2], "--app") == 0) return cmd_show_app(argv[3]);
        fprintf(stderr, "fdk-theme: usage: fdk-theme show  OR  fdk-theme show --app <app>\n");
        return 1;
    }

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    }

    fprintf(stderr, "fdk-theme: unknown command '%s'\n\n", cmd);
    print_usage();
    return 1;
}
