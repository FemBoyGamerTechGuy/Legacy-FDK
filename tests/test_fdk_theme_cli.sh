#!/bin/bash
# test_fdk_theme_cli.sh — automated ctest for the fdk-theme CLI
#
# Exercises the full set/list/show/unset cycle against a throwaway $HOME
# so we don't touch the developer's real ~/.FDKthemes or ~/.config/FDK.
# Uses only the built fdk-theme binary plus standard coreutils.

set -u

# Resolve script source dir up-front (used later for locating bundled themes)
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Locate the fdk-theme binary relative to this script ────────────────
# ctest runs this from the build dir; the binary lives at ../tools/fdk-theme/fdk-theme
# from the tests/ subdir. Also accept an absolute override via $FDK_THEME_BIN.
BIN="${FDK_THEME_BIN:-}"
if [ -z "$BIN" ]; then
    for cand in \
        "$SRC_DIR/../tools/fdk-theme/fdk-theme" \
        "$SRC_DIR/../../tools/fdk-theme/fdk-theme" \
        "$SRC_DIR/tools/fdk-theme/fdk-theme"; do
        if [ -x "$cand" ]; then BIN="$cand"; break; fi
    done
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "ERROR: fdk-theme binary not found (looked from $SRC_DIR)" >&2
    exit 2
fi

# ── Scratch HOME ──────────────────────────────────────────────────────
SCRATCH="$(mktemp -d -t fdk-theme-test-XXXXXX)"
trap 'rm -rf "$SCRATCH"' EXIT
export HOME="$SCRATCH"
# fdk-theme only reads $HOME, but be defensive about XDG vars that
# could otherwise divert config elsewhere on weird systems.
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CONFIG_DIRS XDG_DATA_DIRS

PASS=0
FAIL=0
fail() { echo "FAIL: $*"; FAIL=$((FAIL+1)); }
pass() { echo "PASS: $*"; PASS=$((PASS+1)); }

# ── 1. Initial 'list' on an empty HOME ────────────────────────────────
# Should report no themes and not crash.
OUT=$("$BIN" list 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "list on empty HOME exited $RC"
else
    pass "list on empty HOME exits 0"
fi
case "$OUT" in
    *"doesn't exist yet"*|*"no .fdktheme files found"*)
        pass "list reports empty state" ;;
    *) fail "list output unexpected: $OUT" ;;
esac

# ── 2. 'show' before anything is set ──────────────────────────────────
OUT=$("$BIN" show 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "show on empty HOME exited $RC"
else
    pass "show on empty HOME exits 0"
fi
case "$OUT" in
    *"not set"*) pass "show reports 'not set' on empty HOME" ;;
    *) fail "show should say 'not set': $OUT" ;;
esac

# ── 3. Drop a fake .fdktheme file and 'list' ──────────────────────────
mkdir -p "$HOME/.FDKthemes"
cp "$SRC_DIR/../themes/void.fdktheme" "$HOME/.FDKthemes/void.fdktheme" 2>/dev/null || \
cp "$SRC_DIR/../../themes/void.fdktheme" "$HOME/.FDKthemes/void.fdktheme" 2>/dev/null || \
cp "$SRC_DIR/themes/void.fdktheme" "$HOME/.FDKthemes/void.fdktheme"
if [ ! -f "$HOME/.FDKthemes/void.fdktheme" ]; then
    echo "ERROR: couldn't locate themes/void.fdktheme to copy" >&2
    exit 2
fi

OUT=$("$BIN" list 2>&1)
case "$OUT" in
    *void.fdktheme*) pass "list shows void.fdktheme after copy" ;;
    *) fail "list doesn't show void: $OUT" ;;
esac

# ── 4. 'set' with a nonexistent theme name ───────────────────────────
OUT=$("$BIN" set does-not-exist 2>&1)
RC=$?
if [ $RC -eq 0 ]; then
    fail "set nonexistent should fail (rc=$RC)"
else
    pass "set nonexistent fails with rc=$RC"
fi
case "$OUT" in
    *"not found"*) pass "set nonexistent prints 'not found'" ;;
    *) fail "set nonexistent output unexpected: $OUT" ;;
esac

# Verify the conf wasn't written anyway
if [ -f "$HOME/.config/FDK/fdk.conf" ]; then
    fail "fdk.conf created despite set failure"
else
    pass "no fdk.conf written on failed set"
fi

# ── 5. 'set' with a valid name ───────────────────────────────────────
OUT=$("$BIN" set void 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "set void failed (rc=$RC): $OUT"
else
    pass "set void exits 0"
fi
case "$OUT" in
    *"Active theme: void.fdktheme"*) pass "set reports the resolved name" ;;
    *) fail "set output unexpected: $OUT" ;;
esac

# Verify the conf was actually written
if [ ! -f "$HOME/.config/FDK/fdk.conf" ]; then
    fail "fdk.conf not created"
else
    CONF_CONTENTS=$(cat "$HOME/.config/FDK/fdk.conf")
    case "$CONF_CONTENTS" in
        *"theme = void.fdktheme"*) pass "fdk.conf contains correct theme line" ;;
        *) fail "fdk.conf wrong contents: $CONF_CONTENTS" ;;
    esac
fi

