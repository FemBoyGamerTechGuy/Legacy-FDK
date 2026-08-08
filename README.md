# FDK — Faded Dream Kit

A from-scratch GUI toolkit for Linux. No D-Bus, no GTK, no systemd —
just C11, FreeType, and your display server.

**Version:** 0.1.0
**Platform:** Linux (Wayland + X11)
**Rendering:** Software (CPU) + OpenGL 3.3
**Language:** C11
**License:** Proprietary — see [LICENSE](LICENSE) and [sublicense/SUBLICENSE](sublicense/SUBLICENSE)

---

## Overview

FDK is a lightweight GUI toolkit built for the Faded Dream project
ecosystem. It targets Linux desktops running Wayland or X11, with no
dependencies on the Red Hat / freedesktop.org ecosystem beyond Xorg
and Wayland themselves.

### Design principles

- **No D-Bus, no GTK, no systemd** — FDK talks directly to the display
  server. No portal daemons, no session bus, no init system coupling.
- **Permissive dependencies only** — FreeType (FTL/BSD), Wayland (MIT),
  X11 (MIT), xkbcommon (MIT), stb_image (Public Domain). No GPL, no
  LGPL, no copyleft contamination.
- **Single shared library** — `libfdk.so` is the only runtime dependency
  for FDK apps. No plugin system, no module loading, no dynamic
  dispatch beyond the platform/render vtable.
- **Software rendering by default** — works on any GPU (or no GPU).
  OpenGL 3.3 backend available as an opt-in per window.

---

## Features

### Widgets (23 types)

Container (hbox/vbox), Label, Button (with variants: accent, danger,
ghost), Text Input (cursor, selection, Ctrl shortcuts), TextArea
(multiline, scrollable), Checkbox, Toggle Button, Radio Button,
Separator, Custom Widget, Slider, Progress Bar (determinate +
indeterminate), Scroll View, Dropdown, Image, Badge, Tabs, MenuBar
(Alt mnemonics, keyboard nav), Context Menu, Titlebar (CSD),
Switch (animated slide toggle), LevelBar (segmented progress),
SearchEntry (text input with search icon + clear button + 300ms
debounced search callback).

### Client-side decorations (CSD)

Complete CSD support on both X11 and Wayland:

- Titlebar with close / minimize / maximize button glyphs
- Drag-to-move with 3px threshold (matches kitty / GTK / Qt)
- Double-click-to-maximize (configurable via `fdk_titlebar_set_dblclick_ms()`)
- Edge and corner resize with diagonal cursor support
- Flicker-free resize on X11 (backing pixmap + `NorthWestGravity` +
  no background fill + `realloc` instead of `free`+`calloc`)
- Live resize updates on X11 (client-side `XMoveResizeWindow` with
  root-relative coordinates)
- Compositor-driven resize on Wayland (`xdg_toplevel_resize`)
- Motif WM hints on X11 to suppress server-side decorations
- `zxdg_decoration_manager_v1` on Wayland for the same purpose
- `bool csd` field on `FDK_WindowDesc` for opt-in

### Theme system

- `.fdktheme` file format with variables, per-widget sections, named
  variants, gradients, shadows, fonts, and inline comments
- 3-tier resolution: developer force → per-app override → system-wide
- Live hot-reload via inotify (per-UI watch, race-free under TSan)
- `fdk-theme` CLI tool for managing themes system-wide
- Three bundled themes: `faded-dream`, `void`, `rose`
- `$XDG_DATA_HOME/fonts` scanned per XDG Base Directory Spec

### File dialog

Built-in modal file picker — no D-Bus, no XDG portal, no external
dependencies. Rendered entirely by FDK using its own widget primitives.

- Directory navigation (double-click to enter, `..` to go up)
- File filtering by glob pattern (`*.txt;*.md;*.c`)
- Keyboard navigation (Up/Down/Enter/Esc)
- Mouse scroll wheel
- Dotfiles hidden by default
- Directories sorted first, then files alphabetically
- 0% CPU when idle (blocks on event wait)

