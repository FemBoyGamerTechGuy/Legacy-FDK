add_executable(fdk-theme fdk-theme.c)
target_link_libraries(fdk-theme PRIVATE fdk)
target_include_directories(fdk-theme PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

install(TARGETS fdk-theme RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
