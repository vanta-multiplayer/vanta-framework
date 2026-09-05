include(UpdateGitHash)

update_git_version(VANTAMP "${CMAKE_CURRENT_SOURCE_DIR}/shared/version.cpp.in" "${CMAKE_BINARY_DIR}/vantamp_version.cpp")

include_directories(${CMAKE_SOURCE_DIR}/code/framework ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Client and launcher are 32-bit only (GTA: San Andreas is 32-bit)
    add_subdirectory(client)
    add_subdirectory(launcher)
endif()

if(CMAKE_CL_64 OR NOT MSVC)
    # Server is 64-bit only (or cross-platform)
    add_subdirectory(server)
endif()
