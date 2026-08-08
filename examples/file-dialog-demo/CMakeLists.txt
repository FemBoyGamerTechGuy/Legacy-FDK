add_executable(fdk_file_dialog_demo file_dialog_demo.c)
target_link_libraries(fdk_file_dialog_demo PRIVATE fdk)
target_include_directories(fdk_file_dialog_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
