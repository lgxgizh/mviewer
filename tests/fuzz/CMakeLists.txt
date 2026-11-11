# Review P1-5: fuzz targets for the hand-written binary parsers.
#
# Default build: each target is linked with fuzz_standalone_main.cpp and CTest
# replays the committed seed corpus (tests/fuzz/corpus/<target>) on every
# platform, so reproducers found by nightly fuzzing stay fixed.
#
# -DMVIEWER_FUZZ=ON (clang only, used by the nightly fuzz-linux job): targets
# link libFuzzer (-fsanitize=fuzzer) and are run with -max_total_time.
option(MVIEWER_FUZZ "Build libFuzzer targets (clang -fsanitize=fuzzer)" OFF)

set(MVIEWER_FUZZ_TARGETS fuzz_raw_metadata fuzz_exif_reader fuzz_y4m fuzz_yuv_inference)

foreach(target IN LISTS MVIEWER_FUZZ_TARGETS)
    if(MVIEWER_FUZZ)
        add_executable(${target} ${target}.cpp)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    else()
        add_executable(${target} ${target}.cpp fuzz_standalone_main.cpp)
        add_test(NAME ${target}_corpus
            COMMAND ${target} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${target})
        set_tests_properties(${target}_corpus PROPERTIES LABELS "fuzz;pr-fast" TIMEOUT 120)
    endif()
    target_link_libraries(${target} PRIVATE mviewer_core Qt6::Core)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src
                                                 ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
