include(UpdateGitHash)

# Branch and channel stamped into Version::*. CI can override:
#   -DVANTAMP_GIT_BRANCH=develop -DVANTAMP_ENVIRONMENT=test
#   or GITHUB_REF_NAME / GITHUB_HEAD_REF / VANTAMP_ENVIRONMENT.
set(VANTAMP_GIT_BRANCH "" CACHE STRING "GitHub branch this binary is released under")
set(VANTAMP_ENVIRONMENT "" CACHE STRING "Release channel: test or release")

if(VANTAMP_GIT_BRANCH STREQUAL "")
    if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
        set(VANTAMP_GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
    elseif(DEFINED ENV{GITHUB_REF_NAME} AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
        set(VANTAMP_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    elseif(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE _vanta_git_branch_res
            OUTPUT_VARIABLE VANTAMP_GIT_BRANCH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _vanta_git_branch_res EQUAL 0)
            set(VANTAMP_GIT_BRANCH "")
        endif()
    endif()
endif()

if(VANTAMP_GIT_BRANCH STREQUAL "" OR VANTAMP_GIT_BRANCH STREQUAL "HEAD")
    set(VANTAMP_GIT_BRANCH "unknown")
endif()

string(REPLACE "\\" "" VANTAMP_GIT_BRANCH "${VANTAMP_GIT_BRANCH}")
string(REPLACE "\"" "" VANTAMP_GIT_BRANCH "${VANTAMP_GIT_BRANCH}")

if(VANTAMP_ENVIRONMENT STREQUAL "")
    if(DEFINED ENV{VANTAMP_ENVIRONMENT} AND NOT "$ENV{VANTAMP_ENVIRONMENT}" STREQUAL "")
        set(VANTAMP_ENVIRONMENT "$ENV{VANTAMP_ENVIRONMENT}")
    elseif(VANTAMP_GIT_BRANCH STREQUAL "release" OR VANTAMP_GIT_BRANCH MATCHES "^release/")
        set(VANTAMP_ENVIRONMENT "release")
    else()
        set(VANTAMP_ENVIRONMENT "test")
    endif()
endif()

string(TOLOWER "${VANTAMP_ENVIRONMENT}" VANTAMP_ENVIRONMENT)

if(EXISTS "${PROJECT_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/.git/HEAD")
endif()

update_git_version(VANTAMP "${CMAKE_CURRENT_SOURCE_DIR}/shared/version.cpp.in" "${CMAKE_BINARY_DIR}/vantamp_version.cpp")
message(STATUS "VantaMP channel: ${VANTAMP_ENVIRONMENT} (branch ${VANTAMP_GIT_BRANCH})")

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
