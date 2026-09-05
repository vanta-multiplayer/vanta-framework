set(VANTAMP_CLIENT_FILES
    src/main.cpp

    src/core/application.cpp
    src/core/client_config.cpp

    ../shared/player_entity.cpp

    src/core/hooks/mod_init.cpp
    src/core/hooks/render_device.cpp

    src/core/ui/nui.cpp
    src/core/ui/window_input.cpp
    src/core/ui/ped_debug_ui.cpp

    src/game/boot.cpp
    src/game/display.cpp
    src/game/gameplay.cpp
    src/game/input.cpp
    src/game/player_slots.cpp
    src/game/player_sync.cpp
    src/game/remote_players.cpp
    src/game/population.cpp
    src/game/world.cpp

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

# The interface, beside the client where it looks for it.
#
# One self-contained HTML with every script, style and font inlined - CEF loads
# it over file://, where there is no origin and nothing to resolve.
add_custom_command(TARGET VantaMPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../data/ui/index.html"
        "$<TARGET_FILE_DIR:VantaMPClient>/ui/index.html"
    COMMENT "Copying the VantaMP interface")
