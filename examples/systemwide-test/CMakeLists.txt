add_executable(fdk_systemwide_test systemwide_test.c)
target_link_libraries(fdk_systemwide_test PRIVATE fdk)
target_include_directories(fdk_systemwide_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
