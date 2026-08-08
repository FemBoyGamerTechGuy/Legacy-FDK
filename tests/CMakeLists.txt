# Headless tests — no window, no compositor required.
# Run with: ctest --output-on-failure   (or just execute the binary directly)

add_executable(test_theme test_theme.c)
target_link_libraries(test_theme PRIVATE fdk)
target_include_directories(test_theme PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# test_theme looks for "themes/<name>.fdktheme" relative to its CWD,
# falling back to "../themes/<name>.fdktheme". Running it from the build
# directory (where this target lives) makes the fallback path resolve to
# ${CMAKE_SOURCE_DIR}/themes via the source tree symlink/copy below.
#
# Simplest: just copy the bundled themes next to the test binary so the
# primary "themes/<name>.fdktheme" lookup succeeds with zero path guessing.
add_custom_command(TARGET test_theme POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:test_theme>/themes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/themes
            $<TARGET_FILE_DIR:test_theme>/themes
    COMMENT "Copying bundled .fdktheme files next to test_theme"
)

add_test(NAME theme_parser COMMAND test_theme)

# Regression test for the per-UI theme watch list (see
# src/core/theme.c g_watches / test_internal.h). Needs src/ on the
# include path — unlike test_theme, it reaches into a non-public
# internal header (core/test_internal.h) for watch-list introspection
# that has no reason to exist in the public API.
add_executable(test_watch_multiwindow test_watch_multiwindow.c)
target_link_libraries(test_watch_multiwindow PRIVATE fdk)
target_include_directories(test_watch_multiwindow PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
add_test(NAME watch_multiwindow COMMAND test_watch_multiwindow)

# HarfBuzz + FriBidi integration test (v0.2). Verifies Latin ligature
# shaping, Arabic/CJK don't-crash, BiDi reordering, font lifecycle.
# Uses only the public fdk_measure_text() API so it can run anywhere
# libfdk links — no window, no compositor. Skips cleanly if no test
# font is found in /usr/share/fonts.
add_executable(test_shape test_shape.c)
target_link_libraries(test_shape PRIVATE fdk)
target_include_directories(test_shape PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
add_test(NAME shape COMMAND test_shape)

# HiDPI scale factor API test (v0.2). Verifies fdk_window_get_scale,
# fdk_window_get_dpi, fdk_get_features, fdk_has_feature. Doesn't need
# a display server — just exercises the public API surface.
add_executable(test_hidpi test_hidpi.c)
target_link_libraries(test_hidpi PRIVATE fdk)
target_include_directories(test_hidpi PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
add_test(NAME hidpi COMMAND test_hidpi)

# New widgets (v0.2): Switch, LevelBar, SearchEntry. Verifies
# constructors, getters, setters, callbacks, NULL safety, wrong-type
# safety. No display server needed — just exercises the public API.
add_executable(test_widgets test_widgets.c)
target_link_libraries(test_widgets PRIVATE fdk)
target_include_directories(test_widgets PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
add_test(NAME widgets COMMAND test_widgets)

# New widgets test 2 (v0.2 part 2): Expander, StatusBar, Grid,
# Calendar, Popover. Verifies constructors, getters, setters,
# state transitions, NULL safety, date clamping.
add_executable(test_widgets2 test_widgets2.c)
target_link_libraries(test_widgets2 PRIVATE fdk)
target_include_directories(test_widgets2 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
add_test(NAME widgets2 COMMAND test_widgets2)

# IME (Input Method Editor) commit event handling test (v0.2).
# Verifies the FDK_EVENT_IME_COMMIT public API surface, UTF-8 multi-
# byte commit handling, and widget text APIs that the IME handler
# uses. No display server needed — doesn't actually invoke a real
# IME (would need IBus/Fcitx running).
add_executable(test_ime test_ime.c)
target_link_libraries(test_ime PRIVATE fdk)
target_include_directories(test_ime PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
add_test(NAME ime COMMAND test_ime)

# In-tree UAX #9 BiDi implementation test. Verifies the from-scratch
# Unicode Bidirectional Algorithm in src/render/bidi.c (no FriBidi).
# Tests paragraph level detection, pure LTR/RTL, mixed text, weak
# types, explicit embedding (RLE/LRE), whitespace handling (L1),
# and the visual-to-logical index map.
add_executable(test_bidi test_bidi.c)
target_link_libraries(test_bidi PRIVATE fdk)
target_include_directories(test_bidi PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
add_test(NAME bidi COMMAND test_bidi)

# Xft.dpi parser test — needs an X display (Xvfb in build env).
# Verifies FDK's X11 backend parser correctly extracts Xft.dpi from
# the RESOURCE_MANAGER property for various values and edge cases.
# Not registered as a ctest (needs X display); run manually with
# DISPLAY=:99 after starting Xvfb.
add_executable(test_dpi_parse test_dpi_parse.c)
target_link_libraries(test_dpi_parse PRIVATE ${X11_LIBRARIES})
target_include_directories(test_dpi_parse PRIVATE
    ${X11_INCLUDE_DIR}
)

# Runtime HiDPI test — opens a real window via Xvfb in the build
# environment and verifies fdk_window_get_scale() reflects GDK_SCALE
# and Xft.dpi correctly. Not registered as a ctest because it needs
# a running X server (build environment provides Xvfb).
add_executable(hidpi_runtime hidpi_runtime.c)
target_link_libraries(hidpi_runtime PRIVATE fdk)
target_include_directories(hidpi_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# CLI smoke test for fdk-theme — exercises set/list/show/unset and error
# cases against a throwaway $HOME. The test is a bash script; it locates
# the built fdk-theme binary by walking up from its own source dir. We
# pass FDK_THEME_BIN explicitly so ctest's invocation finds the binary
# even when run from a different CWD.
if(FDK_BUILD_TOOLS)
    add_test(NAME fdk_theme_cli
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_fdk_theme_cli.sh
    )
    set_tests_properties(fdk_theme_cli PROPERTIES
        ENVIRONMENT "FDK_THEME_BIN=$<TARGET_FILE:fdk-theme>"
        # The script needs the bundled themes/ dir to copy a fixture.
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
