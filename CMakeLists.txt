cmake_minimum_required(VERSION 3.22)

project(MViewer
    VERSION 1.0.3
    DESCRIPTION "A Qt 6 based image viewer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_subdirectory(src)
add_subdirectory(plugins/example)