### Drag and drop

- X11: XDND protocol (XdndEnter / Position / Drop / Leave / Status /
  Finished)
- Wayland: `wl_data_device` with fd-based data transfer
- Same `fdk_window_set_drop_handler(win, cb, ud)` API on both backends
- Receives `text/uri-list` format (standard file drag from file managers)

### Window icons

- X11: `_NET_WM_ICON` set from decoded PNG (via vendored stb_image)
- Wayland: `xdg_toplevel_icon_v1` protocol (both icon-name and
  wl_shm buffer modes supported)
- `fdk_window_set_icon_name(win, name)` — XDG icon theme lookup
- `fdk_window_set_icon_from_file(win, path)` — direct PNG file loading

### Other

- Multi-window support (independent theme watches, per-window animation
  clocks, proper event routing)
- Clipboard (Wayland `wl_data_device`, X11 selection protocol)
- Tween / animation engine with 9 easing functions
- 10 cursor types including diagonal resize cursors
- `FDK_PLATFORM_AUTO` picks Wayland or X11 at runtime
- libxcursor is optional (falls back to X11 font cursors)
- `$ORIGIN` rpath on all executables (pre-built binaries are
  self-contained — no `LD_LIBRARY_PATH` needed)

### Text & Internationalization (v0.2)

- **HarfBuzz** text shaping — Arabic, Hebrew, Indic, Bengali, Thai,
  Tibetan, and other complex scripts shape correctly. Ligatures
  (fi, ffi) form. Falls back to codepoint-by-codepoint shaping when
  HarfBuzz is not available.
