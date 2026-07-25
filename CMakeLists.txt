cmake_minimum_required(VERSION 3.22)

project(MViewer
    VERSION 1.0.4
    DESCRIPTION "A Qt 6 based image viewer"
    LANGUAGES CXX
)

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

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable CTest before add_subdirectory(src) so the add_test() calls in
# src/CMakeLists.txt register into the generated CTestTestfile.
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

add_subdirectory(src)
add_subdirectory(plugins/example)
