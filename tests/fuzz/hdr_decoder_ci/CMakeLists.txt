# Focused required CI target: instrument the actual production decoder, not a mock.
cmake_minimum_required(VERSION 3.24)
project(HdrDecoderSanitizerGate LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "HDR libFuzzer gate requires clang")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_library(hdr_decoder_instrumented STATIC
    ${ROOT}/src/core/image/decoder/HdrDecoder.cpp
    ${ROOT}/src/core/filesystem/Utf8Path.cpp)
target_include_directories(hdr_decoder_instrumented PUBLIC ${ROOT}/src)
target_compile_features(hdr_decoder_instrumented PUBLIC cxx_std_20)
target_link_libraries(hdr_decoder_instrumented PUBLIC Qt6::Core Threads::Threads)
target_compile_options(hdr_decoder_instrumented PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g -O1)
add_executable(fuzz_hdr_decoder ../fuzz_hdr_decoder.cpp)
target_link_libraries(fuzz_hdr_decoder PRIVATE hdr_decoder_instrumented)
target_compile_options(fuzz_hdr_decoder PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
target_link_options(fuzz_hdr_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
add_executable(hdr_decoder_sanitized ../../core/test_hdr_decoder.cpp)
target_link_libraries(hdr_decoder_sanitized PRIVATE hdr_decoder_instrumented)
target_compile_options(hdr_decoder_sanitized PRIVATE -fsanitize=address,undefined -g -O1)
# The production objects contain coverage instrumentation even in the unit binary.
target_link_options(hdr_decoder_sanitized PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