- **In-tree BiDi** (UAX #9) — Unicode Bidirectional Algorithm
  implemented from scratch in `src/render/bidi.c`. No FriBidi or
  other LGPL dependency. Covers paragraph level detection (P1-P3),
  explicit embedding (X1-X10), weak type resolution (W1-W7),
  neutral resolution (N1-N2), implicit levels (I1-I2), and
  L1-L2 reordering. Bracket pairs (N0) and isolate sequences
  (FSI/LRI/RLI/PDI) are deferred.
- **IME (Input Method Editor)** support on X11 via XIM — works with
  IBus, Fcitx, SCIM, uim, and any other XIM-capable IME. CJK text
  input works end-to-end. `FDK_EVENT_IME_COMMIT` event delivers
  multi-codepoint UTF-8 strings to focused text widgets.
- **HiDPI scaling** — `fdk_window_get_scale()` / `fdk_window_get_dpi()`
  expose the active scale factor. X11 backend reads `Xft.dpi` from
  RESOURCE_MANAGER and honors `GDK_SCALE` env var override. Wayland
  fractional scaling (`wp_fractional_scale_v1`) is a v0.3 item.
- **Build feature query** — `fdk_get_features()` /
  `fdk_has_feature(name)` for runtime introspection of which optional
  features this libfdk was built with.

---

## Building

### Prerequisites

| Dependency | Required? | License |
|-----------|-----------|---------|
| GCC 14+ (or any C11 compiler) | Yes | GPL (compiler only) |
| CMake 3.16+ | Yes | BSD |
| FreeType 2 | Yes | FTL (BSD-like) |
| harfbuzz | Auto-detected | MIT |
| libx11 | X11 backend | MIT |
| libxcursor | X11 backend (optional) | MIT |
| libwayland-client | Wayland backend | MIT |
| wayland-protocols | Wayland backend | MIT |
| wayland-scanner | Wayland backend | MIT |
| libxkbcommon | Wayland backend | MIT |
| libGL, libEGL, libwayland-egl | OpenGL backend | Spec only |
| stb_image.h | Auto-detected | Public Domain |

**All dependencies are permissive (MIT/BSD/FTL/Public Domain).** No
GPL, no LGPL, no copyleft contamination. The Unicode Bidirectional
Algorithm (UAX #9) is implemented in-tree in `src/render/bidi.c` —
no FriBidi or other LGPL dependency.

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `FDK_ENABLE_X11` | ON | Build X11 platform backend |
| `FDK_ENABLE_WAYLAND` | ON | Build Wayland platform backend |
| `FDK_ENABLE_OPENGL` | ON | Build OpenGL render backend |
| `FDK_BUILD_SHARED` | ON | Build libfdk.so |
| `FDK_BUILD_STATIC` | ON | Build libfdk.a |
| `FDK_BUILD_EXAMPLES` | ON | Build example apps |
| `FDK_BUILD_TESTS` | ON | Build headless test suite |
| `FDK_BUILD_TOOLS` | ON | Build fdk-theme CLI |
| `FDK_WITH_STB_IMAGE` | Auto | PNG/JPEG loading (auto-detected from third_party/stb/) |

---

## Examples

| App | Description |
|-----|-------------|
| `csd-demo/fdk_csd_demo` | CSD titlebar with close/minimize/maximize, drag, resize |
| `theme-switcher/fdk_theme_switcher` | All 20 widget types + live theme switching + menubar + tabs |
| `file-dialog-demo/fdk_file_dialog_demo` | Built-in file picker dialog |
| `dnd-demo/fdk_dnd_demo` | Drag files from a file manager into the window |
| `two-window-demo/fdk_two_window_demo` | Two independent windows with separate theme watches |
| `systemwide-test/fdk_systemwide_test` | System-wide theme resolution + live hot-reload |

---

## Tests

```bash
cd build && ctest --output-on-failure
```

| Test | Description | Assertions |
|------|-------------|------------|
| `theme_parser` | `.fdktheme` file parsing | 49+ |
| `watch_multiwindow` | Per-UI theme watch independence (TSan-clean) | Race-free |
| `fdk_theme_cli` | fdk-theme CLI smoke test (set/list/show/unset) | 32 |

---

## Packaging

Packaging recipes are included for three distributions:

- **Arch Linux:** `packaging/PKGBUILD`
- **Fedora / openSUSE:** `packaging/fdk.spec`
- **Debian / Ubuntu:** `packaging/debian/`

---

## Project structure

```
fdk/
├── include/fdk/
│   ├── fdk.h                  Core types, drawing API, window management
│   └── fdk_widget.h           Widget system, theme types, public widget API
├── src/
│   ├── core/
│   │   ├── core.c             Init, shutdown, window lifecycle, event loop
│   │   ├── core_internal.h    FDK_Window struct (internal)
│   │   ├── theme.c            .fdktheme parser, font scanner, inotify watch
│   │   └── tween.c            Animation engine
│   ├── platform/
│   │   ├── x11.c              X11 backend (Xlib, GLX, XDND, Motif hints)
│   │   ├── wayland.c          Wayland backend (xdg-shell, wl_shm, EGL, DnD)
│   │   └── platform_internal.h  Platform vtable
│   ├── render/
│   │   ├── software.c         CPU renderer (pixel buffer, gradients, shadows)
│   │   └── opengl.c           GL 3.3 renderer (batch vertices, glyph atlas)
│   └── widgets/
│       └── widget.c           All 20 widget types, layout, paint, dispatch
├── examples/                  6 example applications
├── tests/                     3 test suites
├── tools/fdk-theme/           CLI for managing system-wide themes
├── themes/                    3 bundled .fdktheme files
├── third_party/stb/           Vendored stb_image.h (public domain)
├── packaging/                 PKGBUILD, .spec, debian/
├── CMakeLists.txt
├── LICENSE
├── CHANGELOG.md
├── ROADMAP.md
└── README.md
```

---

## Contact

**Email:** faddeddreamproject@proton.me
**GitHub:** https://github.com/FemBoyGamerTechGuy/FDK
