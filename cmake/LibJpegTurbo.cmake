# Isolated libjpeg-turbo (IJG / BSD-3 style) for the JPEG decode fast path.
#
# Fetched at configure time, exactly like cmake/LibRaw.cmake, so Windows CI
# (build.ps1 + MSVC + Qt) and the Linux jobs both pick it up without a vcpkg
# toolchain or a workflow edit. A verified local archive is preferred when
# available (MVIEWER_TURBOJPEG_ARCHIVE below).
#
# Why: Qt's JPEG plugin decodes into a QImage (RGB32) and the pipeline then
# copies that into ImageData (RGB24) - two full-image passes and the slower
# IJG IDCT. libjpeg-turbo writes RGB24 directly into the destination buffer
# with a SIMD IDCT, and can produce 1/2, 1/4 and 1/8 scale output natively, so
# the LOD path needs no second rescale either.
#
# Static only: a shared build would put a turbojpeg.dll next to every shipped
# exe and installer, which the packaging manifest would then have to carry.

option(MVIEWER_WITH_TURBOJPEG "Fetch and link libjpeg-turbo for the JPEG fast path" ON)
# -DMVIEWER_USE_TURBOJPEG=OFF builds without it; every call site falls back to
# Qt's decoder, so the product still works (this is the escape hatch if a
# platform's toolchain cannot build it).
if(DEFINED ENV{MVIEWER_USE_TURBOJPEG})
    set(_turbojpeg_default "$ENV{MVIEWER_USE_TURBOJPEG}")
else()
    set(_turbojpeg_default ON)
endif()
option(MVIEWER_USE_TURBOJPEG "Decode JPEG through libjpeg-turbo when available" ${_turbojpeg_default})

set(MVIEWER_HAS_TURBOJPEG OFF)

if(NOT MVIEWER_WITH_TURBOJPEG OR NOT MVIEWER_USE_TURBOJPEG)
    message(STATUS "libjpeg-turbo: disabled (MVIEWER_WITH_TURBOJPEG / MVIEWER_USE_TURBOJPEG)")
    return()
endif()

include(FetchContent)

set(MVIEWER_TURBOJPEG_VERSION "3.1.0")
set(MVIEWER_TURBOJPEG_URL
    "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${MVIEWER_TURBOJPEG_VERSION}.tar.gz")
set(MVIEWER_TURBOJPEG_SHA256
    "35fec2e1ddfb05ecf6d93e50bc57c1e54bc81c16d611ddf6eff73fff266d8285")

# Offline / flaky-network fallback, same lookup order as LibRaw.cmake:
#   1. MVIEWER_TURBOJPEG_ARCHIVE (cache var, or environment variable)
#   2. third_party/libjpeg-turbo-<version>.tar.gz in the source tree
#   3. the GitHub tag URL above
# A local file with the wrong hash is ignored with a warning (never used).
set(MVIEWER_TURBOJPEG_ARCHIVE "$ENV{MVIEWER_TURBOJPEG_ARCHIVE}" CACHE FILEPATH
    "Local libjpeg-turbo ${MVIEWER_TURBOJPEG_VERSION} tag archive (.tar.gz); empty = third_party/ or download")
set(_turbojpeg_fetch_url "${MVIEWER_TURBOJPEG_URL}")
foreach(_candidate IN ITEMS
        "${MVIEWER_TURBOJPEG_ARCHIVE}"
        "${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo-${MVIEWER_TURBOJPEG_VERSION}.tar.gz")
    if(_candidate STREQUAL "" OR NOT EXISTS "${_candidate}")
        continue()
    endif()
    file(SHA256 "${_candidate}" _candidate_sha256)
    if(_candidate_sha256 STREQUAL MVIEWER_TURBOJPEG_SHA256)
        file(TO_CMAKE_PATH "${_candidate}" _turbojpeg_fetch_url)
        message(STATUS "libjpeg-turbo: using local archive ${_turbojpeg_fetch_url}")
        break()
    endif()
    message(WARNING "libjpeg-turbo: ignoring ${_candidate} (SHA256 ${_candidate_sha256} "
                    "does not match the ${MVIEWER_TURBOJPEG_VERSION} tag archive)")
endforeach()

