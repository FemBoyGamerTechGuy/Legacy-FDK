add_executable(fdk_dnd_demo dnd_demo.c)
target_link_libraries(fdk_dnd_demo PRIVATE fdk)
target_include_directories(fdk_dnd_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
