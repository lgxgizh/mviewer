add_library(mviewer_core STATIC
    core/EventBus.cpp
    core/image/Decoder.cpp
    core/image/decoder/QtDecoder.cpp
    core/image/decoder/QtFallbackDecoder.cpp
    core/image/decoder/DecoderRegistry.cpp
    core/image/ImageObject.cpp
    core/image/ImageFrame.cpp
    core/image/ImageRepository.cpp
    core/image/ImageCache.cpp
    core/image/DiskCache.cpp
    core/cache/CacheManager.cpp
    core/image/Encoder.cpp
    core/image/QtConvert.cpp
    core/filesystem/FileSystem.cpp
    core/scheduler/TaskScheduler.cpp
    core/compare/CompareEngine.cpp
    core/compare/SyncController.cpp
    core/compare/BlinkController.cpp
    core/compare/DifferenceEngine.cpp
    core/analysis/AnalysisEngine.cpp
    core/analyzer/Analyzer.cpp
    core/analyzer/HistogramAnalyzer.cpp
    core/analyzer/RGBMeanAnalyzer.cpp
    core/analyzer/NoiseAnalyzer.cpp
    core/analyzer/PSNRAnalyzer.cpp
    core/analyzer/SSIMAnalyzer.cpp
    core/analyzer/EntropyAnalyzer.cpp
    core/analyzer/SharpnessAnalyzer.cpp
    core/render/RenderEngine.cpp
    core/plugin/PluginLoader.cpp
    core/plugin/PluginManager.cpp

    core/command/CommandRegistry.cpp
    core/command/OpenDirectoryCommand.cpp
    core/command/CompareCommand.cpp
    core/command/RenameCommand.cpp
    core/command/DeleteCommand.cpp
    core/command/ToggleHistogramCommand.cpp
    core/benchmark/Benchmark.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
target_link_libraries(mviewer_core PUBLIC Qt6::Widgets Qt6::Sql)

target_compile_features(mviewer_core PUBLIC cxx_std_20)

target_include_directories(mviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_ui STATIC
    mainwindow.cpp
    imageviewer.cpp
    directorytree.cpp
    thumbnailpanel.cpp
    previewpanel.cpp
    thumbnailcache.cpp
    compareworkspace.cpp
    analysispanel.cpp
    exportdialog.cpp
    exportcommand.cpp
    widgets/rawimageview.cpp
)

target_link_libraries(mviewer_ui PUBLIC Qt6::Widgets mviewer_core mviewer_application)

target_compile_features(mviewer_ui PUBLIC cxx_std_20)

target_include_directories(mviewer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_application STATIC
    application/OpenDirectoryUseCase.cpp
    application/CompareImagesUseCase.cpp
    application/RenameImageUseCase.cpp
    application/DeleteImageUseCase.cpp
    application/Startup.cpp
)

target_link_libraries(mviewer_application PUBLIC mviewer_core Qt6::Gui)
target_include_directories(mviewer_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mviewer_application PUBLIC cxx_std_20)

add_executable(MViewer
    main.cpp
)

target_link_libraries(MViewer
    PRIVATE
        Qt6::Widgets
        mviewer_core
        mviewer_ui
        mviewer_application
)

target_compile_features(MViewer PRIVATE cxx_std_20)

target_include_directories(MViewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(core_tests
    core/test_compare.cpp
)

target_link_libraries(core_tests
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(core_tests PRIVATE cxx_std_20)

target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(mviewer_unit_tests
    core/test_core.cpp
)

target_link_libraries(mviewer_unit_tests
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(mviewer_unit_tests PRIVATE cxx_std_20)

target_include_directories(mviewer_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(benchmark
    benchmark_main.cpp
)

target_link_libraries(benchmark
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(benchmark PRIVATE cxx_std_20)

target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_m3m4m5
    core/test_m3m4m5.cpp
)

target_link_libraries(test_m3m4m5
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(test_m3m4m5 PRIVATE cxx_std_20)

target_include_directories(test_m3m4m5 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ─── M6 split test suites (per-module) ────────────────────────────────────
foreach(_t decoder cache repository scheduler metadata filesystem eventbus)
    add_executable(test_${_t}
        core/test_${_t}.cpp
    )
    target_link_libraries(test_${_t}
        PRIVATE
            Qt6::Widgets
            Qt6::Gui
            Qt6::Core
            mviewer_core
    )
    target_compile_features(test_${_t} PRIVATE cxx_std_20)
    target_include_directories(test_${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_${_t} PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endforeach()

# ─── M3 Phase-1 pipeline acceptance tests ───────────────────────────────
add_executable(m3pipeline_tests
    core/test_m3_pipeline.cpp
)

target_link_libraries(m3pipeline_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(m3pipeline_tests PRIVATE cxx_std_20)

target_include_directories(m3pipeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m3pipeline_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── Engineering Excellence Targets ─────────────────────────────────────────

add_executable(vision_regression
    ${CMAKE_SOURCE_DIR}/tests/vision/vision_regression.cpp
)

target_link_libraries(vision_regression
    PRIVATE
        Qt6::Gui
        Qt6::Core
)

target_compile_features(vision_regression PRIVATE cxx_std_20)
target_include_directories(vision_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(vision_regression PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(golden_main
    ${CMAKE_SOURCE_DIR}/golden/golden_main.cpp
)

target_link_libraries(golden_main
    PRIVATE
        Qt6::Gui
        Qt6::Core
)

target_compile_features(golden_main PRIVATE cxx_std_20)
target_include_directories(golden_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(golden_main PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(benchmark_scenario
    ${CMAKE_SOURCE_DIR}/benchmarks/benchmark_main.cpp
)

target_link_libraries(benchmark_scenario
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

# ─── Testdata regression ───────────────────────────────────────────────

add_executable(testdata_regression
    ${CMAKE_SOURCE_DIR}/tests/testdata_regression.cpp
)

target_link_libraries(testdata_regression
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(testdata_regression PRIVATE cxx_std_20)
target_include_directories(testdata_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(testdata_regression PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_compile_features(benchmark_scenario PRIVATE cxx_std_20)
target_include_directories(benchmark_scenario PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(benchmark_scenario PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── Plugin loader tests ─────────────────────────────────────────────────

add_executable(test_plugin_loader
    ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin_loader.cpp
)

target_link_libraries(test_plugin_loader
    PRIVATE
        mviewer_core
)

target_compile_features(test_plugin_loader PRIVATE cxx_std_20)
target_include_directories(test_plugin_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

# ─── PluginManager tests ──────────────────────────────────────────────

add_executable(test_plugin_manager
    ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin_manager.cpp
)

target_link_libraries(test_plugin_manager
    PRIVATE
        mviewer_core
)

target_compile_features(test_plugin_manager PRIVATE cxx_std_20)
target_include_directories(test_plugin_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

# ─── UI fixture regression ─────────────────────────────────────────────

add_executable(ui_fixture
    ${CMAKE_SOURCE_DIR}/tests/ui_fixture/ui_fixture.cpp
)

target_link_libraries(ui_fixture
    PRIVATE
        Qt6::Widgets
        mviewer_ui
)

target_compile_features(ui_fixture PRIVATE cxx_std_20)
target_include_directories(ui_fixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(ui_fixture PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── Clang-Tidy (optional) ──────────────────────────────────────────────────

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
else()
    message(STATUS "clang-tidy not found; skipping")
endif()

# ─── ctest registration ──────────────────────────────────────────────────────

add_test(NAME core_tests       COMMAND core_tests)
add_test(NAME m3m4m5_tests     COMMAND test_m3m4m5)
add_test(NAME unit_tests       COMMAND mviewer_unit_tests)
add_test(NAME m3pipeline_tests COMMAND m3pipeline_tests)
add_test(NAME decoder_tests    COMMAND test_decoder)
add_test(NAME cache_tests      COMMAND test_cache)
add_test(NAME repository_tests COMMAND test_repository)
add_test(NAME scheduler_tests  COMMAND test_scheduler)
add_test(NAME metadata_tests   COMMAND test_metadata)
add_test(NAME filesystem_tests COMMAND test_filesystem)
add_test(NAME eventbus_tests   COMMAND test_eventbus)

# ─── Format target ──────────────────────────────────────────────────────────

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
         ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
         ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format on all source files"
        VERBATIM)
endif()
