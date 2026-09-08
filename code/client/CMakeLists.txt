# THE ONLY FILES plugin-sdk REACHES.
#
# Listed separately because the boundary is enforced here and nowhere else:
# these get the plugin-sdk include directories below, and everything else in the
# project therefore cannot name a game type even by accident. Above the boundary
# the game is player slots and plain numbers - see src/sdk/game.h.
#
# Adding a file here should be a deliberate act. Prefer putting the game call
# behind an existing SDK function instead.
set(VANTAMP_CLIENT_SDK_FILES
    src/sdk/input/input.cpp
    src/sdk/world/streaming.cpp
    src/sdk/ped/local_ped.cpp
    src/sdk/ped/remote_ped.cpp
    src/sdk/ped/weapon.cpp
    src/sdk/ped/shot.cpp
    src/sdk/vehicle/vehicle.cpp
    src/sdk/vehicle/occupancy.cpp
)

set(VANTAMP_CLIENT_FILES
    src/main.cpp

    # The client itself: framework integration, config, hooks and the interface.
    src/core/application.cpp
    src/core/connection.cpp
    src/core/net.cpp
    src/core/client_config.cpp
    src/core/diagnostics/breadcrumbs.cpp
    src/core/diagnostics/crash_report.cpp
    src/core/hooks/mod_init.cpp
    src/core/hooks/render_device.cpp
    src/core/nui/nui.cpp
    src/core/nui/nui_state.cpp
    src/core/nui/nui_settings.cpp
    src/core/nui/nui_console.cpp
    src/core/nui/nui_launch.cpp
    src/core/nui/nui_netgraph.cpp
    src/core/nui/nui_handlers.cpp
    src/core/nui/embedded_ui.cpp
    src/core/nui/page_events.cpp
    src/core/ui/ped_debug_ui.cpp
    src/core/ui/window_input.cpp
    src/core/voice_io.cpp

    # Changes to the game itself, applied once at startup.
    src/game/startup.cpp
    src/game/patches/audio.cpp
    src/game/patches/audio_output.cpp
    src/game/patches/boot.cpp
    src/game/patches/display.cpp
    src/game/patches/gameplay.cpp
    src/game/patches/input.cpp
    src/game/patches/population.cpp
    src/game/patches/script.cpp
    src/game/patches/vehicle_lights.cpp

    # Things in the world, and the slots they live in.
    src/game/camera.cpp
    src/game/entities/local_player.cpp
    src/game/entities/player_slots.cpp
    src/game/entities/remote_players.cpp

    # Keeping those things in step with the server.
    src/game/sync/player_sync.cpp
    src/game/sync/shots.cpp
    src/game/sync/death.cpp
    src/game/sync/vehicle_sync.cpp
    src/game/sync/world_sync.cpp

    # Usermode EOS Connect (Steam-free) + optional EAC.
    src/eos/manager.cpp
    src/eac/eac.cpp
    src/eac/host_modules.cpp
    ../shared/eos/runtime_config.cpp

    # Resource scripting (readonly Player / Vehicle handles).
    src/scripting/bindings.cpp
    src/scripting/player.cpp
    src/scripting/vehicle.cpp

    src/game/ui/nametags.cpp

    # The game's memory, wrapped. These need no plugin-sdk of their own: an
    # address is a number and a slot is an integer.
    src/sdk/world/camera.cpp
    src/sdk/world/draw.cpp
    src/sdk/world/environment.cpp
    src/sdk/game_build.cpp
    src/sdk/patterns.cpp
    src/sdk/ped/aim.cpp

    ${VANTAMP_CLIENT_SDK_FILES}

    ../shared/entity/player_entity.cpp
    ../shared/entity/vehicle_entity.cpp

    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

# The interface is compiled into the DLL. ui/dist if someone just built it,
# otherwise the last local data/ui/index.html. Never copied next to the
# binary — CEF loads http://vanta/ui.html from the byte array.
set(VANTA_UI_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_content.hpp")
set(VANTA_UI_EMBED "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/embed_ui.py")
set(VANTA_UI_FALLBACK "${CMAKE_CURRENT_SOURCE_DIR}/../../data/ui/index.html")

add_custom_command(
    OUTPUT "${VANTA_UI_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND python "${VANTA_UI_EMBED}" -o "${VANTA_UI_HEADER}"
    DEPENDS "${VANTA_UI_EMBED}" "${VANTA_UI_FALLBACK}"
    COMMENT "Embedding the VantaMP interface"
    VERBATIM)

list(APPEND VANTAMP_CLIENT_FILES "${VANTA_UI_HEADER}")

add_library(VantaMPClient SHARED ${VANTAMP_CLIENT_FILES})
target_include_directories(VantaMPClient PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../ ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(VantaMPClient Framework FrameworkClient)
target_link_options(VantaMPClient PRIVATE /MANIFEST:NO)
target_compile_definitions(VantaMPClient PRIVATE VANTA_CLIENT=1)

# GTA:SA IAT-imports EAX.DLL (ordinal 6). A Vanta-built forwarder to dsound.dll
# lives next to the launcher so LoadLibrary never pulls eax.dll from the GTA
# install (EAC Untrusted). Not a copy of the retail DLL.
add_library(VantaEaxStub SHARED host_stubs/eax/eax.cpp host_stubs/eax/eax.def)
set_target_properties(VantaEaxStub PROPERTIES
    OUTPUT_NAME eax
    PREFIX ""
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:VantaMPClient>
    RUNTIME_OUTPUT_DIRECTORY_DEBUG $<TARGET_FILE_DIR:VantaMPClient>
    RUNTIME_OUTPUT_DIRECTORY_RELEASE $<TARGET_FILE_DIR:VantaMPClient>
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO $<TARGET_FILE_DIR:VantaMPClient>
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL $<TARGET_FILE_DIR:VantaMPClient>)
target_link_libraries(VantaEaxStub PRIVATE dsound ole32)
target_compile_definitions(VantaEaxStub PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(VantaMPClient VantaEaxStub)

# ogg / vorbis / vorbisFile — Xiph sources, GTA IAT names (see host_stubs/vorbis).
add_subdirectory(host_stubs/vorbis)

# Epic Online Services / Easy Anti-Cheat (32-bit client needs Win32 EOSSDK).
# Connect uses Device ID - Steam is not required for EOS identity.
set(VANTA_EOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/eos")
target_include_directories(VantaMPClient PRIVATE "${VANTA_EOS_ROOT}/Include")
if(EXISTS "${VANTA_EOS_ROOT}/Lib/Win32/EOSSDK-Win32-Shipping.lib")
    target_compile_definitions(VantaMPClient PRIVATE VANTAMP_HAS_EOS=1)
    target_link_libraries(VantaMPClient "${VANTA_EOS_ROOT}/Lib/Win32/EOSSDK-Win32-Shipping.lib")
    add_custom_command(TARGET VantaMPClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VANTA_EOS_ROOT}/Bin/Win32/EOSSDK-Win32-Shipping.dll"
            "$<TARGET_FILE_DIR:VantaMPClient>"
        COMMENT "Copy EOSSDK-Win32-Shipping.dll next to VantaMPClient")
endif()

# plugin-sdk's bundled safetyhook uses std::expected
set_target_properties(VantaMPClient PROPERTIES CXX_STANDARD 23)

# See vendors/plugin-sdk.cmake: the SDK reaches these files and no further.
set_source_files_properties(${VANTAMP_CLIENT_SDK_FILES} PROPERTIES
    INCLUDE_DIRECTORIES "${PLUGIN_SDK_SA_INCLUDE_DIRS}"
    COMPILE_DEFINITIONS "${PLUGIN_SDK_SA_DEFINITIONS}")

# Rebuild the Solid page and refresh data/ui/index.html so the embed above
# picks it up on the next compile. Not part of the default client build —
# node is not required to link a client that already has a fallback HTML.
add_custom_target(VantaMPUi
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/build_ui.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    COMMENT "Building and embedding the VantaMP interface")
