set(VANTAMP_CLIENT_FILES
    src/main.cpp

    # The client itself: framework integration, config, hooks and the interface.
    src/core/application.cpp
    src/core/client_config.cpp
    src/core/hooks/mod_init.cpp
    src/core/hooks/render_device.cpp
    src/core/ui/nui.cpp
    src/core/ui/ped_debug_ui.cpp
    src/core/ui/window_input.cpp

    # Changes to the game itself, applied once at startup.
    src/game/patches/boot.cpp
    src/game/patches/display.cpp
    src/game/patches/gameplay.cpp
    src/game/patches/input.cpp
    src/game/patches/population.cpp
    src/game/patches/script.cpp

    # Things in the world, and the slots they live in.
    src/game/camera.cpp
    src/game/entities/local_player.cpp
    src/game/entities/player_slots.cpp
    src/game/entities/remote_players.cpp

    # Keeping those things in step with the server.
    src/game/sync/player_sync.cpp

    # plugin-sdk lives behind here and nowhere else.
    src/sdk/game.cpp
    src/sdk/game_build.cpp
    src/sdk/patterns.cpp

    ../shared/player_entity.cpp

    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

add_library(VantaMPClient SHARED ${VANTAMP_CLIENT_FILES})
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
