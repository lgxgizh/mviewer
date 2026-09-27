# Isolated LibRaw (LGPL-2.1 / CDDL-1.0) for half-size and full demosaic.
# Fetched at configure time so Windows CI (build.ps1 + MSVC + Qt) picks it up
# without a vcpkg toolchain or workflow edit. Optional codecs that need
# libjpeg/lcms/jasper stay off; LibRaw's internal decoders cover CR2/NEF/ARW
# half-size. Sources compile at /W0 (-w) so their warnings cannot trip the
# CI "warning Cxxxx" gate.

option(MVIEWER_WITH_LIBRAW "Fetch and link LibRaw for RAW demosaic" ON)

set(MVIEWER_HAS_LIBRAW OFF)

if(NOT MVIEWER_WITH_LIBRAW)
    return()
endif()

include(FetchContent)
# LibRaw has no CMakeLists of its own; Populate (not MakeAvailable) is required.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(MVIEWER_LIBRAW_VERSION "0.21.4")
set(MVIEWER_LIBRAW_URL
    "https://github.com/LibRaw/LibRaw/archive/refs/tags/${MVIEWER_LIBRAW_VERSION}.tar.gz")

# SHA256 of the 0.21.4 tag archive (LibRaw LLC, LGPL-2.1 OR CDDL-1.0).
set(MVIEWER_LIBRAW_SHA256
    "8baeb5253c746441fadad62e9c5c43ff4e414e41b0c45d6dcabccb542b2dff4b")

FetchContent_Declare(mviewer_libraw_src
    URL ${MVIEWER_LIBRAW_URL}
    URL_HASH SHA256=${MVIEWER_LIBRAW_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(mviewer_libraw_src)
if(NOT mviewer_libraw_src_POPULATED)
    FetchContent_Populate(mviewer_libraw_src)
endif()

set(_libraw_root "${mviewer_libraw_src_SOURCE_DIR}")
if(NOT EXISTS "${_libraw_root}/libraw/libraw.h")
    message(FATAL_ERROR
        "LibRaw fetch did not produce libraw/libraw.h under ${_libraw_root}. "
        "MVIEWER_WITH_LIBRAW=ON requires the 0.21.4 tag archive.")
endif()

file(GLOB_RECURSE _libraw_srcs CONFIGURE_DEPENDS "${_libraw_root}/src/*.cpp")
list(FILTER _libraw_srcs EXCLUDE REGEX "_ph\\.cpp$")
if(NOT _libraw_srcs)
    message(FATAL_ERROR "LibRaw src/*.cpp not found under ${_libraw_root}")
endif()

set(_libraw_config_dir "${CMAKE_BINARY_DIR}/generated/libraw")
file(MAKE_DIRECTORY "${_libraw_config_dir}/libraw")
# All optional external codecs off (no libjpeg/zlib/lcms/jasper on the CI image).
file(WRITE "${_libraw_config_dir}/libraw/libraw_config.h"
"#ifndef __LIBRAW_CONFIG_H
#define __LIBRAW_CONFIG_H
/* MViewer: internal LibRaw decoders only. No external jpeg/zlib/lcms/jasper. */
#endif
")

add_library(mviewer_libraw STATIC ${_libraw_srcs})
set_target_properties(mviewer_libraw PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mviewer_libraw
    PUBLIC "${_libraw_root}"
    PRIVATE "${_libraw_config_dir}")
# Do not define USE_LCMS / USE_JPEG / USE_ZLIB / USE_JASPER. libraw_types.h
# then sets NO_LCMS itself, and dcraw_process skips apply_profile.
target_compile_definitions(mviewer_libraw PUBLIC LIBRAW_NODLL)
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(mviewer_libraw PUBLIC Threads::Threads)
endif()
if(NOT WIN32)
    target_link_libraries(mviewer_libraw PUBLIC m)
endif()
if(WIN32)
    target_compile_definitions(mviewer_libraw PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mviewer_libraw PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(mviewer_libraw PRIVATE /W0 /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(mviewer_libraw PRIVATE -w)
endif()
target_compile_features(mviewer_libraw PRIVATE cxx_std_17)

set(MVIEWER_HAS_LIBRAW ON)
message(STATUS "LibRaw ${MVIEWER_LIBRAW_VERSION} (LGPL-2.1 / CDDL-1.0) enabled")
