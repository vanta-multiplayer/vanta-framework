set(VANTAMP_CLIENT_FILES
    src/main.cpp

    src/core/application.cpp

    src/core/hooks/mod_init.cpp
    src/core/hooks/render_device.cpp

    src/core/ui/ped_debug_ui.cpp

    src/game/population.cpp

    src/sdk/game.cpp
    src/sdk/game_build.cpp
    src/sdk/patterns.cpp
)

add_library(VantaMPClient SHARED ${VANTAMP_CLIENT_FILES} ${CMAKE_BINARY_DIR}/vantamp_version.cpp)
target_include_directories(VantaMPClient PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(VantaMPClient Framework FrameworkClient)
target_link_options(VantaMPClient PRIVATE /MANIFEST:NO)

# plugin-sdk's bundled safetyhook uses std::expected
set_target_properties(VantaMPClient PROPERTIES CXX_STANDARD 23)

# See vendors/plugin-sdk.cmake: the SDK reaches this one file and no further
set_source_files_properties(src/sdk/game.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${PLUGIN_SDK_SA_INCLUDE_DIRS}"
    COMPILE_DEFINITIONS "${PLUGIN_SDK_SA_DEFINITIONS}")
