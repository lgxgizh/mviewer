# Focused required CI target: instrument the actual production parser, not a mock.
cmake_minimum_required(VERSION 3.24)
project(SessionPackageSanitizerGate LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Session ZIP libFuzzer gate requires clang")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_library(session_package_instrumented STATIC
    ${ROOT}/src/core/session/SessionPackage.cpp
    ${ROOT}/src/core/filesystem/Utf8Path.cpp)
target_include_directories(session_package_instrumented PUBLIC ${ROOT}/src)
target_compile_features(session_package_instrumented PUBLIC cxx_std_20)
target_link_libraries(session_package_instrumented PUBLIC Qt6::Core Threads::Threads)
target_compile_options(session_package_instrumented PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g -O1)
add_executable(fuzz_session_package ../fuzz_session_package.cpp)
target_link_libraries(fuzz_session_package PRIVATE session_package_instrumented)
target_compile_options(fuzz_session_package PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
target_link_options(fuzz_session_package PRIVATE -fsanitize=fuzzer,address,undefined)
add_executable(session_package_sanitized ../../core/test_session_package.cpp)
target_link_libraries(session_package_sanitized PRIVATE session_package_instrumented)
target_compile_options(session_package_sanitized PRIVATE -fsanitize=address,undefined -g -O1)
# The production objects contain coverage instrumentation even in the unit binary.
target_link_options(session_package_sanitized PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
