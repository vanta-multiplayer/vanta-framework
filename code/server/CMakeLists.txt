set(VANTAMP_SERVER_FILES
    src/main.cpp
    src/core/server.cpp
    src/game/player_manager.cpp
    src/game/vehicle_manager.cpp
    src/scripting/bindings.cpp

    ../shared/player_entity.cpp
    ../shared/vehicle_entity.cpp

    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

add_executable(VantaMPServer ${VANTAMP_SERVER_FILES})
target_include_directories(VantaMPServer PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(VantaMPServer Framework FrameworkServer)

add_custom_command(TARGET VantaMPServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../data/resources"
        "$<TARGET_FILE_DIR:VantaMPServer>/resources"
    COMMENT "Copying VantaMP resources")

# Match FrameworkServer's compile definitions
if(WIN32)
    target_compile_definitions(VantaMPServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
