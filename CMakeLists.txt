cmake_minimum_required(VERSION 3.22)

project(MViewer
    VERSION 1.0.178
    DESCRIPTION "A Qt 6 based image viewer"
    LANGUAGES CXX
)

# ── Version SSOT (M24) ───────────────────────────────────────────────────────
# project(VERSION) above is the SINGLE source of truth. Everything that needs
# the version (About dialog, UpdateChecker, workspace appVersion, log header,
# package/installer naming) consumes the generated header / version_info.txt.
# Add a new release by bumping project(VERSION) and tagging vX.Y.Z.
set(MVIEWER_GIT_DESCRIBE "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE MVIEWER_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MViewerVersion.h.in
               ${CMAKE_BINARY_DIR}/generated/MViewerVersion.h @ONLY)
file(WRITE ${CMAKE_BINARY_DIR}/version_info.txt
     "MVIEWER_VERSION=${PROJECT_VERSION}\nMVIEWER_GIT_DESCRIBE=${MVIEWER_GIT_DESCRIBE}\n")
# The generated header dir is a usage requirement of mviewer_core (PUBLIC in
# src/CMakeLists.txt), not a directory-wide include path.
set(MVIEWER_GENERATED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated)
# Review P2-14: the configure-time describe above goes stale after an
# incremental build (About dialog and logs showed an old describe). This target
# refreshes it before every build; mviewer_core depends on it.
add_custom_target(mviewer_version_stamp
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -DPROJECT_VERSION=${PROJECT_VERSION}
            -DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
            -DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
            -DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
            -P ${CMAKE_SOURCE_DIR}/cmake/MViewerVersionStamp.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/MViewerVersion.h
               ${CMAKE_BINARY_DIR}/version_info.txt
    COMMENT "Refreshing git describe in MViewerVersion.h"
    VERBATIM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Suppress benign warnings emitted by the MSVC 14.44 STL <atomic> header
# (C4869: 'nodiscard' applied to non-function; C4228: qualifiers after comma).
# These are STL-internal and break the CI zero-warnings gate for every commit;
# they do not originate from project code. Guard to MSVC so clang builds are
# unaffected.
if(MSVC)
    add_compile_options(/wd4869 /wd4228)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MViewerWarnings.cmake)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Release debug symbols (review P0-4) ──────────────────────────────────────
# MSVC Release used to produce no PDBs, so user minidumps (CrashHandler) could
# not be symbolized and OpenCppCoverage selected no modules (coverage 0%).
# /Z7 stores debug info in each .obj (compiler-cache friendly, no shared
# compile PDB); /DEBUG makes the linker write <target>.pdb; /OPT:REF and
# /OPT:ICF restore the Release defaults that /DEBUG would otherwise turn off.
# Code generation is unchanged. Linker PDBs are written only for the shipping
# binaries (MViewer.exe, mviewer_core.dll, plugins) unless MVIEWER_ALL_PDBS is
# ON (the nightly coverage job sets it through the environment), because ~150
# test executables each statically link the UI library.
if(DEFINED ENV{MVIEWER_ALL_PDBS})
    set(_mviewer_all_pdbs_default "$ENV{MVIEWER_ALL_PDBS}")
else()
    set(_mviewer_all_pdbs_default OFF)
endif()
option(MVIEWER_ALL_PDBS "Write linker PDBs for every MSVC executable (coverage)"
       ${_mviewer_all_pdbs_default})
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Z7>)
    if(MVIEWER_ALL_PDBS)
        add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF>
                         $<$<CONFIG:Release>:/OPT:ICF>)
    endif()
endif()
function(mviewer_release_pdb target)
    if(MSVC AND NOT MVIEWER_ALL_PDBS)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG>
                            $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    endif()
endfunction()

# Enable CTest before add_subdirectory(src) so the add_test() calls in
# src/CMakeLists.txt register into the generated CTestTestfile.
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

add_subdirectory(src)
add_subdirectory(plugins/example)
add_subdirectory(tests/fuzz)
