set(VANTAMP_SERVER_FILES
    src/main.cpp
    src/core/server.cpp

    ../shared/player_entity.cpp

    ${CMAKE_BINARY_DIR}/vantamp_version.cpp
)

add_executable(VantaMPServer ${VANTAMP_SERVER_FILES})
target_include_directories(VantaMPServer PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(VantaMPServer Framework FrameworkServer)

# Match FrameworkServer's compile definitions
if(WIN32)
    target_compile_definitions(VantaMPServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
