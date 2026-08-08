# FDK — Faded Dream Kit — Development Roadmap

> **Current version: v0.2.0**
> Version 0.x.x means the toolkit is in active development. APIs may
> change between minor versions. Read the changelog before updating.
>
> **v0.2.0 released** — HarfBuzz, in-tree BiDi (UAX #9 + N0), HiDPI
> auto-scaling (X11 + Wayland), IME (X11 XIM + Wayland text-input-v3),
> 8 new widgets, Wayland feature parity with X11. See CHANGELOG.md.

Platform: Linux (Wayland + X11) | Language: C11 | License: Proprietary (see LICENSE)
Contact: faddeddreamproject@proton.me

---

## Version scheme

| Version | Meaning |
|---|---|
| 0.0.x | Initial commits — not ready for use |
| 0.1.x | Stable enough to build on. Not yet at 1.0 but functional. |
| 1.0.0 | First real stable release — API settled, no excuses |
| 1.x.x | Stable — new features and fixes, no breaking changes within major version |

## Release history

| Version | Commit | Status | Notes |
|---------|--------|--------|-------|
| 0.0.1 | 1st | Not ready | Initial commit. Broken, incomplete, do not use. |
| 0.1.0 | 3rd | Stable | First usable release. CSD, file dialog, DnD, resize, the works. |
| 0.2.0 | v0.2.0 tag | Released | HarfBuzz, in-tree BiDi, HiDPI (X11+Wayland), IME (X11+Wayland), 8 new widgets, Wayland DnD fix. |

---

## ✅ Implemented and confirmed working

### Core / Platform
- [x] **Wayland backend** — `wl_surface`, `xdg_toplevel`, `wl_seat`, input events
- [x] **X11 backend** — Xlib, XImage, MIT-SHM shared memory, XKB keyboard
- [x] **Key repeat** — Wayland: client-side timer using compositor `repeat_info` rate/delay, fires synthesized `FDK_EVENT_KEY_DOWN` events each tick. X11: driven by the X server's native repeat via `XkbSetDetectableAutoRepeat`, no client-side timer needed.
- [x] **Window state tracking and requests** — `FDK_EVENT_STATE_CHANGE` fired on maximize/fullscreen/activated transitions. `fdk_window_set_maximized()`, `fdk_window_set_fullscreen()`, `fdk_window_is_maximized()`, `fdk_window_is_fullscreen()` public API. Wayland: `xdg_toplevel` configure states array. X11: `_NET_WM_STATE` EWMH via `PropertyNotify` + `ClientMessage` to root.
- [x] **Software renderer** — CPU pixel buffer, clip stack, rounded rects, circles, gradients, box-blur shadows
- [x] **OpenGL renderer** — GL 3.3 core profile, batch vertex buffer, FreeType glyph atlas, GLX (X11) + EGL (Wayland)
- [x] **Clipboard** — Wayland `wl_data_device`, X11 selection protocol — `Ctrl+C/V/X` wired into text inputs
- [x] **Tween / animation engine** — `fdk_tween()`, easing functions, per-frame callbacks
- [x] **Font loading** — FreeType, recursive `/usr/share/fonts/` scan by name, absolute path fallback
- [x] **inotify hot-reload** — background pthread watches `.fdktheme` files or `fdk.conf` for live updates. Per-`FDK_UI` watch list (each window's watch is independent — see the now-resolved multi-window item below), signaled to stop via a dedicated `eventfd` rather than closing its inotify fd out from under a blocked read, verified race-free under ThreadSanitizer.

### Widgets (19 types)
- [x] Container (hbox / vbox, FILL / WRAP sizing, gap)
- [x] Label
- [x] Button (with variant support: accent, danger, ghost, custom)
- [x] Text input (cursor, selection, Ctrl shortcuts)
- [x] TextArea (multiline, scrollable, read-only mode)
- [x] Checkbox
- [x] Toggle button (with `[toggle.on]` variant)
- [x] Radio button (group-exclusive selection)
- [x] Slider (continuous, draggable thumb)
- [x] Progress bar (determinate + indeterminate)
- [x] Spinner (numeric up/down input)
- [x] Dropdown (single-select popup)
- [x] Badge (count/status pill)
- [x] Separator
- [x] Scroll view
- [x] Image
- [x] Tabs (horizontal tab bar + pane switcher)
- [x] MenuBar (top-of-window, Alt+key mnemonics, keyboard navigation)
- [x] Context menu (right-click popup, `fdk_widget_set_context_menu()` auto-wires)

### Theme system
- [x] **`.fdktheme` file format** — variables, per-widget sections, named variants, gradients, shadows, fonts, inline comments
- [x] **`fdk_theme_load(theme, path)`** — full parser, unknown keys silently ignored (forward-compatible)
- [x] **`fdk_theme_force()` / `fdk_theme_force_file()`** — tier 1: hard developer override, bypasses everything
- [x] **`~/.FDKthemes/overrides/<app_name>`** — tier 2: user per-app override file, no developer code needed
- [x] **`~/.config/FDK/fdk.conf`** — records active theme name, read by all FDK apps on startup
- [x] **`~/.FDKthemes/theme.fdktheme`** — tier 3: direct file fallback (backward-compat, works without fdk.conf)
- [x] **`fdk_ui_create(win, NULL)`** — auto-resolves all three tiers, auto-starts hot-reload watch
- [x] **`fdk_theme_watch()`** — watches a specific `.fdktheme` file for content changes
- [x] **`fdk_theme_watch_conf()`** — watches `~/.config/FDK/fdk.conf`, re-resolves on `fdk-theme set`
- [x] **`fdk_widget_set_variant(w, "name")`** — maps to `[widget.name]` in theme file
- [x] **`fdk_widget_set_style(w, &style)`** — per-widget programmatic override, highest priority
- [x] **`resolve_style()` wired into all 19 widget paint cases** — bg, fg, border, radius, padding all driven by theme file
- [x] **Three bundled themes** — `faded-dream.fdktheme`, `void.fdktheme`, `rose.fdktheme`

### Tools
- [x] **`fdk-theme` CLI** — `list`, `set <name>`, `set --app <app> <name>`, `unset --app`, `show`, `show --app`
  - Themes live in `~/.FDKthemes/` as individual named files — user drops them in, picks one by name
  - Active theme recorded in `~/.config/FDK/fdk.conf` (plain text, editable by hand)
  - Running FDK apps re-theme within ~1 second via `fdk_theme_watch_conf()`
  - No themes auto-install, auto-copy, or write anywhere without user action

### Build / Packaging
- [x] **CMake build system** — `FDK_BUILD_SHARED`, `FDK_BUILD_STATIC`, `FDK_BUILD_TOOLS`, `FDK_BUILD_EXAMPLES`, `FDK_BUILD_TESTS`
- [x] **CMake install target** — installs library, headers, `fdk-theme` binary, `.pc` file — themes NOT installed (user-managed)
- [x] **pkg-config** — `fdk.pc` generated at build time
- [x] **Packaging recipes** — `packaging/PKGBUILD` (Arch), `packaging/fdk.spec` (RPM), `packaging/debian/` (Debian/Ubuntu)
- [x] **Headless test suite** (`tests/test_theme.c`) — 49+ assertions, runs via `ctest`, no window/compositor needed
- [x] **Optional libxcursor** — the X11 backend's libxcursor dependency
  is now properly optional in CMake (`pkg_check_modules(XCURSOR xcursor)`
  + skip on missing) AND in the source (`#ifdef FDK_HAVE_XCURSOR` around
  `<X11/Xcursor/Xcursor.h>` and `XcursorLibraryLoadCursor`). When
  libxcursor is unavailable, the X11 backend falls back to classic
  X11 font cursors (`XCreateFontCursor` with the same `XC_*` glyph
  indices the previous code used as a per-cursor fallback). Previously
  CMake made the dep optional but `x11.c` included the header
  unconditionally, so the X11 backend would silently break the build
  on systems without `libxcursor-dev`. Verified: full build with
  libxcursor present (cursor themes load via XcursorLibraryLoadCursor),
  and a separate build with `xcursor.pc` hidden (font cursors only) —
  both compile clean under `-Wall -Wextra` and produce working binaries.

---

## ✅ Needed before v1.0.0 — ALL COMPLETE

All items below are resolved as of v0.1.0. None remain open.

- [x] **Key repeat** ✓ done — see ✅ section above.

- [x] **Window state tracking and requests** ✓ done — maximize, fullscreen,
  activated state tracked and exposed via events and query functions.
  See ✅ section above.

- [x] **Client-side decorations (CSD)** ✓ done — visible minimize,
  maximize, and close buttons drawn by FDK itself, so apps look
  complete on desktop environments that expect client-drawn chrome
  (GNOME, XFCE, etc.) and not just compositors/WMs that draw their
  own (Hyprland, Sway, KWin).
  Six separable pieces; **6 of 6 done**, tracked here as they land rather
  than all at once:
  1. [x] **Titlebar rendering** ✓ done — `FDK_WIDGET_TITLEBAR`, a real
     widget type (not a separate subsystem outside the tree, which the
     original note above this list assumed it would need to be — turned
     out the existing switch-based widget dispatch already handles this
     fine, same as `menubar`/`tabs`). `fdk_titlebar(title)` +
     `fdk_titlebar_set_buttons(w, min, max, close)`. Renders a title,
     bottom border, and minimize/maximize/close glyphs (line / stroked
     square / X) drawn with the primitives already in `fdk.h`
     (`fdk_draw_line`, `fdk_stroke_rect`) — no new icon assets needed.
     Button rects and `hover_*` flags are already in the widget's data
     model, computed every paint, even though hit-testing doesn't read
     them yet (item 2) — so that step is additive, not a redesign.
     Verified: clean build under `-Wall -Wextra`, full test suite still
     passes, and (since the usual screenshot-viewing tool wasn't
     rendering images for me in this session) a structured pixel
     analysis in place of a visual check — sampled each button's
     horizontal center-line and confirmed the non-background pixel
     *count* precisely matches each glyph's own geometry (a `half=5`
     horizontal line → exactly 11px; a stroked square's two vertical
     edges at center-height → exactly 2px; an X's diagonals crossing at
     center → 1px), plus confirmed the minimize line is genuinely a
     thin line and not a block (background-colored 8px above/below its
     own row) and that the border/body-background colors are distinct
     from the titlebar's own background and from each other.
  2. [x] **Hit-testing / click handling** ✓ done — `dispatch_event()`
     in `src/widgets/widget.c` now reads the `btn_minimize_rect`,
     `btn_maximize_rect`, and `btn_close_rect` computed during paint
     in the `FDK_WIDGET_TITLEBAR` case of all three mouse-event
     handlers:
       * `MOUSE_MOVE`: updates `hover_minimize/maximize/close` flags
         based on cursor position over the button rects; repaints on
         any change so the highlight follows the pointer. Pointer
         cursor over buttons, default over the title text area.
       * `MOUSE_DOWN`: arms `pressed_minimize/maximize/close` if the
         click landed on a button; otherwise calls the new
         `fdk_window_begin_move()` to hand the move off to the WM
         (item 3 below). The arming pattern matches every other
         button widget: the action only fires on `MOUSE_UP` if the
         release is on the same button, so a drag-off-then-back
         cancels the click.
       * `MOUSE_UP`: walks all `FDK_WIDGET_TITLEBAR` widgets
         (multi-window safe), checks if the release landed on a
         button that was armed by the corresponding `MOUSE_DOWN`, and
         fires the window-state action: close → sets `ui->pending_close`
         which `fdk_ui_step()` converts to a `FDK_EVENT_CLOSE` on the
         next call (the same path a `WM_DELETE_WINDOW` takes); maximize
         → toggles `fdk_window_set_maximized()`; minimize → calls the
         new `fdk_window_minimize()`.
     Three new `pressed_*` fields on the titlebar widget struct track
     the armed state; the paint code uses them to render a darker
     `bg_widget_active` background on the pressed button so the user
     sees the press happen. The `FDK_WIDGET_TITLEBAR` was added to the
     interactive-types list in `hit_test()` so the titlebar can
     actually receive events.
  3. [x] **Drag-to-move** ✓ done (single-click-drag; double-click-to-
     maximize remains unimplemented, see open note below) — added a new
     `fdk_window_begin_move(FDK_Window*, const FDK_Event*)` public API
     and a corresponding `window_begin_move` slot in the platform
     vtable. Backends:
       * X11: sends an EWMH `_NET_WM_MOVERESIZE` ClientMessage to the
         root window with direction `_NET_WM_MOVERESIZE_MOVE` (8),
         button 1, source 2 (user action). The WM handles the actual
         pointer grab and move logic — snap-to-edge, keyboard move,
         etc. all work because the WM owns the interaction.
         `XTranslateCoordinates` converts the event's window-relative
         `mouse.x/y` to the root-relative coords the spec requires.
       * Wayland: calls `xdg_toplevel_move(toplevel, seat, serial)`
         using `s_last_serial` captured in `ptr_button()` — the serial
         must be from the originating input event per protocol, so
         callers must invoke `fdk_window_begin_move()` synchronously
         from their `MOUSE_DOWN` handler.
     `dispatch_event()`'s titlebar `MOUSE_DOWN` case calls
     `fdk_window_begin_move(ui->win, ev)` for any click in the empty
     titlebar area (i.e. not on a button). Double-click-to-maximize
     is now also wired up — see the titlebar struct's `last_click_ms`
     field; a second click within 400ms of the first toggles maximized
     state instead of starting a move (matches GNOME/KDE/Aqua).
  4. [x] **`zxdg_decoration_manager_v1` negotiation on Wayland** ✓ done —
     `wayland-scanner`-generated bindings (same pattern as the existing
     `xdg-shell`/`xdg-toplevel-icon` protocols). CMake finds the
     protocol XML at `${WP_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml`,
     generates client-header + private-code, and defines
     `FDK_HAVE_XDG_DECORATION` when present. At runtime, `wl_init()`
     binds the `zxdg_decoration_manager_v1` global when the compositor
     advertises it. `wl_window_set_decorated()` calls
     `zxdg_decoration_manager_v1_get_toplevel_decoration()` to get a
     per-toplevel decoration object, then
     `zxdg_toplevel_decoration_v1_set_mode()` with either
     `SERVER_SIDE` (decorated=true) or `CLIENT_SIDE` (decorated=false).
     Per-toplevel decoration objects are destroyed in `wl_window_destroy()`
     BEFORE the toplevel itself (otherwise the compositor may log a
     protocol error). The protocol uses "request" terminology because
     the compositor has the final say — GNOME/Mutter forces SSD on
     Wayland regardless, but labwc/Hyprland/KWin/Sway honor the
     client's preference. Without this protocol, the compositor falls
     back to its own default (labwc/Hyprland = SSD, Sway = CSD),
     which is why FDK's titlebar was previously hidden underneath
     labwc's.
  5. [x] **Motif WM hints on X11** ✓ done — `x11_window_set_decorated()`
     sets `_MOTIF_WM_HINTS` on the window with `flags = MWM_HINTS_DECORATIONS`
     and `decorations = 0` (no decorations) when called with
     `decorated=false`, or `MWM_DECOR_ALL` when called with `true`. The
     hint struct layout matches the original mwm/2b definition: 5 longs
     (flags, functions, decorations, input_mode, status). Despite the
     "Motif" name, every modern EWMH-compliant WM (Openbox, i3,
     awesome, KWin, Mutter/X11, XFWM, etc.) still honors these hints.
     `functions` is left at `MWM_FUNC_ALL` so the WM still lets the
     user close/minimize/move from its own menus even when decorations
     are suppressed — only the visual chrome is removed, not the
     functionality.
  6. [x] **Public API opt-in** ✓ done — added `bool csd` field to
     `FDK_WindowDesc`. When true, `fdk_window_create()` calls the new
     `window_set_decorated(pw, false)` vtable slot after the window is
     registered (so the backend has its `pw` set up). On X11 this
     suppresses server-side decorations via Motif hints (item 5); on
     Wayland this is currently a no-op pending item 4. The app is
     expected to also place an `fdk_titlebar()` widget at the top of
     its root container to draw the client-side chrome (items 1-3);
     the titlebar widget works whether or not `csd=true` is set, but
     without it the user may see both the WM's titlebar and FDK's.
     `examples/csd-demo/` shows the full pattern.

- [x] **App window icon** ✓ done — both backends now support setting the
  taskbar icon via `fdk_window_set_icon_name(win, "icon-name")`.
  - Wayland: uses `xdg_toplevel_icon_v1` protocol (already implemented)
  - X11: now fully implemented using `stb_image.h` (public domain,
    vendored in `third_party/stb/`) to decode PNG files from the XDG
    icon theme, then sets `_NET_WM_ICON` with the ARGB pixel data per
    EWMH spec. CMake auto-detects the vendored `stb_image.h` and
    enables `FDK_WITH_STB_IMAGE` automatically — no manual config needed.
  The `stb_image.h` header also enables `fdk_image_from_file()` to load
  PNG/JPEG/BMP files (previously required manual `FDK_WITH_STB_IMAGE`
  + `FDK_STB_DIR` configuration).

- [x] **Per-UI theme watch contexts** ✓ done — `theme.c`'s watch state was a
  single global `g_watch`; starting a second window's watch silently tore
  down the first window's watch, so only the most-recently-created window
  ever kept a live watch. Replaced with a linked list of watch contexts
  keyed by `FDK_UI*`, guarded by a mutex, so each window's watch is fully
  independent. `fdk_ui_destroy()` now also stops its own watch before
  freeing the `ui`, closing a use-after-free a watch thread would otherwise
  hit on the next file-change event for a destroyed window.
  New regression test: `tests/test_watch_multiwindow.c` (`ctest -R
  watch_multiwindow`) — two windows hold simultaneous independent watches,
  file edits reach only the correct window, stopping one doesn't affect the
  other, and `fdk_ui_destroy()` cleans up a watch the caller forgot to stop.
  Verified clean under ThreadSanitizer (25 runs, 0 races) and
  AddressSanitizer + UBSan + LeakSanitizer (0 leaks, 0 UB) after two rounds
  of real races ThreadSanitizer caught during development — see
  `src/core/theme.c`'s `WatchCtx.stop_efd` comment for why the shutdown
  signal is a dedicated `eventfd` rather than closing the inotify fd out
  from under a blocked reader, and why even that fd's `close()` has to
  happen on a specific thread, in a specific order, to be race-free. Also
  added a `theme_lock` mutex to `FDK_UI` (guards `ui->theme`/`ui->dirty` in
  `fdk_ui_set_theme()`), since a live watch thread and the app's own thread
  can now legitimately call it concurrently on the same `ui`.
  **This was the only part of "multiple windows" that had defined,
  testable behavior to fix without a display server** — see the open item
  right below for what's still unverified.

- [x] **Multiple windows / dialogs (windowing layer)** ✓ empirically verified
  on X11, confirmed working on Wayland (Hyprland) by the developer — see
  the breakdown below. The
  watch-context prerequisite above was the first piece; actually running
  two `FDK_Window`s at once turned up four more real bugs, all specific to
  multi-window usage and invisible in every existing (single-window)
  example. Fixed all four, then built `examples/two-window-demo/` and
  proved the fix genuinely works end-to-end — not just "should work,"
  actually ran it.

  1. **`fdk_ui_step()`'s internal event-drain loop misrouted events between
     windows.** It drains the shared global event queue via
     `fdk_poll_event()` and dispatches every event it finds into whatever
     `FDK_UI*` it was called with — checking `pending.type` for QUIT/CLOSE
     (which it already re-injects back to the caller) but never checking
     `pending.window`. In a multi-window app calling `fdk_ui_step()` once
     per window per frame, an event meant for window B could get greedily
     drained and dispatched into window A's widget tree instead — most
     easily triggered by mouse-motion bursts crossing from one window into
     another, exactly the case this drain loop exists to coalesce. Fixed
     by widening the existing re-injection condition to also cover
     `pending.window != ui->win`, reusing a mechanism the function already
     had rather than adding new machinery. Zero behavior change for every
     existing single-window caller: with one `FDK_Window` in the process,
     `pending.window` is always `== ui->win`.
  2. **Per-window animation clock was a shared `static`.** `fdk_ui_step()`
     computed `dt` from a function-local `static uint64_t last_tick`,
     shared across every call regardless of which `ui` was passed. Two
     windows calling `fdk_ui_step()` each real frame would compute `dt`
     from whichever window's step ran most recently, not their own
     previous step — silently running `tick_animations()` at roughly Nx
     speed with N windows open. Fixed: moved to a new
     `FDK_UI.anim_last_tick_ms` field, one clock per window.
  3. ~~`fdk__tweens_tick()` drives a single process-global tween pool,
     broken by multi-window the same way #2 was~~ — **this was wrong,
     corrected after actually tracing it rather than assuming.** Flagged
     initially by analogy to #2 (same doc-comment phrasing: "called once
     per frame by `fdk_ui_step()`"), without separately tracing this
     function's own mechanics. It's fine: `fdk__tweens_tick()` computes
     `delta = now - last_ms` and applies it to the *same shared pool*
     every single call, regardless of which window's step triggered it —
     unlike #2, where each call's delta went to a *different*,
     window-specific `root`. That distinction matters: consecutive
     deltas telescope (`(t1-t0)+(t2-t1)+(t3-t2)+... = tN-t0`) regardless
     of how many calls happen or how unevenly spaced they are, as long as
     every call updates the *same* clock and applies to the *same*
     recipient — which is exactly what happens here, so the pool's total
     elapsed time tracks real wall-clock time correctly no matter how
     many windows are calling `fdk_ui_step()`. Verified empirically, not
     just re-derived by hand: a throwaway test ran the same 500ms tween
     to completion under single-call-per-~16ms and double-call-per-~16ms
     (simulating two windows stepping back-to-back) patterns five times
     each — real completion time differed by at most ~3%, consistent
     with ordinary scheduling jitter, never anywhere near the ~100% a
     genuine per-call speed-doubling would produce. No fix needed;
     `fdk_tween()` is safe to use in a multi-window app.
  4. **A theme change didn't reliably repaint on screen** — the most
     significant find. `fdk_ui_set_theme()` sets `ui->dirty = true`, but
     that flag is only acted on inside `fdk_ui_step()`'s tail check, which
     only runs when the app's event loop calls `fdk_ui_step()` at all. A
     `fdk_theme_watch()` background thread calling `fdk_ui_set_theme()`
     while the app is otherwise idle (no mouse movement, nothing else
     happening) left the window showing its old theme indefinitely —
     confirmed empirically, not theorized: running the demo against a real
     X server, editing a theme file did nothing visible until a subsequent
     unrelated mouse-move event happened to arrive and incidentally
     triggered the pending repaint. This isn't multi-window-specific — it
     would affect any single-window app using `fdk_theme_watch()` too
     (`systemwide-test`/`theme-switcher` included), just less noticeably,
     since a person's mouse is rarely completely motionless for long. Fixed
     by having `fdk_ui_set_theme()` call
     `fdk_window_request_redraw(ui->win)` after updating the theme. That
     alone was sufficient on X11 (`XSendEvent` synthesizes a real `Expose`
     the app's loop will see) but **not** on Wayland: `wl_surface_damage`
     + `wl_surface_commit` only tell the *compositor* to recomposite,
     generating nothing an `FDK_Event`-based loop would ever observe.
     `wl_window_request_redraw()` now also pushes a synthetic
     `FDK_EVENT_EXPOSE` into the local queue, mirroring the exact pattern
     `xdg_surface_configure` already uses once at startup in the same
     file.

  **Verification.** Built `examples/two-window-demo/` — two real windows,
  each watching its own `.fdktheme` file, each showing widgets so a theme
  change is visible, with an independent "close this window" per window.
  Ran it against a real (if virtual) X11 server: `Xvfb :99`, forced
  `FDK_PLATFORM=x11`, drove it with `xdotool`, and captured pixel colors
  with ImageMagick's `import`/`convert` before and after editing each
  theme file. Every value was an exact hex match with **zero** manual
  nudging required after the redraw fix:
  - Editing `left.fdktheme` → LEFT's background became exactly the new
    color; RIGHT's stayed byte-for-byte unchanged.
  - Editing `right.fdktheme` → same in reverse: RIGHT updated, LEFT
    (already showing its own prior edit) was untouched.
  - Closing LEFT via a real `WM_DELETE_WINDOW` (`xdotool windowclose`,
    the same mechanism a window manager's close button uses) — the
    process didn't crash, only RIGHT remained, and RIGHT's watch **still
    worked** on a subsequent edit after LEFT's `fdk_ui_destroy()` ran.
    This is the use-after-free fix from the watch-context item above,
    exercised for real rather than headlessly.
  **Verified on Wayland (Hyprland)**: confirmed by the developer — both
  windows open correctly on a real Hyprland session, built from a plain
  `cmake --build`. This was the one piece the environment this fix was
  developed in couldn't check (no compositor available, X11-only via
  Xvfb). Window-manager-level concerns the demo doesn't exercise (focus
  handling and z-order across two windows) remain untested either way, as
  does whether the theme-file-edit-without-mouse-movement repaint
  specifically fires promptly on Wayland the same way it was confirmed to
  on X11 — worth a quick look if not already covered by the above.

- [x] **Font resource leak on theme reload** ✓ fixed — the original
  diagnosis above was imprecise in an important way: `fdk_font_load()`
  itself isn't the problem (it's a plain allocator, behaves exactly as
  expected). The real leak is in `fdk_ui_set_theme()`
  (`src/widgets/widget.c`) — every hot-reload goes through it, and it
  genuinely orphans the *previous* font whenever the newly-loaded theme
  specifies its own (non-NULL) font. Confirmed precisely with a focused
  LeakSanitizer reproduction before touching anything: two consecutive
  `fdk_ui_set_theme()` calls, each with a real font, leaked exactly one
  font every time.
  The naive fix (`fdk_font_destroy()` the orphaned font immediately)
  isn't safe: paint code elsewhere in `widget.c` reads
  `ui->theme.font_body`/`font_label`/`font_mono` without taking
  `theme_lock` (a separate, already-documented gap), so freeing
  immediately risks a paint pass on the main thread still using that
  exact pointer at that exact moment — trading a certain leak for an
  occasional crash. Fixed with a one-generation deferred free instead:
  three new `FDK_UI` fields (`retired_font_body/label/mono`) hold
  whatever the *previous* `fdk_ui_set_theme()` call orphaned; each new
  call frees that previous generation before doing its own work, then
  retires (doesn't yet free) whatever *it's* replacing. `fdk_ui_destroy()`
  frees any still-retired fonts at shutdown, since there's no further
  call left to hand that off to. This narrows the theoretical race to
  "a paint pass stalled for an entire extra reload cycle" rather than
  eliminating it outright — a fully robust fix needs every paint-time
  read of `ui->theme` to also take the lock, a larger, separate change
  not made here.
  Also fixed the *original* `tests/test_theme.c` leak this item was
  first reported against — genuinely separate from the above: a `for`
  loop reused a `FDK_Theme` struct across bundled theme files without
  freeing the font each iteration loaded before the next iteration's
  fresh struct shadowed it. Test-code hygiene, not a library defect.
  Verified: a dedicated throwaway LeakSanitizer test (two- and
  three-generation reload sequences, both clean), the existing
  `test_watch_multiwindow` suite re-run under ThreadSanitizer (20 runs,
  0 races — this test already has real watch threads calling
  `fdk_ui_set_theme()` concurrently with the main thread, exactly the
  scenario that would catch a problem with the deferred-free logic),
  and independently re-confirmed on the developer's own machine via
  `-fsanitize=address` on the full `test_theme` suite — clean, no
  `LeakSanitizer` block, "ALL CHECKS PASSED."

- [x] **File dialogs** ✓ done — `fdk_file_dialog()` in
  `src/widgets/widget.c` is a built-in modal file picker overlay.
  No D-Bus, no XDG portal, no external dependencies — the dialog is
  rendered entirely by FDK as an overlay on the parent window, using
  the same widget primitives (fill_rect, draw_text, scroll, theme
  colors) as every other FDK widget. This matches the project's
  "no desktop-ecosystem deps" philosophy.
  Features:
  - Directory navigation (double-click a dir to enter, ".." to go up)
  - File filtering by glob pattern (e.g. "*.txt;*.md")
  - Keyboard: Up/Down to navigate, Enter to open/enter, Esc to cancel
  - Mouse scroll wheel for long directories
  - Dotfiles hidden by default
  - Directories sorted first, then files, alphabetically (case-insensitive)
  - Modal — blocks the calling thread until the user picks or cancels
  API: `char *fdk_file_dialog(FDK_Window*, const FDK_FileDialogDesc*)`
  returns a malloc'd path (caller frees) or NULL if cancelled.
  Example: `examples/file-dialog-demo/` shows the full pattern.

- [x] **Drag and drop (X11)** ✓ done — implemented the XDND protocol
  (XdndEnter/Position/Drop/Leave/Status/Finished) in `src/platform/x11.c`.
  On window creation, `XdndAware` is set to advertise drop support.
  When a drag enters, the window responds with `XdndStatus` to accept.
  On drop, `XConvertSelection(XdndSelection, text/uri-list)` requests
  the data, which arrives via `SelectionNotify` and is passed to the
  app's `FDK_DropCb` callback as a null-terminated URI list string.
  New API: `fdk_window_set_drop_handler(win, cb, ud)`.
  Example: `examples/dnd-demo/` — drag files from a file manager into
  the window.
  Wayland `wl_data_device` support is also fully implemented —
  `wl_data_offer_receive()` with `pipe()` + `poll()` + `read()` for
  fd-based data transfer, `wl_data_offer_finish()` on success.

- [x] **Font discovery improvements** ✓ done — `load_font_spec()` in
  `src/core/theme.c` now scans `$XDG_DATA_HOME/fonts` in addition to
  `~/.local/share/fonts/`. Per the XDG Base Directory Spec,
  `$XDG_DATA_HOME` defaults to `~/.local/share` when unset, so the
  existing default-case behavior is preserved; the new code only
  diverges when `$XDG_DATA_HOME` is explicitly set (e.g. to a
  version-controlled dotfiles tree), in which case that path's
  `fonts/` subdir is scanned instead of the hardcoded default. The
  check requires `XDG_DATA_HOME` to start with `/` (the spec says
  relative paths should be ignored) — same defensive parsing every
  other XDG-aware tool uses. No fontconfig dependency added; the
  custom recursive scanner is preserved exactly as before, just with
  one more candidate path on the search list.

- [x] **`ctest` wasn't finding any tests** ✓ fixed — `enable_testing()` was
  only called inside `tests/CMakeLists.txt`, not at the top level, so CMake
  never wrote a `subdirs()` entry for `tests/` into the top-level
  `build/CTestTestfile.cmake`. Running `ctest --output-on-failure` from
  `build/` — exactly what the existing docs/build instructions say to do —
  reported "No tests were found!!!" even though `test_theme` built and
  passed fine when run directly. Moved `enable_testing()` to the top-level
  `CMakeLists.txt`, before `add_subdirectory(tests)`.

- [x] **Test coverage for `fdk-theme` CLI** ✓ done —
  `tests/test_fdk_theme_cli.sh` is a 32-assertion bash test that
  exercises every `fdk-theme` subcommand against a `mktemp`-created
  scratch `$HOME`: list/show on empty state, set with valid name,
  set with `.fdktheme` extension, set with absolute path, set with
  nonexistent name (must fail without writing `fdk.conf`), set --app
  per-app override, show --app resolving through tiers 2 and 3,
  unset --app (both existing and never-set), no-args / --help /
  unknown-command exit codes, and `fdk.conf` preservation of
  unrelated keys across rewrites. Registered in `tests/CMakeLists.txt`
  as ctest target `fdk_theme_cli`, passing `FDK_THEME_BIN` via
  `set_tests_properties(... ENVIRONMENT ...)` so the script finds the
  freshly-built binary regardless of CWD. Runs in ~0.25s, no display
  server or compositor required.

---

## ⬜ Planned (post v1.0.0) — competitive parity with GTK / Qt

Scope: **Linux only**. No Windows, macOS, iOS, Android, or web targets.
License: **unchanged** (proprietary, see LICENSE).

This section catalogs what FDK needs in order to be production-viable
against GTK4 and Qt6 on Linux. Items are grouped by capability area and
tagged with a target version:

- **`v0.2`** — text + input layer (HarfBuzz, IME, BiDi, HiDPI). Required
  for non-Latin scripts and modern displays.
- **`v0.3`** — widget completeness + layout negotiation. Required for
  real apps to be buildable without pain.
- **`v0.4`** — accessibility, i18n, model/view, data binding. Required
  for enterprise / government / education adoption.
- **`v0.5`** — rendering depth (Vulkan, scene graph, dirty rects).
- **`v0.6`** — tooling (designer, declarative UI, inspector).
- **`v1.0`** — API/ABI freeze, performance parity, documentation.

### Text & Typography — `v0.2`

- [x] **HarfBuzz integration** ✓ done — text shaping for Arabic, Hebrew,
  Devanagari, Bengali, Thai, Tibetan, and other complex scripts. Ligatures
  (fi, ffi) form. `harfbuzz` (MIT) added as an optional runtime dependency
  on the text path. `fdk_draw_text()` and `fdk_measure_text()` route
  through `hb_shape()` before FreeType rasterization. Falls back to
  codepoint-by-codepoint shaping when HarfBuzz is unavailable. See
  `src/render/shape.c` and `src/render/shape.h`.
- [x] **Bidirectional (BiDi) text layout** ✓ done — Unicode Bidirectional
  Algorithm (UAX #9) implemented from scratch in `src/render/bidi.c`.
  **No FriBidi dependency** (FriBidi is LGPL-2.1+, FDK is proprietary —
  we cannot link LGPL code, so we wrote our own). Covers paragraph level
  detection (P1-P3), explicit embedding (X1-X10, RLE/LRE/RLO/LRO/PDF),
  weak type resolution (W1-W7), neutral resolution (N1-N2), implicit
  levels (I1-I2), and **N0 bracket pair resolution** (using
  BidiBrackets.txt data — 64 bracket pairs). Isolate sequences
  (FSI/LRI/RLI/PDI) are deferred. Used by `Label`, `TextInput`,
  `TextArea`. Character type lookup covers all Unicode 16.0 RTL/AL
  ranges via @missing directives from DerivedBidiClass.txt.
- [ ] **Vertical text layout** — CJK vertical writing mode
  (`writing-mode: vertical-rl`). Used by Japanese and traditional
  Chinese typography. Toggle via a new `fdk_label_set_vertical()` API.
- [ ] **Rich text / attributed strings** — `FDK_AttributedText` with
  per-range font, size, color, weight, style, underline, strikethrough.
  Pango-markup-style simple parser (`<b>`, `<i>`, `<span color=...>`).
  Needed for any chat / document / help viewer app.
- [ ] **Color font support** — `COLR/CPAL`, `sbix`, and SVG-OT for
  emoji. Without this, 🎉 renders as a monochrome tofu. Plan: FreeType
  `FT_COLOR` glyph loading + per-glyph palette lookup.
- [ ] **Subpixel glyph positioning** — LCD-optimized subpixel rendering
  (FreeType + `FT_LCD_FILTER`). Required for crisp text on 1× and 1.5×
  DPI screens. Today FDK uses integer pixel positions, causing visible
  jitter during caret motion and animation.
- [ ] **Font fallback chain** — when the active font lacks a glyph
  (e.g., Latin font missing CJK characters), transparently fall back
  to a system font that has it. Plan: read `~/.config/fontconfig/fonts.conf`
  or scan `/usr/share/fonts/` for likely fallbacks (Noto Sans CJK,
  Noto Color Emoji, Noto Sans Arabic). Cache the fallback decision
  per codepoint.
- [ ] **Variable font axes** — expose `wght`, `wdth`, `slnt`, `ital`
  axes via `fdk_font_set_axis(font, axis_tag, value)`. Required for
  modern typographic workflows without shipping 8 separate font files.
- [ ] **OpenType feature flags** — `kern`, `liga`, `dlig`, `ss01`,
  `tnum`, etc. Expose via `fdk_font_set_features(font, "+kern,-liga")`.
  Needed for code editors (disable ligatures) and tabular data
  (tabular figures).
- [ ] **RTL widget mirroring** — when locale is RTL, mirror `hbox`
  direction, align labels right, flip icon positions, mirror slider
  fill direction. Detect via `LC_MESSAGES` / `LC_ALL` or explicit
  `fdk_ui_set_text_direction()`.
- [ ] **Text caret with IME preedit support** — caret must accept a
  preedit string from IME (see Input section below) and render it
  inline with an underline. This is the visible half of IME; the
  protocol half is in the platform backends.

### Input — `v0.2`

- [x] **IME support (Wayland)** ✓ done — `zwp_text_input_v3` protocol.
  Binds `zwp_text_input_manager_v3`, creates text_input from seat.
  `ti_enter` enables on focus, `ti_commit_string` pushes
  `FDK_EVENT_IME_COMMIT`. Works with fcitx5/ibus on Wayland.
  (X11 IME via XIM is ✓ done — see above.)
- [x] **IME support (X11)** ✓ done — XIM (X Input Method) via `Xlib`'s
  `XOpenIM()` / `XCreateIC()` / `XFilterEvent()`. Opens XIM at init
  with `XIMPreeditNothing | XIMStatusNothing` style (IME draws its
  own preedit window). All X events now filtered through `XFilterEvent`
  first. KeyPress uses `Xutf8LookupString` before `XLookupString`; if
  the IME commits multi-byte text, it's delivered as a new
  `FDK_EVENT_IME_COMMIT` event (distinct from `FDK_EVENT_KEY_DOWN`).
  Works with IBus/Fcitx/SCIM/uim. See `src/platform/x11.c`.
- [ ] **Dead-key / compose sequences** — `xkbcommon`'s
  `xkb_compose_table_new_from_locale()` + `xkb_compose_state_feed()`.
  Required for European layouts (é, ü, ñ, å) and any layout where a
  single key produces a combining character. Today FDK silently drops
  these. Plan: feed every keypress through the compose state machine
  before emitting `FDK_EVENT_KEY_DOWN`.
- [ ] **Touchscreen support** — `wl_touch` (Wayland) and `XI2`
  (X11). New event types `FDK_EVENT_TOUCH_DOWN / UP / MOTION`, with
  multi-touch tracking IDs. Widget hit-testing extends to touch.
- [ ] **Touch gestures framework** — `FDK_Gesture` with `pinch`,
  `swipe`, `long-press`, `drag`, `tap` recognizers. Mirrors
  `GtkGesture*`. Plan: a gesture attaches to a widget, consumes
  pointer/touch events, fires high-level callbacks. Multiple gestures
  can coexist on the same widget (priority-based conflict resolution).
- [ ] **Tablet / stylus input** — `zwp_tablet_v2` (Wayland) and
  `XI2` (X11). Pressure, tilt, rotation, eraser, tool ID. Required
  for any drawing application. Plan: new `FDK_EVENT_TABLET_*` events
  with `pressure`, `tilt_x`, `tilt_y`, `rotation` fields.
- [ ] **Keyboard focus traversal framework** — `Tab` / `Shift+Tab`
  walks the widget tree in declaration order, skipping disabled /
  invisible widgets. Focus rings drawn around the focused widget.
  `fdk_widget_set_can_focus(w, true)` and
  `fdk_widget_set_focus_order(w, idx)`. Required for accessibility
  and for any power-user workflow.

### Accessibility (a11y) — `v0.4`

- [ ] **Widget tree introspection** — every `FDK_Widget` exposes:
  `role` (button, label, entry, list, etc.), `name` (text label),
  `description` (longer help text), `state` (focused, disabled,
  checked, expanded, etc.), `bounds` (screen rect), `parent`,
  `children`, `action` (default action verb). Foundation for all
  other a11y work.
- [ ] **AT-SPI bridge** — expose the widget tree to AT-SPI2 without
  D-Bus. Plan: open the AT-SPI bus socket directly
  (`/run/user/$UID/at-spi/bus`) and speak the AT-SPI D-Bus protocol
  by hand — no `libdbus` dependency, no `GIO`. Orca reads via AT-SPI;
  once the bridge is up, Orca works without any FDK-specific code
  in Orca.
- [ ] **High-contrast theme variant** — `.fdktheme` files can declare
  a `[high-contrast]` section; FDK auto-switches when
  `org.gnome.desktop.a11y.high-contrast` is set, or when the
  compositor advertises `prefers-contrast: more` (Wayland
  `xdg-system-bell`-style preference protocol).
- [ ] **Reduced-motion preference** — animations scale down to 0ms
  when `org.gnome.desktop.a11y.reduce-motion` is set, or when the
  compositor advertises `prefers-reduced-motion`. Tween engine and
  all widget transitions respect it.
- [ ] **Screen reader announcements** — `fdk_widget_announce(w, msg,
  polite|assertive)` triggers an AT-SPI "live region" event so Orca
  reads the message. Used for toast notifications, validation
  errors, async status updates.
- [ ] **Caret-moved events** — when the caret moves in a
  `TextInput`/`TextArea`, fire an AT-SPI `text-caret-moved` signal
  so Orca reads the new line. Required for blind users to use any
  text-editing app.
- [ ] **Keyboard-only navigation** — full keyboard traversal of
  every widget, every action. No "mouse-only" interactions. Built
  on the focus-traversal framework above plus per-widget key
  bindings.

### Internationalization (i18n) — `v0.4`

- [ ] **Message catalog** — gettext-style API: `fdk_text(domain,
  msgid)`, `fdk_ngettext(domain, msgid, msgid_plural, n)`. Binds to
  `LC_MESSAGES`. Loads `.mo` files from `$XDG_DATA_HOME/locale/<lang>/
  LC_MESSAGES/<app>.mo`. No gettext runtime dependency — we ship a
  tiny `.mo` parser (~200 LOC).
- [ ] **Locale detection** — `setlocale(LC_ALL, "")` at startup,
  expose `fdk_get_locale()` returning the effective language tag
  (e.g., `zh-CN`, `pt-BR`). Used by widgets that format dates /
  numbers.
- [ ] **Plural forms** — `fdk_ngettext()` per the GNU gettext plural
  formula spec. Each language has its own plural rule (Arabic has
  6 forms, Russian has 3, English has 2).
- [ ] **Date/time/number formatting** — locale-aware
  `fdk_format_date()`, `fdk_format_number()`, `fdk_format_currency()`.
  Implementation: a small built-in CLDR data file for the top 30
  locales; falls back to ISO formats for the long tail.
- [ ] **Unicode collation** — `fdk_strcoll()` using DUCET (default
  Unicode collation element table). Required for sorted list views
  in any locale with non-ASCII characters. ICU is too heavy to add
  as a dep; ship the DUCET table and a tailoring-free collator.
- [ ] **RTL layout mirroring** — when locale is RTL, mirror `hbox`
  direction, flip horizontal padding, reverse slider fill, etc.
  Build on the `fdk_ui_set_text_direction()` API in the Text section.

### Layout & Size Negotiation — `v0.3`

- [ ] **Height-for-width / width-for-height negotiation** — today
  FDK uses a single-pass "ask each child its size, then assign"
  model. Real toolkits (GTK4, Qt6) use a two-pass negotiation: child
  reports `(min, nat)` for both orientations, parent reconciles.
  Required for labels that wrap (their height depends on their
  width) and for any layout that wants to look correct on resize.
- [x] **Grid layout** ✓ done — `fdk_grid(cols, homogeneous)` with
  `fdk_grid_add(grid, child, row, col, row_span, col_span)` and
  `fdk_grid_add_next()` for auto-positioning.
- [ ] **Flow layout** — `fdk_flow(gap)` — children wrap to the next
  line when they exceed the container width. Like a CSS inline-flow.
  Used for tag clouds, chip selectors, image galleries.
- [ ] **Stack + StackSwitcher** — `fdk_stack()` shows one child at a
  time; `fdk_stack_switcher()` is a row of buttons / tabs that
  switches between them. Already partially have tabs, but a stack
  is more flexible (children can be added at runtime).
- [ ] **Revealer** — `fdk_revealer(child, transition_type)` —
  animated show/hide of a child. Used for collapsible sections,
  notification bars, side panels. Transition types: slide-left,
  slide-right, slide-up, slide-down, crossfade, none.
- [ ] **Baseline alignment** — `fdk_hbox_baseline()` aligns
  children to a common text baseline, so a label, a text input,
  and a button on the same row look correct. Without baseline
  alignment, rows of mixed widgets always look slightly off.
- [ ] **Constraint-based layout** — `fdk_constraint_layout()` — a
  Cassowary-style constraint solver. Lets apps express layout as
  equations ("button.left = label.right + 8"). Qt6 has this via
  `QQuickAnchorLayout`. Optional but powerful.
- [ ] **Natural size redistribution** — when the parent has more
  space than the children's natural sizes, distribute the slack
  according to `expand` flags (already partially exist via
  `FDK_SIZE_FILL`). Make this consistent across all containers.

### Widget Catalog — `v0.3` and ongoing

The audit found ~30 of ~50 standard GTK4 widgets missing. Each
below is a concrete, named gap. Some are trivial (~50 LOC), some
are not (TreeView with virtualization).

- [x] **Popover** ✓ done — `fdk_popover(child)` — transient anchored
  popup. Basic show/hide API. Full outside-click dismiss is a
  follow-up.
- [ ] **MenuButton** — `fdk_menu_button(label, popover)` — a
  button that toggles a popover. The single most-used GTK4 widget
  that FDK doesn't have. Build on Popover above.
- [x] **SearchEntry** ✓ done — `fdk_search_entry(placeholder)` — text
  input with search icon, clear button, 300ms debounced `on_search`
  callback. Full keyboard support.
- [x] **Expander** ✓ done — `fdk_expander(label, child)` — collapsible
  section with ▶/▼ disclosure triangle. Click header to toggle.
- [x] **StatusBar** ✓ done — `fdk_status_bar()` — bottom bar with
  push/pop message stack (16 deep).
- [x] **LevelBar** ✓ done — `fdk_level_bar(value, max, segments)` —
  segmented discrete progress indicator.
- [x] **Calendar** ✓ done — `fdk_calendar()` — month-view date picker
  with nav arrows, click-to-select, leap year handling, `FDK_Date`
  struct, `on_change` callback.
- [ ] **ColorButton + ColorChooser** — `fdk_color_button(rgba)`
  opens a color picker popover. Color picker has HSV square, hue
  slider, hex entry, eyedropper (X11 only — Wayland screencopy).
- [ ] **FontButton + FontChooser** — `fdk_font_button(family, size)`
  opens a font picker dialog with preview, family list, size
  spinbutton, style toggles (B/I/U).
- [ ] **Scale with marks** — extend `Slider` with
  `fdk_slider_add_mark(value, label)` for labeled slider ticks
  (volume slider with 0/25/50/75/100 marks).
- [ ] **Standalone Scrollbar** — `fdk_scrollbar(orientation)`
  — a scrollbar widget usable outside a `ScrollView` (e.g., for
  custom widgets that manage their own scrolling).
- [ ] **Spinner (busy indicator)** — `fdk_spinner()` — an
  indeterminate spinning animation, NOT the numeric up/down
  input we already have. Rename existing `Spinner` to
  `SpinButton` for clarity, add new `Spinner` as the busy
  indicator. (Will require a deprecation cycle.)
- [x] **Switch** ✓ done — `fdk_switch(active)` — animated slide toggle
  with rounded pill track and sliding circular knob. Distinct visual
  from `ToggleButton`.
- [ ] **FlowBox** — `fdk_flow_box(gap)` — responsive grid of
  homogeneous children with selection (single/multi/none). Used
  for emoji pickers, image galleries, app grids.
- [ ] **IconView** — `fdk_icon_view()` — list of icons with
  labels, optionally multi-select. Different from FlowBox in
  that children are (icon, label) pairs with a fixed visual
  template.
- [ ] **TreeView** — `fdk_tree_view(model)` — hierarchical
  list with expandable rows, columns, headers. Backed by
  `FDK_TreeModel` (see Data Binding section). Required for
  file managers, settings trees, debug log viewers. Big-ticket
  item — easily 1000+ LOC.
- [ ] **ListView** — `fdk_list_view(model)` — flat list backed
  by `FDK_ListModel`. Unlike a dropdown, this is a scrollable
  on-screen list (file list, contact list, message list).
  Includes virtualization (only render visible rows).
- [ ] **VolumeButton** — `fdk_volume_button(level)` — button
  showing a speaker icon; clicking pops up a vertical slider.
- [ ] **RecentChooser** — `fdk_recent_chooser()` — picker for
  recently-used files. Reads `~/.local/share/recently-used.xbel`.
- [ ] **ActionBar** — `fdk_action_bar()` — bottom bar with
  left-aligned and right-aligned action buttons. Used by
  GNOME-style detail views.
- [ ] **HeaderBar** — `fdk_header_bar()` — combined titlebar
  + action buttons (save, open, menu). Replaces the simple
  `Titlebar` for apps that want chrome-integrated actions.
  Build on CSD.
- [ ] **InfoBar** — `fdk_info_bar(kind, message)` —
  dismissible inline message banner (info/warning/error/
  question). Used for non-blocking notifications inside the
  window (as opposed to toasts which float).

### Data Binding & Model/View — `v0.4`

- [ ] **`FDK_Model`** — observable property bag. `fdk_model_set
  (model, key, value)`, `fdk_model_get(model, key)`, signals on
  change. Foundation for data binding.
- [ ] **`FDK_ListModel`** — ordered list of items with
  `items-changed` signal. `fdk_list_model_append/remove/insert/
  clear/get_n_items/get_item`. Backs `ListView` and `IconView`.
- [ ] **`FDK_TreeModel`** — hierarchical model with `row-inserted
  / row-removed / row-changed` signals. Backs `TreeView`.
- [ ] **Property binding** — `fdk_bind(source_model, source_key,
  target_widget, target_property, flags)` — two-way or one-way
  binding between a model field and a widget property. Updates
  flow automatically. Like GLib `GBinding`.
- [ ] **Signal/slot system** — `fdk_signal_new(name)`,
  `fdk_signal_connect(object, signal, callback, userdata)`,
  `fdk_signal_emit(object, signal, ...)`. Replacement for the
  ad-hoc `_on_change` callbacks scattered across the current API.
  Cleaner for language bindings. Optional — existing callbacks
  remain.

### Rendering Depth — `v0.5`

- [ ] **Vulkan backend** — `VkSurfaceKHR` via
  `VK_KHR_wayland_surface` / `VK_KHR_xlib_surface`. Lower CPU
  overhead than OpenGL, native access to compute shaders for
  blur, and a path to raytracing for future UI effects. Today's
  `FDK_RENDER_VULKAN` enum value is a placeholder; this is the
  real implementation.
- [ ] **`wl_surface_frame` callbacks** — truly vsync-driven
  repaint. Today FDK repaints on a timer; with frame callbacks,
  repaint is synchronized to the compositor's refresh, eliminating
  tearing and reducing idle CPU.
- [ ] **Scene graph** — retained-mode render tree (nodes for
  rectangles, text, images, transforms, clips, effects). Each
  frame walks the scene graph, builds a draw list, and submits
  to the backend. Enables dirty-rect rendering, GPU instancing,
  and declarative animation. Big architectural change.
- [ ] **Dirty-rect rendering** — only repaint the regions of
  the window that actually changed. Today FDK repaints the entire
  window every frame. For a 4K display this is a massive waste.
  Track dirty rects per widget, union them, scissor the repaint.
- [ ] **Path-based 2D rendering** — SVG-like path API:
  `fdk_path_move_to/line_to/cubic_to/quadratic_to/arc/close`,
  `fdk_path_fill(path, paint)`, `fdk_path_stroke(path, paint,
  width, cap, join)`. Used for icons, custom widgets, vector
  illustrations. Paint can be solid, gradient, or pattern.
- [ ] **Multi-stop gradients** — extend `FDK_Gradient` to
  support >4 stops (currently capped at 4), radial gradients,
  conic gradients. Required for modern UI design (subtle depth
  shading, glows).
- [ ] **Blend modes** — `fdk_set_blend_mode(mode)` — multiply,
  screen, overlay, darken, lighten, color-dodge, etc. Used for
  compositing layered effects.
- [ ] **Filters** — `fdk_filter_blur(rect, radius)`,
  `fdk_filter_color_matrix(rect, matrix)`,
  `fdk_filter_drop_shadow(rect, dx, dy, blur, color)`. Used
  for backdrop-blur (frosted glass), color shifting, soft
  shadows beyond the current box-blur shadow.
- [ ] **Image scaling filters** — nearest, bilinear, cubic,
  mipmaps. Today FDK uses nearest by default; high-quality
  image widgets need bilinear+mipmaps.
- [ ] **GPU instancing** — when rendering repeated identical
  widgets (list rows, grid cells), submit one draw call with
  per-instance transforms instead of N draw calls. Major win
  for long lists.
- [ ] **Glyph cache with LRU** — today's GL backend has a glyph
  atlas but no eviction. Long-running apps with many fonts /
  sizes eventually fill the atlas and stall. Add LRU eviction
  + multi-atlas.

### DPI & Scaling — `v0.2`

- [x] **HiDPI awareness (X11)** ✓ done — reads `Xft.dpi` from the
  RESOURCE_MANAGER property via `XGetWindowProperty` (not the cached
  `XResourceManagerString`). Honors `GDK_SCALE` env var override
  (matches GDK/Qt behavior). Range 0.5-8.0, clamped. Exposed via
  `fdk_window_get_scale()` and `fdk_window_get_dpi()` public API.
  See `src/platform/x11.c`.
- [x] **HiDPI awareness (Wayland)** ✓ done — binds `wl_output`, reads
  compositor-reported scale factor via `output_scale` callback.
  `wl_window_get_scale()` returns the real scale. `FDK_SCALE` env var
  overrides on both backends. Auto-scaling applied in `fdk_ui_create`
  and `fdk_ui_set_theme`. See `src/platform/wayland.c`.
- [ ] **Fractional scaling** — `wp_fractional_scale_v1`
  protocol. Lets the compositor tell the client to render at
  1.25× / 1.5× / 1.75× (the integer-only `wl_surface.set_buffer
  _scale` doesn't cover these). Render at 2× then downscale, or
  render at the fractional scale directly.
- [ ] **Per-monitor DPI** — multi-monitor setups with different
  DPIs (4K laptop + 1080p external). Each output has its own
  scale; windows dragged between outputs re-render at the new
  scale.
- [ ] **Live DPI changes** — when the user changes display
  scale mid-session (GNOME Settings → Displays), re-layout all
  windows. Today FDK caches font sizes at load time and never
  re-scales.
- [ ] **Cursor scaling** — load HiDPI cursor themes
  (`xcursor` `cursor-size` 32/48/64) matching the active scale.
  Today cursors stay at 24px regardless, looking tiny on 2×.

### Wayland Protocols — ongoing

Each item is a separate protocol extension FDK should bind. Linux
desktop users have come to expect these.

- [ ] **`xdg-activation-v1`** — focus stealing / activation
  tokens. Lets a launcher (or another app) request focus for a
  new window without rude grabs. Without this, FDK apps launched
  from a dock may appear behind the focused window.
- [ ] **`idle-inhibit-unstable-v1`** — prevent screensaver during
  video playback. `fdk_window_set_inhibit_idle(win, true)`.
- [ ] **`ext-idle-notification-v1`** — notify app when user goes
  idle / returns. Used by chat apps (auto-away), by video players
  (pause when idle), etc.
- [ ] **`ext-session-lock-v1`** — lock-screen surfaces. Lets FDK
  apps act as lock screens. Security-sensitive — implement carefully.
- [ ] **`wlr-foreign-toplevel-management-unstable-v1`** — taskbar
  / dock integration. Exposes the list of open windows across all
  apps so a taskbar (or FDK's own taskbar) can show them.
- [ ] **`wlr-screencopy-unstable-v1`** — screen capture. Used by
  screenshot tools and screen recorders. Also used for the
  eyedropper in `ColorChooser`.
- [ ] **`cursor-shape-v1`** (aka `wp_cursor_shape_manager_v1`) —
  named cursor shapes (RFC-defined strings like "text", "pointer",
  "ns-resize") without needing to ship cursor theme files. The
  compositor renders the shape.
- [ ] **`xdg-dialog-v1`** — mark a window as a modal dialog
  attached to a parent. Compositors can render a dimmed backdrop
  behind the dialog and prevent focus from leaving.
- [ ] **`xdg-system-bell-v1`** — audible bell. FDK apps can ring
  the system bell (configurable per-compositor — visual flash,
  sound, or both) without linking to libcanberra.
- [ ] **`wp-tearing-control-v1`** — explicit tearing for game /
  video windows (VRR / FreeSync). Lets the compositor present the
  surface without vsync when the app opts in.
- [ ] **`wp-alpha-modifier-v1`** — per-surface opacity. Useful
  for tooltips, popovers, notification overlays.
- [ ] **`wp-content-type-v1`** — hint to compositor: this surface
  is photo / video / text / game. Compositor can adjust refresh
  rate (variable refresh), color, or upscaling.
- [ ] **`wp-single-pixel-buffer`** — single-pixel buffers for
  solid-color surfaces. Avoids allocating a full surface for
  things like dim backdrops.
- [ ] **`wp-viewporter`** — surface crop and scale. Used for
  video surfaces, image editors, partial screen capture.
- [ ] **`zwp-text-input-v3`** — IME (see Input section).
- [ ] **`zwp-input-method-v2`** — input method (for on-screen
  keyboards). Different from text-input: this is the
  **input-method side**, used to write a virtual keyboard app in
  FDK. Lower priority than text-input but listed for completeness.
- [ ] **`wp-presentation-time`** — precise presentation timing
  feedback. Lets apps know exactly when a frame was shown on
  screen, for video/audio sync.
- [ ] **`zwp-pointer-gestures-v1`** — pinch and swipe gestures
  from trackpads. Different from the gestures framework above
  (that's app-level; this is the protocol that delivers the raw
  trackpad pinch/swipe events).
- [ ] **`zwp-pointer-constraints-v1`** — lock pointer to window
  (for FPS-style games, design tools with infinite canvas).
- [ ] **`zwp-relative-pointer-manager-v1`** — relative pointer
  motion (no cursor jump), also for games.
- [ ] **`zwp-tablet-v2`** — tablet / stylus input (see Input
  section).
- [ ] **`zwp-keyboard-shortcuts-inhibit-v1`** — let apps
  override system shortcuts (e.g., a game wants to use Super
  key without triggering the compositor's overview).
- [ ] **`zxdg-output-manager-v1`** — logical output metadata
  (name, description, position) for multi-monitor apps.
- [ ] **`wp-drm-lease-device-v1`** — direct scanout for VR
  headsets and KMS-attached displays. Niche but listed for
  completeness.

### X11 Features — ongoing

X11 isn't going away soon. Each item below is a real gap.

- [ ] **EWMH `_NET_WM_STATE_DEMANDS_ATTENTION`** — urgency hint.
  Lets a window flash in the taskbar when it has new messages,
  finished a long task, etc.
- [ ] **EWMH `_NET_WM_STATE_ABOVE` / `_BELOW`** — keep-on-top
  / keep-on-bottom. Used by pin-on-top tools, docks, desktop
  widgets.
- [ ] **EWMH `_NET_WM_STATE_SKIP_TASKBAR` / `_SKIP_PAGER`** —
  hide from taskbar and pager. Used by docks, splash screens,
  desktop widgets.
- [ ] **EWMH `_NET_WORKAREA`** — multi-monitor workarea. Avoid
  placing windows where they overlap panels.
- [ ] **XRandR multi-monitor awareness** — detect monitor
  layout, per-monitor DPI, layout changes (hotplug).
- [ ] **XInput2 (XI2)** — touch, multitouch, tablet, raw
  motion. Replaces core X input for any modern feature.
- [ ] **XFixes** — cursor visibility (`XFixesHideCursor`),
  region tracking, selection notification.
- [ ] **XShape** — non-rectangular windows. Used by rounded
  splash screens, custom-shaped widgets, screen-edge triggers.
- [ ] **XComposite + XRedirect** — off-screen rendering for
  embedded windows, thumbnails, previews.
- [ ] **XPresent** — vsync extension. X11 equivalent of
  `wl_surface_frame`.

### Application Lifecycle — `v0.3`

- [ ] **App ID** — reverse-DNS identifier (`org.fadedream.
  texteditor`). Set via `fdk_init(info)`'s `app_id` field. Used
  for desktop file lookup, icon lookup, Wayland app_id, X11
  `_GTK_APPLICATION_ID`.
- [ ] **Desktop file** — `~/.local/share/applications/<id>.desktop`.
  Generator tool `fdk-desktop-gen` writes one from a JSON spec.
  Used by app launchers and the start menu.
- [ ] **AppStream metadata** — `~/.local/share/metainfo/<id>.
  metainfo.xml`. Required for software stores (GNOME Software,
  KDE Discover, AppCenter).
- [ ] **Single-instance** — abstract socket lock
  (`/tmp/.fdk-<id>-lock`) or fcntl lock on
  `~/.cache/<id>/lock`. If another instance is running, send
  it the open-files request via a Unix socket and exit.
- [ ] **MimeType registration** — `~/.config/mimeapps.list`
  entries. Lets the user pick an FDK app as the default for
  a file type.
- [ ] **URL scheme handler** — `.desktop` `MimeType=x-scheme-
  handler/<scheme>;`. Lets FDK apps handle custom URL schemes.
- [ ] **Window session management** — XSMP (X11) or
  `systemd-system-session` (systemd). Lets the user log out
  and have FDK apps restore their windows on next login.
- [ ] **Application menu** — `fdk_app_menu(model)` populates
  the GNOME-Shell-style app menu. Backed by `GMenu`-equivalent
  XML model.
- [ ] **Recent files manager** — read / write
  `~/.local/share/recently-used.xbel`. Shared with GTK and Qt
  apps, so recently-used files appear in both ecosystems.
- [ ] **Settings persistence** — `~/.config/<id>/settings.ini`
  with a simple key/value API (`fdk_settings_get_string/int/
  bool/float`, `_set_*`, `_watch`). Equivalent to `GSettings`
  without the schema compilation step.

### Notifications — `v0.4`

FDK is explicitly D-Bus-free, which means no `libnotify`. Two
options for app-visible notifications:

- [ ] **Built-in notification daemon** — FDK ships an optional
  `fdk-notifyd` daemon that listens on a Unix socket and renders
  notifications using FDK itself. Apps call `fdk_notify_send()`,
  which connects to the daemon. Toaster popup in the corner of
  the screen.
- [ ] **OR accept the trade-off** — document that FDK apps
  don't send system notifications and rely on in-app toasts
  (`fdk_notify()`, already implemented) for user feedback. The
  project's "no D-Bus" stance makes this acceptable.

The first option is the production-viable path. The second is
the minimalist path. The roadmap tracks the first; a final
decision can wait until v0.4.

### Theming Depth — `v0.3`

- [ ] **Theme animation** — when a widget transitions between
  states (normal → hover → pressed), animate the color change
  over ~150ms. Today the transition is instant. Adds polish
  without changing the API.
- [ ] **`prefers-color-scheme` auto-switch** — `.fdktheme`
  files can declare a `[dark]` and `[light]` section. FDK
  auto-selects based on `org.gnome.desktop.interface.color-
  scheme` (X11) or the Wayland settings protocol.
- [ ] **Color computation** — `fdk_color_mix(a, b, t)`,
  `fdk_color_lighten(c, pct)`, `fdk_color_darken(c, pct)`,
  `fdk_color_alpha(c, a)`. Lets `.fdktheme` files express
  derived colors: `accent_hover = lighten(accent, 10%)`.
- [ ] **State-based selectors** — `.fdktheme` already supports
  `[button.danger]` variants. Extend to state-based:
  `[button:disabled]`, `[button:hover]`, `[button:focus]`.
  Today state colors are baked into the theme as separate
  fields; this is a generalization.
- [ ] **Theme inheritance** — `extends = "faded-dream"` at the
  top of a `.fdktheme` file. Loads the parent theme, then
  applies overrides. Lets users make minimal customization
  without copying the entire base theme.
- [ ] **Live theme editing tool** — `fdk-theme-edit` GUI app.
  Shows a widget gallery on the right, a theme editor on the
  left. Edit a color, see it live. Save → writes `.fdktheme`.
  Like GTK's `gtk4-widget-selector` + a color picker.

### Tooling & Ecosystem — `v0.6`

- [ ] **`.fdkui` declarative UI markup** — XML or JSON format
  for describing widget trees. `fdk_ui_load_from_file(path)`
  builds the tree at runtime. Like Qt's `.ui` files or QML.
  Foundation for any visual designer tool.
- [ ] **FDK Designer** — visual UI builder that outputs
  `.fdkui` files. Drag widgets from a palette, set properties
  in an inspector, preview live. Like Qt Designer or
  Cambalache.
- [ ] **`fdk-doc`** — Doxygen-like API doc generator. Reads
  `/** */` comments from headers, outputs HTML. Match GTK's
  gtk-doc output style for familiarity.
- [ ] **`fdk-inspect`** — runtime widget tree inspector.
  Hover over a widget in any FDK app, see its type, style,
  bounds, children. Like GTK Inspector.
- [ ] **`fdk-bench`** — performance benchmark suite. Frame
  time, layout time, paint time, glyph rasterization time,
  memory. Track regressions across versions.
- [ ] **`fdk-trace`** — instrumented tracing. Per-frame
  timeline of layout / paint / event dispatch. Export to
  `perfetto` trace format.

### Language Bindings — `v0.6`

The C API is the source of truth; bindings sit on top. Each
binding is a separate repo (`fdk-rs`, `fdk-zig`, etc.).

- [ ] **Rust bindings** — idiomatic Rust API via `bindgen` +
  hand-written wrapper types. RAII for widget lifecycle,
  `Result<>` for fallible calls, `cargo-fdk` for project
  scaffolding.
- [ ] **Zig bindings** — `@cImport` for the raw API, plus
  idiomatic Zig wrappers. Zig is a natural fit for FDK's
  manual-memory C style.
- [ ] **Python bindings** — `cffi`-based (no GIL issues
  since FDK is C). Lets users prototype UIs in Python and
  port to C later.
- [ ] **Lua bindings** — for embedding FDK in game engines /
  editors that already use Lua for scripting.
- [ ] **Vala bindings** — ironic since Vala targets GObject,
  but a VAPI file is straightforward. Lets existing GNOME
  Vala codebases incrementally adopt FDK.

### Performance — `v0.5`

- [ ] **Glyph cache LRU** — see Rendering Depth.
- [ ] **Texture atlas** — already partial in GL backend;
  extend to Vulkan.
- [ ] **Instanced rendering** — see Rendering Depth.
- [ ] **Dirty-rect repaint** — see Rendering Depth.
- [ ] **Frame skipping** — under load, skip repaints instead
  of accumulating latency. Detect via `fdk_time_ms()` delta
  vs. expected frame time.
- [ ] **Power-aware animation** — when on battery (read
  `/sys/class/power_supply/AC0/online`), scale animations
  down to 30fps and reduce shadow blur radius. Saves battery
  on laptops.
- [ ] **GPU compute blur** — compute shaders in the Vulkan
  backend for blur, replacing the CPU box-blur in the
  software backend.

### Testing — ongoing

- [ ] **Visual regression tests** — render each widget to a
  PNG, compare against a baseline. Catches visual regressions
  that unit tests miss.
- [ ] **Property-based tests** — for the layout engine:
  generate random widget trees, assert that layout never
  crashes, never overflows the window.
- [ ] **Fuzzing** — `libFuzzer` harnesses for `.fdktheme`
  parser, `.fdkui` parser, image decoders.
- [ ] **Performance benchmarks** — see `fdk-bench` above.
- [ ] **Widget conformance tests** — every widget has a test
  suite: keyboard interaction, focus, disable, theme variants,
  RTL mirroring.
- [ ] **Multi-backend equivalence** — for any given widget
  tree, the software and OpenGL backends must produce
  pixel-identical output (within a small tolerance). Today
  there is no such test.
- [ ] **Multi-window / multi-monitor tests** — extend
  `tests/test_watch_multiwindow.c` to cover the new
  multi-monitor / multi-DPI scenarios.

### Documentation — `v1.0`

- [ ] **API reference** — see `fdk-doc` above. Every public
  function documented with parameters, return value, lifecycle,
  thread-safety, example.
- [ ] **Tutorial / getting-started guide** — "Hello World" →
  first interactive app → first multi-window app → first themed
  app → first CJK-ready app.
- [ ] **Widget gallery** — visual reference of every widget
  in every bundled theme. Like `gtk4-widget-editor`.
- [ ] **Migration guide (GTK → FDK)** — table mapping every
  GTK4 widget / API to its FDK equivalent (or "not yet
  implemented"). Reduces adoption friction.
- [ ] **Migration guide (Qt → FDK)** — same for Qt6.
- [ ] **Architecture overview** — internals for contributors:
  platform vtable, render vtable, widget tree, theme system,
  event routing. Reduces the "scary 5000-line widget.c"
  factor.

### Security — ongoing

- [ ] **Wayland security context** — sandbox-aware (for
  Flatpak-style apps that use `bwrap` / `portal`). Document
  which APIs work sandboxed and which need portals.
- [ ] **X11 input validation** — validate every atom, every
  selection target, every property length on read. X11
  clients can send malicious data; FDK must not crash.
- [ ] **Memory safety** — full ASan + UBSan + TSan clean on
  every release. Already largely true; add CI to enforce.
- [ ] **`seccomp` filter for `fdk-notifyd`** — if we ship a
  notification daemon, sandbox it tightly.

### Distribution / Packaging — `v0.6`

- [ ] **Flatpak runtime** — `org.fdk.Platform` / `org.fdk.Sdk`
  Flatpak runtime. Lets FDK apps ship as Flatpaks without
  bundling FDK itself.
- [ ] **AppImage recipe** — self-contained bundle for users
  who don't want to install.
- [ ] **Nix flake** — NixOS users expect a flake.
- [ ] **vcpkg recipe** — even though FDK is Linux-only,
  vcpkg has a Linux target and some users use vcpkg on
  Linux.
- [ ] **Musl static build** — for portable binaries that
  work on any distro regardless of glibc version.

### Subprojects (separate repositories)

- [ ] **FDK Overlay** — compositor overlay layer built on FDK.
- [ ] **GTK Theme Bridge** — reads the active GTK theme and
  translates its color tokens into a `.fdktheme` file
  automatically, so FDK apps and GTK apps look visually
  consistent without manual coordination.
- [ ] **Qt Theme Bridge** — reads the active Qt/KDE theme
  (via `~/.config/qt5ct/`, `~/.config/qt6ct/`, or Kvantum
  theme files) and translates its color palette into a
  `.fdktheme` file automatically, so FDK apps look consistent
  with Qt apps on KDE / LXQt setups. No Qt dependency in FDK
  itself — the bridge reads Qt's plain-text config files
  directly.

### Original (pre-expansion) post-v1.0 items — preserved

These items were on the roadmap before the v0.2+ expansion
above. They are tracked here for continuity; most have been
absorbed into the larger sections.

- [ ] **Per-app theme packaging** — a standard way for an
  FDK app to ship its own `.fdktheme` files so `fdk-theme
  list` can discover them after the app is installed.
  *(Partially covered by the App ID + Desktop file work
  in Application Lifecycle; the per-app themes search path
  is `~/.local/share/<id>/themes/`.)*
- [ ] **Notifications** — moved to the Notifications section
  above.
- [ ] **Vulkan backend** — moved to Rendering Depth above.
- [ ] **`wl_surface_frame` callback** — moved to Rendering
  Depth above.
- [ ] **IME / compose sequences** — moved to Input above.
- [ ] **Text shaping / RTL** — moved to Text & Typography
  above.
- [ ] **Accessibility** — moved to its own section above.

---

## Dependency audit (all permissive — no GPL/LGPL contamination)

Planned additions (in `v0.2`+): `harfbuzz` (MIT) for text shaping,
plus various `wayland-protocols` extensions listed in the Wayland
Protocols section above. All new deps stay permissive — no GPL/LGPL
contamination. (FriBidi was previously considered for BiDi but
rejected — LGPL. BiDi is now implemented in-tree in `src/render/bidi.c`.)

| Library | License | Purpose |
|---|---|---|
| libwayland-client | MIT | Wayland platform backend |
| libxcb / Xlib | MIT/X11 | X11 platform backend |
| xdg-shell (wayland-protocols) | MIT | Window management protocol — surfaces, toplevels, state |
| xdg-decoration-unstable-v1 (wayland-protocols) | MIT | CSD/SSD negotiation |
| EWMH / `_NET_WM_STATE` | spec only | X11 maximize/fullscreen/state via WM properties |
| libxkbcommon | MIT | Keyboard layout + Unicode input |
| wayland-cursor | MIT | Cursor shape changes |
| FreeType | FTL (BSD-like) | Font rasterisation |
| libGL / EGL | spec only | OpenGL + EGL render backend |
| wayland-egl | MIT | EGL window surface on Wayland |
| pthread | system | inotify hot-reload background thread |
| stb_image.h (optional) | MIT/Public Domain | PNG / JPEG image loading |
| harfbuzz (optional) | MIT | Text shaping — Arabic, Hebrew, Indic, complex scripts |

---

## File structure

```
fdk/
├── include/fdk/
│   ├── fdk.h                  ← core types, FDK_Gradient, FDK_Shadow
│   └── fdk_widget.h           ← FDK_Theme, FDK_WidgetStyle, full public API
├── src/
│   ├── core/
│   │   ├── core.c             ← init, shutdown, clipboard, app_name stash
│   │   ├── core_internal.h
│   │   ├── test_internal.h    ← watch-list/theme introspection for tests only, not public API
│   │   ├── tween.c            ← animation engine (global pool, but safe under multi-window — see roadmap item)
│   │   └── theme.c            ← .fdktheme parser, font scanner, per-UI inotify watch list, 3-tier resolver
│   ├── platform/
│   │   ├── wayland.c          ← Wayland backend incl. clipboard, data device
│   │   └── x11.c              ← X11 backend incl. clipboard, selection protocol
│   ├── render/
│   │   ├── software.c         ← CPU renderer, gradients, box-blur shadows
│   │   └── opengl.c           ← GL 3.3 batch renderer, glyph atlas
│   └── widgets/
│       └── widget.c           ← all 19 widget types, layout, paint, resolve_style
├── themes/                    ← bundled .fdktheme reference files (not auto-installed)
│   ├── faded-dream.fdktheme
│   ├── void.fdktheme
│   └── rose.fdktheme
├── examples/                  ← consolidated from 6 apps down to 3: hello/,
│                                  widgets/, and showcase/ retired, their real
│                                  (non-overlapping) coverage folded into
│                                  theme-switcher/ — see roadmap item above
│   ├── theme-switcher/        ← every widget type incl. tabs/menubar/toasts,
│                                  live switching, hot-reload demo, and both
│                                  ways to reference a widget (direct pointer
│                                  and fdk_widget_find()-by-tag)
│   ├── systemwide-test/       ← tests fdk_theme_watch_conf() live re-theme
│   └── two-window-demo/       ← two real windows, independent fdk_theme_watch() each,
│                                  proves the multi-window fix — see roadmap item above
├── tools/
│   └── fdk-theme/             ← CLI: list/set/unset/show system-wide + per-app themes
├── tests/
│   ├── test_theme.c              ← 49+ headless assertions, runs via ctest
│   ├── test_watch_multiwindow.c  ← per-UI watch independence, runs via ctest
│   └── CMakeLists.txt
├── packaging/
│   ├── PKGBUILD               ← Arch Linux (.pkg.tar.zst)
│   ├── fdk.spec               ← RPM (Fedora / openSUSE)
│   └── debian/                ← Debian / Ubuntu (.deb)
├── sublicense/
│   └── SUBLICENSE             ← terms for Dependent Projects using FDK as a library
├── .gitignore
├── CMakeLists.txt
├── LICENSE
├── README.md
└── ROADMAP.md
```
