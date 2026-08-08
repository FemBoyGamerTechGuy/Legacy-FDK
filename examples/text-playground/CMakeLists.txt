add_executable(fdk_text_playground text_playground.c)
target_link_libraries(fdk_text_playground PRIVATE fdk)
target_include_directories(fdk_text_playground PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
