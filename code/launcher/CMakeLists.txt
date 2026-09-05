set(VANTAMP_LAUNCHER_FILES
    src/main.cpp
    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

add_executable(VantaMPLauncher ${VANTAMP_LAUNCHER_FILES})
target_include_directories(VantaMPLauncher PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(VantaMPLauncher PRIVATE Framework FrameworkLoader VantaMPClient)
