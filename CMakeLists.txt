cmake_minimum_required(VERSION 3.16)
project(fdk VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Make every executable in this project find libfdk.so at runtime via
# $ORIGIN — so a pre-built binary + libfdk.so.0 in the same directory
# "just works" without requiring LD_LIBRARY_PATH or a system install.
# Also includes $ORIGIN/.. so test binaries in build/tests/ can find
# libfdk.so.0 in build/ during development.
# Applies to examples, tools, and tests. Set globally so individual
# target CMakeLists.txt files don't need to remember to add it.
set(CMAKE_BUILD_RPATH "$ORIGIN;$ORIGIN/..")
set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/..")

# ── Options ───────────────────────────────────────────────────────────────────
option(FDK_ENABLE_X11      "Build X11 platform backend"              ON)
option(FDK_ENABLE_WAYLAND  "Build Wayland platform backend"          ON)
option(FDK_ENABLE_OPENGL   "Build OpenGL render backend"             ON)
option(FDK_BUILD_SHARED    "Build libfdk.so alongside libfdk.a"      ON)
option(FDK_BUILD_EXAMPLES  "Build example applications"              ON)
option(FDK_BUILD_STATIC    "Build libfdk.a static library"           ON)
option(FDK_WITH_STB_IMAGE  "Enable PNG/JPEG loading via stb_image.h" OFF)

# Auto-detect stb_image.h if it's vendored in third_party/stb/
if(NOT FDK_WITH_STB_IMAGE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image.h")
        set(FDK_WITH_STB_IMAGE ON)
        set(FDK_STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
        message(STATUS "FDK: stb_image.h found in third_party/ — PNG/JPEG loading enabled")
    endif()
endif()
if(FDK_WITH_STB_IMAGE AND DEFINED FDK_STB_DIR)
    list(APPEND FDK_INCLUDE_DIRS ${FDK_STB_DIR})
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Freetype  REQUIRED)

set(FDK_SOURCES
    src/core/core.c
    src/core/tween.c
    src/core/theme.c
    src/render/software.c
    src/render/shape.c
    src/render/bidi.c
    src/widgets/widget.c
)
set(FDK_COMPILE_DEFS "")
set(FDK_LINK_LIBS    Freetype::Freetype m pthread)
set(FDK_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(FDK_EXTRA_TARGETS "")

# Pkg-config requires a flat list of -l flags for the .pc file
set(FDK_PC_LIBS      "-lm -lfreetype")
set(FDK_PC_REQUIRES  "freetype2")

# ── HarfBuzz (text shaping — Arabic, Hebrew, Indic, ligatures) ──────────────
# Optional but auto-detected. Without HarfBuzz, FDK falls back to
# codepoint-by-codepoint shaping (broken for complex scripts).
pkg_check_modules(HARFBUZZ harfbuzz)
if(HARFBUZZ_FOUND)
    list(APPEND FDK_LINK_LIBS    ${HARFBUZZ_LIBRARIES})
    list(APPEND FDK_INCLUDE_DIRS ${HARFBUZZ_INCLUDE_DIRS})
    list(APPEND FDK_COMPILE_DEFS FDK_HAVE_HARFBUZZ)
    string(APPEND FDK_PC_REQUIRES " harfbuzz")
    message(STATUS "FDK: HarfBuzz found — text shaping enabled")
else()
    message(WARNING "FDK: HarfBuzz not found — text shaping will use codepoint fallback (complex scripts broken)")
endif()

# ── Bidirectional text layout (UAX #9) ─────────────────────────────────────
# Implemented in-tree (src/render/bidi.c) — no external dependency.
# Previously FDK used FriBidi (LGPL-2.1+), but that violated the
# project's "no copyleft contamination" principle. The in-tree
# implementation is intentionally simplified — covers the common cases
# (paragraph level detection, weak type resolution W1-W7, neutral
# resolution N1-N2, implicit levels I1-I2, L1-L2 reorder) but does
# NOT implement: bracket pair matching (N0, requires Unicode data
# file), isolate sequences (X5a-X9, FSI/LRI/RLI/PDI — deferred).
# Pure LTR and most mixed LTR/RTL text works correctly.

# ── stb_image ─────────────────────────────────────────────────────────────────
if(FDK_WITH_STB_IMAGE)
    # The caller must make stb_image.h available on the include path.
    # A typical setup:  -DFDK_WITH_STB_IMAGE=ON -DFDK_STB_DIR=/path/to/stb
    if(DEFINED FDK_STB_DIR)
        list(APPEND FDK_INCLUDE_DIRS ${FDK_STB_DIR})
    endif()
    list(APPEND FDK_COMPILE_DEFS FDK_WITH_STB_IMAGE)
    message(STATUS "FDK: stb_image PNG/JPEG loader enabled")
endif()

# ── X11 ───────────────────────────────────────────────────────────────────────
if(FDK_ENABLE_X11)
    find_package(X11)
    if(X11_FOUND)
        list(APPEND FDK_SOURCES      src/platform/x11.c)
        list(APPEND FDK_COMPILE_DEFS FDK_HAVE_X11)
        list(APPEND FDK_LINK_LIBS    ${X11_LIBRARIES})
        list(APPEND FDK_INCLUDE_DIRS ${X11_INCLUDE_DIR})
        string(APPEND FDK_PC_LIBS    " -lX11")
        pkg_check_modules(XCURSOR xcursor)
        if(XCURSOR_FOUND)
            list(APPEND FDK_LINK_LIBS    ${XCURSOR_LIBRARIES})
            list(APPEND FDK_INCLUDE_DIRS ${XCURSOR_INCLUDE_DIRS})
            list(APPEND FDK_COMPILE_DEFS FDK_HAVE_XCURSOR)
            string(APPEND FDK_PC_LIBS    " -lXcursor")
        else()
            message(STATUS "FDK: libxcursor not found — X11 backend will use built-in core X cursor font only")
        endif()
        message(STATUS "FDK: X11 backend enabled")
    else()
        message(WARNING "FDK: X11 not found — skipping")
    endif()
endif()

# ── Wayland ───────────────────────────────────────────────────────────────────
if(FDK_ENABLE_WAYLAND)
    pkg_check_modules(WAYLAND       wayland-client)
    pkg_check_modules(WAYLAND_PROTO wayland-protocols)
    pkg_check_modules(XKBCOMMON     xkbcommon)
    pkg_check_modules(WAYLAND_CURSOR wayland-cursor)
    find_program(WAYLAND_SCANNER    wayland-scanner)

    if(WAYLAND_FOUND AND WAYLAND_PROTO_FOUND AND WAYLAND_SCANNER)
        execute_process(
            COMMAND pkg-config --variable=pkgdatadir wayland-protocols
            OUTPUT_VARIABLE WP_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(XDG_XML "${WP_DIR}/stable/xdg-shell/xdg-shell.xml")
        set(XDG_H   "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
        set(XDG_C   "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

        add_custom_command(
            OUTPUT  ${XDG_H}
            COMMAND ${WAYLAND_SCANNER} client-header ${XDG_XML} ${XDG_H}
            DEPENDS ${XDG_XML}
            COMMENT "wayland-scanner: xdg-shell client header"
        )
        add_custom_command(
            OUTPUT  ${XDG_C}
            COMMAND ${WAYLAND_SCANNER} private-code ${XDG_XML} ${XDG_C}
            DEPENDS ${XDG_XML}
            COMMENT "wayland-scanner: xdg-shell private code"
        )

        add_custom_target(fdk_xdg_shell_gen DEPENDS ${XDG_H} ${XDG_C})
        list(APPEND FDK_EXTRA_TARGETS fdk_xdg_shell_gen)

        # xdg-toplevel-icon — staging protocol, landed in wayland-protocols 1.37.
        # Gate on file existence so older distros just build without icon support.
        set(ICON_XML "${WP_DIR}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml")
        if(EXISTS "${ICON_XML}")
            set(ICON_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-client-protocol.h")
            set(ICON_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-protocol.c")
            add_custom_command(
                OUTPUT  ${ICON_H}
                COMMAND ${WAYLAND_SCANNER} client-header ${ICON_XML} ${ICON_H}
                DEPENDS ${ICON_XML}
                COMMENT "wayland-scanner: xdg-toplevel-icon client header"
            )
            add_custom_command(
                OUTPUT  ${ICON_C}
                COMMAND ${WAYLAND_SCANNER} private-code ${ICON_XML} ${ICON_C}
                DEPENDS ${ICON_XML}
                COMMENT "wayland-scanner: xdg-toplevel-icon private code"
            )
            add_custom_target(fdk_toplevel_icon_gen DEPENDS ${ICON_H} ${ICON_C})
            list(APPEND FDK_EXTRA_TARGETS fdk_toplevel_icon_gen)
            list(APPEND FDK_SOURCES      ${ICON_C})
            list(APPEND FDK_COMPILE_DEFS FDK_HAVE_TOPLEVEL_ICON)
            message(STATUS "FDK: xdg-toplevel-icon protocol enabled")
        else()
            message(STATUS "FDK: xdg-toplevel-icon not found (wayland-protocols < 1.37), icon support disabled")
        endif()

        # xdg-decoration-unstable-v1 — lets the client tell the compositor
        # whether to draw its own server-side decorations (title bar, borders)
        # or let the client draw them (CSD). Used by FDK_WindowDesc.csd.
        # The protocol is in the "unstable" directory but has been stable in
        # practice since 2018 — every major compositor (GNOME, KDE, wlroots,
        # Labwc, Sway, Hyprland) implements it.
        set(DECOR_XML "${WP_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
        if(EXISTS "${DECOR_XML}")
            set(DECOR_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h")
            set(DECOR_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c")
            add_custom_command(
                OUTPUT  ${DECOR_H}
                COMMAND ${WAYLAND_SCANNER} client-header ${DECOR_XML} ${DECOR_H}
                DEPENDS ${DECOR_XML}
                COMMENT "wayland-scanner: xdg-decoration client header"
            )
            add_custom_command(
                OUTPUT  ${DECOR_C}
                COMMAND ${WAYLAND_SCANNER} private-code ${DECOR_XML} ${DECOR_C}
                DEPENDS ${DECOR_XML}
                COMMENT "wayland-scanner: xdg-decoration private code"
            )
            add_custom_target(fdk_xdg_decoration_gen DEPENDS ${DECOR_H} ${DECOR_C})
            list(APPEND FDK_EXTRA_TARGETS fdk_xdg_decoration_gen)
            list(APPEND FDK_SOURCES      ${DECOR_C})
            list(APPEND FDK_COMPILE_DEFS FDK_HAVE_XDG_DECORATION)
            message(STATUS "FDK: xdg-decoration protocol enabled (CSD support)")
        else()
            message(STATUS "FDK: xdg-decoration not found, CSD negotiation disabled (compositor will always draw its own titlebar)")
        endif()

        # text-input-unstable-v3 — IME support (CJK input via fcitx5/ibus)
        set(TEXT_INPUT_XML "${WP_DIR}/unstable/text-input/text-input-unstable-v3.xml")
        if(EXISTS "${TEXT_INPUT_XML}")
            set(TI_H "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h")
            set(TI_C "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c")
            add_custom_command(
                OUTPUT  ${TI_H}
                COMMAND ${WAYLAND_SCANNER} client-header ${TEXT_INPUT_XML} ${TI_H}
                DEPENDS ${TEXT_INPUT_XML}
                COMMENT "wayland-scanner: text-input-v3 client header"
            )
            add_custom_command(
                OUTPUT  ${TI_C}
                COMMAND ${WAYLAND_SCANNER} private-code ${TEXT_INPUT_XML} ${TI_C}
                DEPENDS ${TEXT_INPUT_XML}
                COMMENT "wayland-scanner: text-input-v3 private code"
            )
            add_custom_target(fdk_text_input_gen DEPENDS ${TI_H} ${TI_C})
            list(APPEND FDK_EXTRA_TARGETS fdk_text_input_gen)
            list(APPEND FDK_SOURCES      ${TI_C})
            list(APPEND FDK_COMPILE_DEFS FDK_HAVE_TEXT_INPUT)
            message(STATUS "FDK: text-input-v3 protocol enabled (IME support)")
        else()
            message(STATUS "FDK: text-input-v3 not found, IME disabled on Wayland")
        endif()

        list(APPEND FDK_SOURCES      src/platform/wayland.c ${XDG_C})
        list(APPEND FDK_COMPILE_DEFS FDK_HAVE_WAYLAND)
        list(APPEND FDK_LINK_LIBS    ${WAYLAND_LIBRARIES})
        list(APPEND FDK_INCLUDE_DIRS
            ${WAYLAND_INCLUDE_DIRS}
            ${CMAKE_CURRENT_BINARY_DIR}
        )
        string(APPEND FDK_PC_REQUIRES " wayland-client")

        if(XKBCOMMON_FOUND)
            list(APPEND FDK_LINK_LIBS    ${XKBCOMMON_LIBRARIES})
            list(APPEND FDK_INCLUDE_DIRS ${XKBCOMMON_INCLUDE_DIRS})
            string(APPEND FDK_PC_REQUIRES " xkbcommon")
        endif()
        if(WAYLAND_CURSOR_FOUND)
            list(APPEND FDK_LINK_LIBS    ${WAYLAND_CURSOR_LIBRARIES})
            list(APPEND FDK_INCLUDE_DIRS ${WAYLAND_CURSOR_INCLUDE_DIRS})
            string(APPEND FDK_PC_REQUIRES " wayland-cursor")
        endif()

        message(STATUS "FDK: Wayland backend enabled")
    else()
        message(WARNING "FDK: Wayland disabled (need wayland-client, wayland-protocols, wayland-scanner)")
    endif()
endif()

# ── OpenGL ────────────────────────────────────────────────────────────────────
if(FDK_ENABLE_OPENGL)
    find_package(OpenGL)
    pkg_check_modules(EGL        egl)
    pkg_check_modules(WAYLAND_EGL wayland-egl)
    if(OpenGL_FOUND)
        list(APPEND FDK_SOURCES      src/render/opengl.c)
        list(APPEND FDK_COMPILE_DEFS FDK_HAVE_OPENGL)
        list(APPEND FDK_LINK_LIBS    OpenGL::GL)
        string(APPEND FDK_PC_LIBS    " -lGL")
        if(EGL_FOUND)
            list(APPEND FDK_LINK_LIBS    ${EGL_LIBRARIES})
            list(APPEND FDK_INCLUDE_DIRS ${EGL_INCLUDE_DIRS})
            string(APPEND FDK_PC_REQUIRES " egl")
        endif()
        if(WAYLAND_EGL_FOUND)
            list(APPEND FDK_LINK_LIBS    ${WAYLAND_EGL_LIBRARIES})
            list(APPEND FDK_INCLUDE_DIRS ${WAYLAND_EGL_INCLUDE_DIRS})
            string(APPEND FDK_PC_REQUIRES " wayland-egl")
        endif()
        message(STATUS "FDK: OpenGL backend enabled")
    else()
        message(WARNING "FDK: OpenGL not found — skipping")
    endif()
endif()

# ── Shared library macro (avoids repeating target setup) ──────────────────────
# We use a helper function so both targets share identical settings.
function(fdk_configure_target target_name)
    if(FDK_EXTRA_TARGETS)
        add_dependencies(${target_name} ${FDK_EXTRA_TARGETS})
    endif()
    target_include_directories(${target_name}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
        PRIVATE ${FDK_INCLUDE_DIRS}
                ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(${target_name} PRIVATE ${FDK_COMPILE_DEFS})
    target_link_libraries(${target_name} PUBLIC ${FDK_LINK_LIBS})
    target_compile_options(${target_name} PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g -O0>
    )
    # Shared libraries need -fPIC; static can use it too with no cost
    set_target_properties(${target_name} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()

# ── Static library ────────────────────────────────────────────────────────────
if(FDK_BUILD_STATIC)
    add_library(fdk_static STATIC ${FDK_SOURCES})
    set_target_properties(fdk_static PROPERTIES OUTPUT_NAME fdk)
    fdk_configure_target(fdk_static)
    # Alias so existing code using target 'fdk' still works
    add_library(fdk::fdk_static ALIAS fdk_static)
endif()

# ── Shared library ────────────────────────────────────────────────────────────
if(FDK_BUILD_SHARED)
    add_library(fdk_shared SHARED ${FDK_SOURCES})
    set_target_properties(fdk_shared PROPERTIES
        OUTPUT_NAME   fdk
        # SO versioning: libfdk.so.0.3.4  →  libfdk.so.0  →  libfdk.so
        VERSION       ${PROJECT_VERSION}
        SOVERSION     ${PROJECT_VERSION_MAJOR}
    )
    fdk_configure_target(fdk_shared)
    add_library(fdk::fdk ALIAS fdk_shared)
endif()

# Convenience alias 'fdk' — prefers shared, falls back to static
if(FDK_BUILD_SHARED)
    add_library(fdk ALIAS fdk_shared)
elseif(FDK_BUILD_STATIC)
    add_library(fdk ALIAS fdk_static)
endif()

# ── pkg-config file ───────────────────────────────────────────────────────────
# Generated from a template so paths are correct after install
set(FDK_PC_PREFIX      "${CMAKE_INSTALL_PREFIX}")
set(FDK_PC_INCLUDEDIR  "${CMAKE_INSTALL_PREFIX}/include")
set(FDK_PC_LIBDIR      "${CMAKE_INSTALL_PREFIX}/lib")
set(FDK_PC_VERSION     "${PROJECT_VERSION}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fdk.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fdk.pc"
    @ONLY
)

# ── CMake package config ──────────────────────────────────────────────────────
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FDKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FDKConfig.cmake"
    INSTALL_DESTINATION lib/cmake/FDK
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FDKConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

# Headers
install(DIRECTORY include/fdk
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Libraries
if(FDK_BUILD_STATIC)
    install(TARGETS fdk_static
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
if(FDK_BUILD_SHARED)
    install(TARGETS fdk_shared
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            NAMELINK_COMPONENT devel   # libfdk.so symlink → devel component
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# pkg-config
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fdk.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# CMake package config
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FDKConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FDK
)

# ── Examples ──────────────────────────────────────────────────────────────────
if(FDK_BUILD_EXAMPLES)
    add_subdirectory(examples/theme-switcher)
    add_subdirectory(examples/systemwide-test)
    add_subdirectory(examples/two-window-demo)
    add_subdirectory(examples/csd-demo)
    add_subdirectory(examples/file-dialog-demo)
    add_subdirectory(examples/dnd-demo)
    add_subdirectory(examples/text-playground)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
option(FDK_BUILD_TESTS "Build headless test binaries" ON)
if(FDK_BUILD_TESTS)
    # enable_testing() must be called here (top level), not only inside
    # tests/CMakeLists.txt — otherwise CMake never writes a subdirs()
    # entry for tests/ into the top-level build/CTestTestfile.cmake, and
    # `ctest` run from build/ (as documented in the README/build
    # instructions) reports "No tests were found!!!" even though the
    # test binary itself builds and runs fine directly.
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Tools ─────────────────────────────────────────────────────────────────────
option(FDK_BUILD_TOOLS "Build companion CLI tools (fdk-theme)" ON)
if(FDK_BUILD_TOOLS)
    add_subdirectory(tools/fdk-theme)
endif()
