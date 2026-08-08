# Changelog

All notable changes to FDK (Faded Dream Kit) are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/).

---

## [0.2.0] — 2026-07-15

Feature parity between X11 and Wayland backends. Text rendering pipeline
rebuilt from scratch with HarfBuzz shaping and an in-tree UAX #9 BiDi
implementation (no FriBidi/LGPL dependency). HiDPI auto-scaling and IME
support on both backends. 8 new widgets. Interactive text playground app.

### Added

**Text shaping & BiDi:**
- HarfBuzz integration — Arabic, Hebrew, Indic, and other complex scripts
  shape correctly. Ligatures (fi, ffi) form. Falls back to codepoint-by-
  codepoint shaping when HarfBuzz is not available. (`src/render/shape.c`)
- In-tree UAX #9 BiDi implementation — Unicode Bidirectional Algorithm
  written from scratch in `src/render/bidi.c`. No FriBidi dependency
  (FriBidi is LGPL-2.1+, FDK is proprietary — cannot link LGPL code).
  Covers: P1-P3 paragraph level detection, X1-X10 explicit embedding
  (RLE/LRE/RLO/LRO/PDF), W1-W7 weak type resolution, N0 bracket pair
  resolution (64 bracket pairs from BidiBrackets.txt), N1-N2 neutral
  resolution, I1-I2 implicit levels, L1 trailing whitespace, L2 reverse
  with X9 compaction. Character type lookup covers all Unicode 16.0
  RTL/AL ranges via @missing directives from DerivedBidiClass.txt.
- OpenGL glyph cache key changed from codepoint to FreeType glyph ID
  (required for HarfBuzz — same codepoint maps to different glyphs
  contextually for Arabic positional forms and ligatures).

**HiDPI auto-scaling:**
- `fdk_window_get_scale()` / `fdk_window_get_dpi()` public API.
- X11: reads `Xft.dpi` from RESOURCE_MANAGER via `XGetWindowProperty`.
  Honors `GDK_SCALE` env var override (matches GDK/Qt behavior).
- Wayland: binds `wl_output`, reads compositor-reported scale factor.
- `FDK_SCALE` env var — works on both backends, overrides everything.
  Priority: `FDK_SCALE` > `GDK_SCALE` (X11) / `wl_output` (Wayland) > 1.0.
- Auto-scales theme fonts (via `FT_Set_Pixel_Sizes` + HarfBuzz re-init),
  layout values (radius, scrollbar width, gaps, padding, line height).
- Applied in both `fdk_ui_create()` (auto-resolved themes) and
  `fdk_ui_set_theme()` (explicit themes set after creation).
- `FDK_EVENT_SCALE_CHANGE` event type defined (not yet fired — will
  trigger when per-monitor DPI tracking lands in v0.3).

**IME (Input Method Editor):**
- `FDK_EVENT_IME_COMMIT` event type — carries multi-codepoint UTF-8
  strings (e.g., a kanji lookup commits 2-3 chars at once). Distinct
  from `FDK_EVENT_KEY_DOWN` which carries a single codepoint.
- X11: XIM via `XOpenIM` + `XCreateIC` (`XIMPreeditNothing |
  XIMStatusNothing` style). All X events filtered through
  `XFilterEvent`. `Xutf8LookupString` before `XLookupString` on
  KeyPress. Works with IBus, Fcitx, SCIM, uim.
- Wayland: `zwp_text_input_v3` protocol. Binds
  `zwp_text_input_manager_v3`, creates text_input from seat.
  `ti_enter` enables text input on focus, `ti_commit_string` pushes
  `FDK_EVENT_IME_COMMIT`. Preedit display deferred (compositor shows
  its own preedit window on most compositors).
- Widget dispatch: `FDK_EVENT_IME_COMMIT` inserts committed UTF-8 at
  cursor of focused `TextInput`, `TextArea`, or `SearchEntry`.

**New widgets (8):**
- `fdk_switch(active)` — animated slide toggle with rounded pill track
  and sliding circular knob. Knob animates via `tick_animations`.
- `fdk_level_bar(value, max, segments)` — segmented discrete progress
  indicator (signal strength bars, battery level).
