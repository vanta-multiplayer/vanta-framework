# Satisfies libvorbis's find_package(Ogg) when ogg was built via FetchContent
# in the same CMake run (target name: ogg).

if(NOT TARGET ogg)
    message(FATAL_ERROR "FindOgg: expected FetchContent target 'ogg' before libvorbis")
endif()

if(NOT TARGET Ogg::ogg)
    add_library(Ogg::ogg ALIAS ogg)
endif()

set(OGG_FOUND TRUE)
set(OGG_LIBRARY ogg)
set(OGG_LIBRARIES Ogg::ogg)
get_target_property(_vanta_ogg_incs ogg INTERFACE_INCLUDE_DIRECTORIES)
set(OGG_INCLUDE_DIR "${_vanta_ogg_incs}")
set(OGG_INCLUDE_DIRS "${_vanta_ogg_incs}")
