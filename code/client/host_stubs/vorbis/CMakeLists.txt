# Open-source ogg/vorbis shared libs named like GTA:SA's IAT expects.
# gta_sa.exe imports vorbisfile.dll; that DLL loads ogg.dll + vorbis.dll from
# the same directory. Built from Xiph sources — not retail GTA copies.

include(FetchContent)

set(_vanta_prev_build_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS ON)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    vanta_libogg
    URL https://github.com/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.tar.gz
    URL_HASH SHA256=0eb4b4b9420a0f51db142ba3f9c64b333f826532dc0f48c6410ae51f4799b664
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    vanta_libvorbis
    URL https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# libogg 1.3.5 still declares cmake_minimum_required(2.8.12); CMake 4 needs a floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_MakeAvailable(vanta_libogg)

# Prefer our FindOgg.cmake so libvorbis links the FetchContent `ogg` target.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
FetchContent_MakeAvailable(vanta_libvorbis)

set(BUILD_SHARED_LIBS "${_vanta_prev_build_shared}")

function(vanta_host_dll_output target outname)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${outname}"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:VantaMPClient>"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "$<TARGET_FILE_DIR:VantaMPClient>"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "$<TARGET_FILE_DIR:VantaMPClient>"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "$<TARGET_FILE_DIR:VantaMPClient>"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "$<TARGET_FILE_DIR:VantaMPClient>")
endfunction()

vanta_host_dll_output(ogg ogg)
vanta_host_dll_output(vorbis vorbis)
vanta_host_dll_output(vorbisfile vorbisFile)

# vorbisenc is unused by GTA; keep it out of the runtime dir.
set_target_properties(vorbisenc PROPERTIES EXCLUDE_FROM_ALL TRUE)

add_dependencies(VantaMPClient ogg vorbis vorbisfile)