- `fdk_search_entry(placeholder)` — text input with magnifying-glass
  icon (drawn with primitives), clear (×) button, 300ms debounced
  `on_search` callback. Full keyboard support (Backspace, Delete,
  Left/Right/Home/End, Escape clears, Enter fires search immediately).
- `fdk_expander(label, child)` — collapsible section with ▶/▼ disclosure
  triangle. Click header to toggle. Child laid out below header when
  expanded. Height grows/shrinks via `measure_widget` + `need_relayout`.
- `fdk_status_bar()` — bottom bar with push/pop message stack (16 deep).
- `fdk_grid(cols, homogeneous)` — row/col layout with `fdk_grid_add(grid,
  child, row, col, row_span, col_span)` + auto-positioning
  `fdk_grid_add_next()`.
- `fdk_popover(content)` — transient anchored popup (basic show/hide API).
- `fdk_calendar()` — month-view date picker with nav arrows, click-to-
  select, leap year handling, `FDK_Date` struct, `on_change` callback.

**Build feature query:**
- `fdk_get_features()` / `fdk_has_feature(name)` — runtime introspection
  of which optional features libfdk was built with.

**Wayland protocol support:**
- `wl_output` — bound for HiDPI scale factor detection.
- `zwp_text_input_v3` — bound for IME support (auto-detected by CMake,
  gated on `FDK_HAVE_TEXT_INPUT`).
- `ptr_enter` now generates `FDK_EVENT_MOUSE_MOVE` so `ui->mouse_x/y`
  are correct from the moment the pointer enters the window.

**Example apps:**
- `examples/text-playground/` — interactive BiDi/shaping preview app.
  Type or paste any text (Arabic, Hebrew, CJK, mixed LTR/RTL), see it
  rendered live. Font selector cycles through system fonts. 8 preset
  buttons. Live direction detection (LTR/RTL), byte/codepoint count.
  Visual showcase of all new widgets (Switch, LevelBar, StatusBar,
  Expander, Calendar). CSD titlebar, scroll view, `FDK_DEBUG=1` env
  var for terminal + debug.log event logging.

### Changed

- `fdk_measure_text()` now routes through the shaping pipeline
  (HarfBuzz + BiDi) instead of codepoint-by-codepoint FreeType lookup.
- `fdk_draw_text()` in both software and OpenGL renderers routes
  through `fdk__shape_text()` → HarfBuzz `hb_shape()` → FreeType
  `FT_Load_Glyph()` (was `FT_Load_Char()` — codepoint lookup).
- `fdk_ui_create()` reads `fdk_window_get_scale()` and stores the
  scale factor. Auto-scales fonts and layout values when scale > 1.0.
- `fdk_ui_set_theme()` also applies auto-scaling (handles the pattern
  where apps load fonts after UI creation).
- `tick_animations()` now checks `SWITCH` widgets for animation state
  (knob_pos vs target) and walks `EXPANDER` children.
- Mouse wheel scroll on X11: only `ButtonPress` generates
  `FDK_EVENT_MOUSE_SCROLL` (was both press + release, causing double
  scroll). `ButtonRelease` for buttons 4/5 is silently swallowed.
- Scroll handler uses `ui->mouse_x/mouse_y` (tracked from
  `MOUSE_MOVE`) instead of `ev->mouse.x/y` (corrupted by the
  `FDK_Event` union — `mouse.y` and `scroll.dy` share memory).
- Text playground uses manual event loop with "wait for first
  EXPOSE/RESIZE" pattern (same as csd-demo) for reliable rendering
  under real WMs.

### Fixed

- **Scroll not working:** `FDK_Event` uses a union where `mouse.x/y`
  and `scroll.dx/dy` share the same memory. When the platform backend
  writes `scroll.dy = -1.0f`, it overwrites `mouse.y` with the float
  bit pattern of -1.0 (interpreted as int: -1082130432). The scroll
  handler was using `ev->mouse.x/y` for hit-testing, which always
  failed. Now uses `ui->mouse_x/mouse_y` (from `MOUSE_MOVE` events).
- **Scroll not updating until resize:** `fdk_ui_step` only ran
  `do_layout` on `RESIZE`/`EXPOSE` events. Added `do_layout` call
  after `MOUSE_SCROLL` events and after `need_relayout` flag set
  by expander toggle.