if(_turbojpeg_fetch_url STREQUAL MVIEWER_TURBOJPEG_URL)
    set(_turbojpeg_cached
        "${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo-${MVIEWER_TURBOJPEG_VERSION}.tar.gz")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(DOWNLOAD "${MVIEWER_TURBOJPEG_URL}" "${_turbojpeg_cached}.part"
         STATUS _dl_status TLS_VERIFY ON INACTIVITY_TIMEOUT 60 TIMEOUT 600)
    list(GET _dl_status 0 _dl_code)
    set(_dl_sha256 "")
    if(_dl_code EQUAL 0)
        file(SHA256 "${_turbojpeg_cached}.part" _dl_sha256)
    endif()
    if(_dl_code EQUAL 0 AND _dl_sha256 STREQUAL MVIEWER_TURBOJPEG_SHA256)
        file(RENAME "${_turbojpeg_cached}.part" "${_turbojpeg_cached}")
        file(TO_CMAKE_PATH "${_turbojpeg_cached}" _turbojpeg_fetch_url)
        message(STATUS "libjpeg-turbo: downloaded and cached ${_turbojpeg_fetch_url}")
    else()
        file(REMOVE "${_turbojpeg_cached}.part")
        list(JOIN _dl_status " " _dl_text)
        message(WARNING "libjpeg-turbo: could not obtain the ${MVIEWER_TURBOJPEG_VERSION} archive "
                        "(${_dl_text}). Building WITHOUT the JPEG fast path; Qt's decoder is used.")
        return()
    endif()
endif()

FetchContent_Declare(mviewer_turbojpeg_src
    URL ${_turbojpeg_fetch_url}
    URL_HASH SHA256=${MVIEWER_TURBOJPEG_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(mviewer_turbojpeg_src)
if(NOT mviewer_turbojpeg_src_POPULATED)
    FetchContent_Populate(mviewer_turbojpeg_src)
endif()

set(_turbojpeg_root "${mviewer_turbojpeg_src_SOURCE_DIR}")
if(NOT EXISTS "${_turbojpeg_root}/CMakeLists.txt")
    message(WARNING "libjpeg-turbo: no CMakeLists.txt under ${_turbojpeg_root}; "
                    "building WITHOUT the JPEG fast path.")
    return()
endif()

# libjpeg-turbo's CMake explicitly refuses to be embedded in another build
# ("cannot be integrated into another build"), so it is configured and built
# as its own project and installed into the build tree.
include(ExternalProject)

set(_turbojpeg_prefix "${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo")
set(_turbojpeg_cmake_args
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${_turbojpeg_prefix}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_SIMD=ON
    -DREQUIRE_SIMD=OFF
    -DWITH_TURBOJPEG=ON
    -DWITH_TESTS=OFF
    -DWITH_TOOLS=OFF
    -DWITH_JPEG8=OFF)
if(MSVC)
    # Match the parent build's runtime library; libjpeg-turbo defaults to the
    # static CRT and mixing the two in one binary is a hard error.
    set(_turbojpeg_cmake_args ${_turbojpeg_cmake_args}
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<CONFIG:Release>:MultiThreadedDLL>$<$<CONFIG:RelWithDebInfo>:MultiThreadedDLL>")
endif()

ExternalProject_Add(mviewer_turbojpeg_build
    SOURCE_DIR "${_turbojpeg_root}"
    CMAKE_ARGS ${_turbojpeg_cmake_args}
    BUILD_ALWAYS OFF
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install
    BYPRODUCTS
        "${_turbojpeg_prefix}/lib/turbojpeg-static.lib"
        "${_turbojpeg_prefix}/lib/libturbojpeg-static.a"
    LOG_CONFIGURE ON
    LOG_BUILD OFF
    LOG_INSTALL ON)

if(MSVC)
    set(_turbojpeg_lib "${_turbojpeg_prefix}/lib/turbojpeg-static.lib")
else()
    set(_turbojpeg_lib "${_turbojpeg_prefix}/lib/libturbojpeg-static.a")
endif()

add_library(mviewer_turbojpeg INTERFACE)
add_dependencies(mviewer_turbojpeg mviewer_turbojpeg_build)
target_include_directories(mviewer_turbojpeg INTERFACE "${_turbojpeg_prefix}/include")
target_link_libraries(mviewer_turbojpeg INTERFACE "${_turbojpeg_lib}")
target_compile_definitions(mviewer_turbojpeg INTERFACE MVIEWER_HAS_TURBOJPEG=1)
target_compile_features(mviewer_turbojpeg INTERFACE cxx_std_17)

set(MVIEWER_HAS_TURBOJPEG ON)
message(STATUS "libjpeg-turbo ${MVIEWER_TURBOJPEG_VERSION}: JPEG fast path enabled "
               "(installed under ${_turbojpeg_prefix})")
