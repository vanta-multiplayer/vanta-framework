# plugin-sdk, used header-only for its San Andreas structure layouts. Its addresses are 1.0 US and
# bind to no RGL build, so ours come from signature scans instead (code/client/src/sdk/patterns).
#
# Never applied to a whole target: the SDK ships DirectX SDK headers that lose to the Windows SDK's
# dxgi, and needs RW, which collides with a function in the framework's jitasm.h. Per file only:
#
#   set_source_files_properties(src/sdk/game.cpp PROPERTIES
#       INCLUDE_DIRECTORIES "${PLUGIN_SDK_SA_INCLUDE_DIRS}"
#       COMPILE_DEFINITIONS "${PLUGIN_SDK_SA_DEFINITIONS}")

set(PLUGIN_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/plugin-sdk")

if(NOT EXISTS "${PLUGIN_SDK_ROOT}/plugin_sa/plugin_sa.h")
    message(FATAL_ERROR "plugin-sdk is missing. Run: git submodule update --init vendors/plugin-sdk")
endif()

set(PLUGIN_SDK_SA_INCLUDE_DIRS
    "${PLUGIN_SDK_ROOT}/shared"
    "${PLUGIN_SDK_ROOT}/shared/game"
    "${PLUGIN_SDK_ROOT}/shared/rwd3d9"
    "${PLUGIN_SDK_ROOT}/shared/extensions"
    "${PLUGIN_SDK_ROOT}/plugin_sa"
    "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa"
    "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/enums"
    "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/meta"
    "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/rw"
    "${PLUGIN_SDK_ROOT}/injector"
    "${PLUGIN_SDK_ROOT}/hooking"
    "${PLUGIN_SDK_ROOT}/stb"
)

set(PLUGIN_SDK_SA_DEFINITIONS "GTASA;RW;PLUGIN_SGV_10US")
