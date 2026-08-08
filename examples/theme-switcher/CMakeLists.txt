add_executable(fdk_theme_switcher theme_switcher.c)
target_link_libraries(fdk_theme_switcher PRIVATE fdk)
target_include_directories(fdk_theme_switcher PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# The demo looks for "themes/<name>.fdktheme" relative to its CWD by
# default. Copy the bundled themes next to the built binary so running
# it directly from the build directory works with zero arguments.
add_custom_command(TARGET fdk_theme_switcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:fdk_theme_switcher>/themes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/themes
            $<TARGET_FILE_DIR:fdk_theme_switcher>/themes
    COMMENT "Copying bundled .fdktheme files next to fdk_theme_switcher"
)