- **Scroll handler couldn't find scroll view:** `hit_test` uses
  `rect_contains(widget->rect, x, y)`, but when content is scrolled,
  children's rects are offset by `-scroll_y`. After scrolling, a
  widget visually under the cursor has a rect above the cursor.
  Replaced with tree BFS that directly finds `SCROLL_VIEW` whose own
  rect contains the mouse position (scroll view's rect doesn't change
  when content is scrolled).
- **Switch animation not ticking:** `tick_animations()` only checked
  `PROGRESS_BAR` for animation state. The switch's knob animation runs
  in `paint_widget`, but paint only runs when `ui->dirty` is true.
  Without `tick_animations` reporting the switch as still-animating,
  `ui->dirty` was never set during idle frames. Now checks if
  `knob_pos != target`.
- **Expander not expanding:** `h_hint` was `FDK_SIZE_FILL` (layout
  engine divides space among FILL children — expander got 0 height).
  Changed to `FDK_SIZE_WRAP` (layout uses `measure_widget` for natural
  height: 28px collapsed, 28 + child when expanded).
- **Expander child not laid out:** Layout pass had no code for
  expander children. Added `layout_widget()` call for child when
  expanded, plus `need_relayout` flag for full re-layout on toggle.
- **Expander child missing parent pointer:** `fdk_expander()` didn't
  set `child->parent = w`, so `hit_test` couldn't find the expander.
- **Calendar text overlap:** Calendar painted outside its allocated
  rect. Added `fdk_push_clip(r)` / `fdk_pop_clip()`. Increased
  intrinsic height from 200 to 240px.
- **Close button not working:** Event loop only checked
  `FDK_EVENT_CLOSE` before `fdk_ui_step()`, but the titlebar close
  handler sets `pending_close` during `MOUSE_UP` processing inside
  `fdk_ui_step()`, which converts it to `CLOSE` on return. Added
  post-step `CLOSE` check (same pattern as csd-demo).
- **Wayland DnD not working:** Three bugs: (1) `do_offer` MIME type
  callback was mapped to `do_target` (wrong name, no-op) — FDK never
  knew if `text/uri-list` was available. (2) `dd_enter` discarded the
  serial — `wl_data_offer_accept()` requires it; without `accept`,
  compositor never sends `drop`. (3) Duplicate function body in
  `dd_data_offer`. All fixed.
- **Wayland scroll not working:** `ptr_axis` callback didn't set
  `ev.mouse.x/y` (corrupted by union anyway). `ptr_enter` didn't
  generate `MOUSE_MOVE` event — `ui->mouse_x/y` stayed at (0,0)
  until first mouse motion. Both fixed.
- **Bamum misclassified as R:** Bamum (A6A0-A6FF) was in the type
  table as R but should be L per Unicode 16.0 @missing directives.
  Corrected — Bamum was deliberately designed left-to-right.
- **Yezidi/Arabic Extended-C collapsed:** 10E80-10EFF was a single AL
  range. Per @missing: Yezidi (10E80-10EBF) is R, Arabic Extended-C
  (10EC0-10EFF) is AL. Now split correctly.
- **Hebrew Presentation Forms misclassified:** FB1D-FB4F was falling
  through to the L Alphabetic Presentation block. Now correctly R.
- **All Unicode 16.0 RTL/AL Plane 1+ ranges added:** 0 of 123 ranges
  were covered; now all are, sourced from @missing directives.
- **ASCII bidi type table off-by-one:** Comment said "0x00-0x08" (9
  entries) but only 8 values were written, shifting everything by 1.
  Rewrote with explicit `[N] = ...` designators.

### Removed

- **FriBidi dependency** — the LGPL-2.1+ FriBidi library was removed.
  BiDi is now implemented in-tree in `src/render/bidi.c` (~1100 LOC,
  no external deps, no heap allocation in reordering path). All FDK
  dependencies are now permissive (MIT/BSD/FTL/Public Domain). No
  GPL, no LGPL, no copyleft contamination.

---

## [0.1.0] — 2026-07-14

First usable release. CSD, file dialog, DnD, resize, theme system,
20 widgets, multi-window support, 6 example apps, 3 test suites.
