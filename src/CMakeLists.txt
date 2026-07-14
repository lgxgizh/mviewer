add_library(mviewer_core STATIC
    core/EventBus.cpp
    core/image/Decoder.cpp
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
    core/compare/SelectionController.cpp
    core/compare/ViewportController.cpp
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
)

target_link_libraries(mviewer_ui PUBLIC Qt6::Widgets mviewer_core mviewer_application)

target_compile_features(mviewer_ui PUBLIC cxx_std_20)

target_include_directories(mviewer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_application STATIC
    application/OpenDirectoryUseCase.cpp
    application/CompareImagesUseCase.cpp
    application/RenameImageUseCase.cpp
    application/DeleteImageUseCase.cpp
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

add_executable(visual_test
    visual_test.cpp
)

target_link_libraries(visual_test
    PRIVATE
        Qt6::Widgets
        mviewer_core
        mviewer_ui
)

target_compile_features(visual_test PRIVATE cxx_std_20)

target_include_directories(visual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

add_executable(analyze_main
    analyze_main.cpp
)

target_link_libraries(analyze_main
    PRIVATE
        Qt6::Gui
        Qt6::Core
)

target_compile_features(analyze_main PRIVATE cxx_std_20)

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

target_compile_features(benchmark_scenario PRIVATE cxx_std_20)
target_include_directories(benchmark_scenario PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

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

# ─── Clang-Tidy (optional) ──────────────────────────────────────────────────

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
else()
    message(STATUS "clang-tidy not found; skipping")
endif()
