add_executable(fdk_csd_demo csd_demo.c)
target_link_libraries(fdk_csd_demo PRIVATE fdk)
target_include_directories(fdk_csd_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# Copy the icon PNG next to the binary so fdk_window_set_icon_from_file
# can find it at runtime via relative path.
add_custom_command(TARGET fdk_csd_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/fdk-icon-256.png
            $<TARGET_FILE_DIR:fdk_csd_demo>/fdk-icon-256.png
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/fdk-icon-48.png
            $<TARGET_FILE_DIR:fdk_csd_demo>/fdk-icon-48.png
    COMMENT "Copying FDK icon PNGs next to fdk_csd_demo"
)
