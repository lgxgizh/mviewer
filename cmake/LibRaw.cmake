# Isolated LibRaw (LGPL-2.1 / CDDL-1.0) for half-size and full demosaic.
# Fetched at configure time so Windows CI (build.ps1 + MSVC + Qt) picks it up
# without a vcpkg toolchain or workflow edit; a verified local archive is
# preferred when available (see MVIEWER_LIBRAW_ARCHIVE below). Optional codecs that need
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

# Offline / flaky-network fallback: a pre-downloaded copy of the SAME tag archive
# is used when present and its SHA256 matches. Lookup order:
#   1. MVIEWER_LIBRAW_ARCHIVE (cache var, or environment variable of that name)
#   2. third_party/LibRaw-<version>.tar.gz in the source tree
#   3. the GitHub tag URL above
# A local file with the wrong hash is ignored with a warning (never used).
set(MVIEWER_LIBRAW_ARCHIVE "$ENV{MVIEWER_LIBRAW_ARCHIVE}" CACHE FILEPATH
    "Local LibRaw ${MVIEWER_LIBRAW_VERSION} tag archive (.tar.gz); empty = third_party/ or download")
set(_libraw_fetch_url "${MVIEWER_LIBRAW_URL}")
foreach(_libraw_candidate IN ITEMS
        "${MVIEWER_LIBRAW_ARCHIVE}"
        "${CMAKE_SOURCE_DIR}/third_party/LibRaw-${MVIEWER_LIBRAW_VERSION}.tar.gz")
    if(_libraw_candidate STREQUAL "" OR NOT EXISTS "${_libraw_candidate}")
        continue()
    endif()
    file(SHA256 "${_libraw_candidate}" _libraw_candidate_sha256)
    if(_libraw_candidate_sha256 STREQUAL MVIEWER_LIBRAW_SHA256)
        file(TO_CMAKE_PATH "${_libraw_candidate}" _libraw_fetch_url)
        message(STATUS "LibRaw: using local archive ${_libraw_fetch_url}")
        break()
    endif()
    message(WARNING "LibRaw: ignoring ${_libraw_candidate} (SHA256 ${_libraw_candidate_sha256} "
                    "does not match the ${MVIEWER_LIBRAW_VERSION} tag archive)")
endforeach()
if(MVIEWER_LIBRAW_ARCHIVE AND NOT EXISTS "${MVIEWER_LIBRAW_ARCHIVE}")
    message(WARNING "LibRaw: MVIEWER_LIBRAW_ARCHIVE='${MVIEWER_LIBRAW_ARCHIVE}' does not exist")
endif()

# Review P2-11: download once into third_party/ (git-ignored, cached by CI)
# and degrade instead of failing when offline. A failed download builds
# without LibRaw (RAW files fall back to their embedded previews) unless
# MVIEWER_REQUIRE_LIBRAW is ON (CI and release set it through the environment,
# so a shipped build can never silently lose RAW demosaic).
if(DEFINED ENV{MVIEWER_REQUIRE_LIBRAW})
    set(_libraw_require_default "$ENV{MVIEWER_REQUIRE_LIBRAW}")
else()
    set(_libraw_require_default OFF)
endif()
option(MVIEWER_REQUIRE_LIBRAW "Fail configure when LibRaw cannot be obtained"
       ${_libraw_require_default})
if(_libraw_fetch_url STREQUAL MVIEWER_LIBRAW_URL)
    set(_libraw_cached "${CMAKE_SOURCE_DIR}/third_party/LibRaw-${MVIEWER_LIBRAW_VERSION}.tar.gz")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(DOWNLOAD "${MVIEWER_LIBRAW_URL}" "${_libraw_cached}.part"
         STATUS _libraw_dl_status TLS_VERIFY ON INACTIVITY_TIMEOUT 60 TIMEOUT 600)
    list(GET _libraw_dl_status 0 _libraw_dl_code)
    set(_libraw_dl_sha256 "")
    if(_libraw_dl_code EQUAL 0)
        file(SHA256 "${_libraw_cached}.part" _libraw_dl_sha256)
    endif()
    if(_libraw_dl_code EQUAL 0 AND _libraw_dl_sha256 STREQUAL MVIEWER_LIBRAW_SHA256)
        file(RENAME "${_libraw_cached}.part" "${_libraw_cached}")
        file(TO_CMAKE_PATH "${_libraw_cached}" _libraw_fetch_url)
        message(STATUS "LibRaw: downloaded and cached ${_libraw_fetch_url}")
    else()
        file(REMOVE "${_libraw_cached}.part")
        list(JOIN _libraw_dl_status " " _libraw_dl_text)
        set(_libraw_reason "download of ${MVIEWER_LIBRAW_URL} failed (${_libraw_dl_text})")
        if(_libraw_dl_code EQUAL 0)
            set(_libraw_reason "downloaded archive SHA256 ${_libraw_dl_sha256} does not match")
        endif()
        if(MVIEWER_REQUIRE_LIBRAW)
            message(FATAL_ERROR "LibRaw: ${_libraw_reason}. MVIEWER_REQUIRE_LIBRAW is ON.")
        endif()
        message(WARNING "LibRaw: ${_libraw_reason}. Building WITHOUT LibRaw: RAW files use "
                        "their embedded previews. Put the ${MVIEWER_LIBRAW_VERSION} tag archive "
                        "at third_party/LibRaw-${MVIEWER_LIBRAW_VERSION}.tar.gz or set "
                        "MVIEWER_LIBRAW_ARCHIVE to build with RAW demosaic.")
        return()
    endif()
endif()

FetchContent_Declare(mviewer_libraw_src
    URL ${_libraw_fetch_url}
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