# ── 6. 'list' now marks void as active ───────────────────────────────
OUT=$("$BIN" list 2>&1)
case "$OUT" in
    *"* void.fdktheme"*) pass "list marks void as active" ;;
    *) fail "list doesn't mark void active: $OUT" ;;
esac

# ── 7. 'show' reports the active theme ───────────────────────────────
OUT=$("$BIN" show 2>&1)
case "$OUT" in
    *"System-wide theme:"*void*) pass "show reports active system theme" ;;
    *) fail "show output unexpected: $OUT" ;;
esac

# ── 8. 'set --app' for a per-app override ────────────────────────────
OUT=$("$BIN" set --app myapp void 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "set --app failed (rc=$RC): $OUT"
else
    pass "set --app exits 0"
fi
if [ ! -f "$HOME/.FDKthemes/overrides/myapp" ]; then
    fail "override file not created"
else
    pass "override file created"
    OVR=$(cat "$HOME/.FDKthemes/overrides/myapp")
    case "$OVR" in
        *void.fdktheme*) pass "override contains theme path" ;;
        *) fail "override contents wrong: $OVR" ;;
    esac
fi

# ── 9. 'show --app' resolves to the per-app override ─────────────────
OUT=$("$BIN" show --app myapp 2>&1)
case "$OUT" in
    *"Tier 2"*"SET"*) pass "show --app reports tier 2 override" ;;
    *) fail "show --app output unexpected: $OUT" ;;
esac

# ── 10. 'show --app' for an app with no override falls through to tier 3
OUT=$("$BIN" show --app otherapp 2>&1)
case "$OUT" in
    *"Tier 2"*"not set"*) pass "show --app reports no tier 2 for unset app" ;;
    *) fail "show --app otherapp output unexpected: $OUT" ;;
esac
case "$OUT" in
    *"Tier 3"*"void"*) pass "show --app falls through to tier 3" ;;
    *) fail "show --app otherapp should fall through to tier 3: $OUT" ;;
esac

# ── 11. 'unset --app' removes the override ───────────────────────────
OUT=$("$BIN" unset --app myapp 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "unset --app failed (rc=$RC)"
else
    pass "unset --app exits 0"
fi
if [ -f "$HOME/.FDKthemes/overrides/myapp" ]; then
    fail "override file still exists after unset"
else
    pass "override file removed"
fi

# ── 12. 'unset --app' on a never-set app is a no-op ──────────────────
OUT=$("$BIN" unset --app never-set 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "unset --app on missing override should exit 0"
else
    pass "unset --app on missing override exits 0"
fi
case "$OUT" in
    *"nothing to remove"*) pass "unset --app reports nothing to remove" ;;
    *) fail "unset --app missing output unexpected: $OUT" ;;
esac

# ── 13. 'set' with a .fdktheme extension works ───────────────────────
OUT=$("$BIN" set void.fdktheme 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "set with extension failed"
else
    pass "set with .fdktheme extension works"
fi

# ── 14. 'set' with an absolute path ──────────────────────────────────
ABS_PATH="$HOME/.FDKthemes/void.fdktheme"
OUT=$("$BIN" set "$ABS_PATH" 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "set with absolute path failed: $OUT"
else
    pass "set with absolute path works"
fi

# ── 15. 'show' after absolute-path set still works ───────────────────
OUT=$("$BIN" show 2>&1)
case "$OUT" in
    *"$ABS_PATH"*) pass "show reports absolute-path theme correctly" ;;
    *) fail "show after abs-path set unexpected: $OUT" ;;
esac

# ── 16. No args / -h / --help ────────────────────────────────────────
OUT=$("$BIN" 2>&1)
RC=$?
if [ $RC -eq 0 ]; then
    fail "no-args should exit nonzero"
else
    pass "no-args exits nonzero"
fi

OUT=$("$BIN" --help 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
    fail "--help should exit 0"
else
    pass "--help exits 0"
fi
case "$OUT" in
    *"Usage:"*) pass "--help prints usage" ;;
    *) fail "--help output unexpected: $OUT" ;;
esac

# ── 17. Unknown command ──────────────────────────────────────────────
OUT=$("$BIN" bogus-command 2>&1)
RC=$?
if [ $RC -eq 0 ]; then
    fail "unknown command should exit nonzero"
else
    pass "unknown command exits nonzero"
fi
case "$OUT" in
    *"unknown command"*bogus-command*) pass "unknown command reports error" ;;
    *) fail "unknown command output unexpected: $OUT" ;;
esac

# ── 18. fdk.conf preserves other keys on rewrite ─────────────────────
# Add a non-theme key, then 'set' again — the other key should survive.
printf 'theme = void.fdktheme\nother_key = should-survive\n' > "$HOME/.config/FDK/fdk.conf"
OUT=$("$BIN" set void 2>&1)
NEW_CONF=$(cat "$HOME/.config/FDK/fdk.conf")
case "$NEW_CONF" in
    *"other_key = should-survive"*) pass "fdk.conf preserves unrelated keys" ;;
    *) fail "fdk.conf clobbered unrelated keys: $NEW_CONF" ;;
esac

# ── Summary ───────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo " fdk-theme CLI test results: $PASS passed, $FAIL failed"
echo "=========================================="
[ $FAIL -eq 0 ] && exit 0 || exit 1
