add_executable(fdk_two_window_demo two_window_demo.c)
target_link_libraries(fdk_two_window_demo PRIVATE fdk)
target_include_directories(fdk_two_window_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
