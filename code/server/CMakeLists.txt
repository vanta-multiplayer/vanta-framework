set(VANTAMP_SERVER_FILES
    src/main.cpp
    src/core/server.cpp
    src/core/net.cpp
    src/eac/eac.cpp
    src/game/player_manager.cpp
    src/game/vehicle_manager.cpp
    src/game/world_manager.cpp
    src/scripting/bindings.cpp
    src/scripting/player.cpp
    src/scripting/vehicle.cpp
    src/scripting/world.cpp

    ../shared/entity/player_entity.cpp
    ../shared/entity/vehicle_entity.cpp
    ../shared/catalog/model_catalog.cpp
    ../shared/vehicle/upgrades.cpp
    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

add_executable(VantaMPServer ${VANTAMP_SERVER_FILES})
target_include_directories(VantaMPServer PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(VantaMPServer Framework FrameworkServer)
target_compile_definitions(VantaMPServer PRIVATE VANTA_SERVER=1)

# Epic Online Services / Easy Anti-Cheat (64-bit server).
set(VANTA_EOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/eos")
target_include_directories(VantaMPServer PRIVATE "${VANTA_EOS_ROOT}/Include")
if(EXISTS "${VANTA_EOS_ROOT}/Lib/Win64/EOSSDK-Win64-Shipping.lib")
    target_compile_definitions(VantaMPServer PRIVATE VANTAMP_HAS_EOS=1)
    target_link_libraries(VantaMPServer "${VANTA_EOS_ROOT}/Lib/Win64/EOSSDK-Win64-Shipping.lib")
    add_custom_command(TARGET VantaMPServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VANTA_EOS_ROOT}/Bin/Win64/EOSSDK-Win64-Shipping.dll"
            "$<TARGET_FILE_DIR:VantaMPServer>"
        COMMENT "Copy EOSSDK-Win64-Shipping.dll next to VantaMPServer")
endif()

add_custom_command(TARGET VantaMPServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../data/resources"
        "$<TARGET_FILE_DIR:VantaMPServer>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../data/catalog"
        "$<TARGET_FILE_DIR:VantaMPServer>/catalog"
    COMMENT "Copying VantaMP resources and model catalog")

# Match FrameworkServer's compile definitions
if(WIN32)
    target_compile_definitions(VantaMPServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
