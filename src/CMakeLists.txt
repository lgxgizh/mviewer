# ─── M13 Phase 5: optional Perfetto-style trace instrumentation ────────────
# OFF by default: the MV_TRACE_* macros compile to no-ops and the green build
# gains no dependency. When ON, hot-path spans are recorded and can be flushed
# to a Chrome trace JSON (openable in ui.perfetto.dev / chrome://tracing) via
# mviewer::trace::flush(). Build with -DMVIEWER_ENABLE_PERFETTO=ON to opt in.
option(MVIEWER_ENABLE_PERFETTO "Enable MV_TRACE_* instrumentation -> Chrome trace JSON" OFF)
if(MVIEWER_ENABLE_PERFETTO)
    add_compile_definitions(MVIEWER_ENABLE_PERFETTO)
endif()

add_library(mviewer_core SHARED
    core/EventBus.cpp
    core/image/Decoder.cpp
    core/image/decoder/QtDecoder.cpp
    core/image/decoder/QtFallbackDecoder.cpp
    core/image/decoder/DecoderRegistry.cpp
    core/image/decoder/RawDecoder.cpp
    core/image/ImageObject.cpp
    core/image/ImageFrame.cpp
    core/image/ImageRepository.cpp
    core/image/MetadataReader.cpp
    core/image/RawMetadata.cpp
    core/image/ImageCache.cpp
    core/image/DiskCache.cpp
    core/cache/CacheManager.cpp
    core/image/Encoder.cpp
    core/image/ImageTransform.cpp
    core/image/QtConvert.cpp
    core/filesystem/FileSystem.cpp
    core/scheduler/TaskScheduler.cpp
    core/compare/CompareEngine.cpp
    core/compare/SyncController.cpp
    core/compare/BlinkController.cpp
    core/compare/DifferenceEngine.cpp
    core/RatingStore.cpp
    core/CrashHandler.cpp
    core/SelfTest.cpp
    core/analysis/AnalysisEngine.cpp
    core/analysis/PixelInspector.cpp
    core/analysis/ExportReport.cpp
    core/analysis/ReportHtml.cpp
    core/workspace/WorkspaceSerializer.cpp
    core/project/ProjectSerializer.cpp
    core/analyzer/Analyzer.cpp
    core/analyzer/HistogramAnalyzer.cpp
    core/analyzer/RGBMeanAnalyzer.cpp
    core/analyzer/NoiseAnalyzer.cpp
    core/analyzer/PSNRAnalyzer.cpp
    core/analyzer/SSIMAnalyzer.cpp
    core/analyzer/EntropyAnalyzer.cpp
    core/analyzer/SharpnessAnalyzer.cpp
    core/analyzer/MTFAnalyzer.cpp
    core/analyzer/DeadPixelAnalyzer.cpp
    core/analyzer/ColorCheckerAnalyzer.cpp
    core/render/RenderEngine.cpp
    core/thumbnail/ThumbnailPipeline.h
    core/plugin/PluginLoader.cpp
    core/plugin/PluginManager.cpp
    core/export/IExporter.h
    core/export/ExporterRegistry.cpp

    core/command/CommandRegistry.cpp
    core/command/RotateCommand.cpp
    core/command/CropCommand.cpp
    core/command/LabelCommand.cpp
    core/command/OpenDirectoryCommand.cpp
    core/command/CompareCommand.cpp
    core/command/RenameCommand.cpp
    core/command/DeleteCommand.cpp
    core/command/ToggleHistogramCommand.cpp
    core/command/CallbackCommand.cpp
    core/benchmark/Benchmark.cpp
    core/perf/MemoryTracker.cpp
    core/trace/TraceSink.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
# psapi: provides GetProcessMemoryInfo used by core/perf/MemoryTracker for the
# OS working-set (RSS) sample. Windows-only; harmless on other platforms.
target_link_libraries(mviewer_core PRIVATE Qt6::Widgets Qt6::Sql $<$<PLATFORM_ID:Windows>:psapi>)

target_compile_features(mviewer_core PUBLIC cxx_std_20)

# Plugin systems require a single, shared core so host and plugin share one
# Analyzer/Command vtable (a static core would give each module its own vtable
# copy and crash on cross-module delete / teardown). Auto-export all symbols
# so consumers link without per-symbol declspec markup.
set_target_properties(mviewer_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(mviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_ui STATIC
    mainwindow.cpp
    appstate.cpp
    imageviewer.cpp
    directorytree.cpp
    thumbnailpanel.cpp
    previewpanel.cpp
    thumbnailcache.cpp
    compareworkspace.cpp
    analysispanel.cpp
    exportdialog.cpp
    exportcommand.cpp
    metadatapanel.cpp
    metadatamodel.cpp
    widgets/rawimageview.cpp
    widgets/ratingwidget.cpp
    widgets/histogramwidget.cpp
    gpu/GpuTileUploader.cpp
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
    core/test_m10.cpp
    # M10 suites (incremental isolation)
    core/test_memorytracker.cpp
    core/test_benchmark.cpp
    benchmark/corpus.cpp
    benchmark/scenarios.cpp
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

add_executable(test_appstate
    test_appstate.cpp
    appstate.cpp
)

target_link_libraries(test_appstate
    PRIVATE
        Qt6::Widgets
        Qt6::Core
        mviewer_core
)
target_compile_features(test_appstate PRIVATE cxx_std_20)
target_include_directories(test_appstate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(benchmark
    benchmark_main.cpp
)

# M16: GpuTileUploader bookkeeping (headless; GL upload is a no-op
# offscreen and activates only on real hardware with MVIEWER_GPU=1).
add_executable(test_gputile
    core/test_gputile.cpp
    gpu/GpuTileUploader.cpp
)
target_link_libraries(test_gputile
    PRIVATE
        Qt6::Widgets
        Qt6::Core
        mviewer_core
)
target_compile_features(test_gputile PRIVATE cxx_std_20)
target_include_directories(test_gputile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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
foreach(_t decoder cache repository scheduler metadata filesystem eventbus render_pipeline tilecache pixelcontroller thumbnailpipeline commandstack crop job datamodel pluginregistry pluginregistryrunner m3acceptance analyzer_ext analyzer_batch raw_metadata rawdecode crashhandler pluginabi pluginexamples)
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

# P5: headless release self-test gate. Runs the full application with --selftest,
# which decodes a generated image through the real product path and exits 0/1.
add_test(NAME selftest COMMAND MViewer --selftest)
set_tests_properties(selftest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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

# ─── M9-1 Browse workflow acceptance tests ────────────────────────────────
add_executable(product_browse_tests
    core/test_product_browse.cpp
)

target_link_libraries(product_browse_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(product_browse_tests PRIVATE cxx_std_20)
target_include_directories(product_browse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(product_browse_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── M9-2 Compare workflow acceptance tests ───────────────────────────────
add_executable(compare_workflow_tests
    core/test_compare_workflow.cpp
)

target_link_libraries(compare_workflow_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(compare_workflow_tests PRIVATE cxx_std_20)
target_include_directories(compare_workflow_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(compare_workflow_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── M9-3 Analysis workflow acceptance tests ─────────────────────────────
# ─── P0 #③ / P1 core unit tests (histogram, rating store) ───────────────────
add_executable(histogram_tests
    core/test_histogram.cpp
)
target_link_libraries(histogram_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(histogram_tests PRIVATE cxx_std_20)
target_include_directories(histogram_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(histogram_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(ratingstore_tests
    core/test_ratingstore.cpp
)
target_link_libraries(ratingstore_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(ratingstore_tests PRIVATE cxx_std_20)
target_include_directories(ratingstore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ratingstore_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(analysis_panel_tests
    core/test_analysis_panel.cpp
)

target_link_libraries(analysis_panel_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(analysis_panel_tests PRIVATE cxx_std_20)
target_include_directories(analysis_panel_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(analysis_panel_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── M9-4 Export workflow acceptance tests ────────────────────────────────
add_executable(export_tests
    core/test_export.cpp
)

target_link_libraries(export_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(export_tests PRIVATE cxx_std_20)
target_include_directories(export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(export_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# P4 — Batch export pipeline (resize / watermark / contact sheet / PDF / rename)
add_executable(export_pipeline_tests
    core/test_export_pipeline.cpp
)

target_link_libraries(export_pipeline_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(export_pipeline_tests PRIVATE cxx_std_20)
target_include_directories(export_pipeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(export_pipeline_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── M9-5 Workspace persistence acceptance tests ──────────────────────────
add_executable(workspace_persist_tests
    core/test_workspace_persist.cpp
)

target_link_libraries(workspace_persist_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(workspace_persist_tests PRIVATE cxx_std_20)
target_include_directories(workspace_persist_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(workspace_persist_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ─── M15 Project (.mvproj) acceptance tests ──────────────────────────────
add_executable(test_project
    core/test_project.cpp
)

target_link_libraries(test_project
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(test_project PRIVATE cxx_std_20)
target_include_directories(test_project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_project PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME project_tests COMMAND test_project)

# ─── M15 Pixel Inspector Pro (color-space + kernel math) ───────────────────
add_executable(test_pixelinspector
    core/test_pixelinspector.cpp
)

target_link_libraries(test_pixelinspector
    PRIVATE
        Qt6::Core
        mviewer_core
)

target_compile_features(test_pixelinspector PRIVATE cxx_std_20)
target_include_directories(test_pixelinspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME pixelinspector_tests COMMAND test_pixelinspector)

# ─── M15 P0#4 Metadata Center (unified MetadataModel + QTreeView) ───────────
add_executable(test_metadatamodel
    test_metadatamodel.cpp
)

target_link_libraries(test_metadatamodel
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(test_metadatamodel PRIVATE cxx_std_20)
target_include_directories(test_metadatamodel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_metadatamodel PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME metadatamodel_tests COMMAND test_metadatamodel)

# ─── M9-6 Polish (shortcuts route via Command) acceptance tests ──────────
add_executable(shortcuts_tests
    core/test_shortcuts.cpp
)

target_link_libraries(shortcuts_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(shortcuts_tests PRIVATE cxx_std_20)
target_include_directories(shortcuts_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(shortcuts_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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

# M13.6: wire the loader/manager unit tests into the gate (they were built but
# never registered as ctest, so they could bit-rot). Both pass standalone.
add_test(NAME pluginloader_tests COMMAND test_plugin_loader)
add_test(NAME pluginmanager_tests COMMAND test_plugin_manager)

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

# M11.3 release artifact: render the REAL MainWindow to a PNG (headless via
# QT_QPA_PLATFORM=offscreen + QWidget::grab). No mock — uses actual UI code.
add_executable(ui_screenshot
    ui_screenshot.cpp
)

target_link_libraries(ui_screenshot
    PRIVATE
        Qt6::Widgets
        mviewer_ui
)

target_compile_features(ui_screenshot PRIVATE cxx_std_20)
target_include_directories(ui_screenshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

# M18 demo harness: drives the REAL MainWindow on the visible desktop so a
# screen recorder can capture the new file-search / metadata workflow. Exercised
# by scripts/record_demo.ps1; not part of the test gate.
add_executable(mviewer_demo
    demo_workflow.cpp
)

target_link_libraries(mviewer_demo
    PRIVATE
        Qt6::Widgets
        mviewer_ui
)

target_compile_features(mviewer_demo PRIVATE cxx_std_20)
target_include_directories(mviewer_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

# M18 demo render: renders the REAL MainWindow offscreen and grabs three
# workflow states (dir open / image selected+metadata / live search) to PNGs;
# scripts/record_demo.ps1 stitches them into an animated GIF. Headless-safe
# (QT_QPA_PLATFORM=offscreen); genuinely renders the real UI code.
add_executable(mviewer_demo_render
    demo_render.cpp
)

target_link_libraries(mviewer_demo_render
    PRIVATE
        Qt6::Widgets
        mviewer_ui
)

target_compile_features(mviewer_demo_render PRIVATE cxx_std_20)
target_include_directories(mviewer_demo_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})

# M13 Phase 4 — real-image-dataset acceptance: open every testdata/ fixture
# via the real Decoder; assert no crash + (decoded OR gracefully skipped).
add_executable(test_assets_acceptance
    core/test_assets_acceptance.cpp
)

target_link_libraries(test_assets_acceptance
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(test_assets_acceptance PRIVATE cxx_std_20)
target_include_directories(test_assets_acceptance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_assets_acceptance PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")


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
add_test(NAME appstate_tests   COMMAND test_appstate)
add_test(NAME m3pipeline_tests COMMAND m3pipeline_tests)
add_test(NAME decoder_tests    COMMAND test_decoder)
add_test(NAME cache_tests      COMMAND test_cache)
add_test(NAME repository_tests COMMAND test_repository)
add_test(NAME scheduler_tests  COMMAND test_scheduler)
add_test(NAME metadata_tests   COMMAND test_metadata)
add_test(NAME filesystem_tests COMMAND test_filesystem)
add_test(NAME eventbus_tests   COMMAND test_eventbus)
add_test(NAME render_pipeline_tests COMMAND test_render_pipeline)
add_test(NAME tilecache_tests COMMAND test_tilecache)
add_test(NAME gputile_tests  COMMAND test_gputile)
add_test(NAME rawdecode_tests COMMAND test_rawdecode)
add_test(NAME pixelcontroller_tests COMMAND test_pixelcontroller)
add_test(NAME thumbnailpipeline_tests COMMAND test_thumbnailpipeline)
add_test(NAME commandstack_tests COMMAND test_commandstack)
add_test(NAME crop_tests COMMAND test_crop)
add_test(NAME job_tests COMMAND test_job)
add_test(NAME datamodel_tests COMMAND test_datamodel)
add_test(NAME pluginregistry_tests COMMAND test_pluginregistryrunner)
add_test(NAME pluginabi_tests COMMAND test_pluginabi)
add_test(NAME pluginexamples_tests COMMAND test_pluginexamples)

# The exporter example is compiled into the test so its IExporter implementation
# can be exercised via its C entry point (createExporter) WITHOUT LoadLibrary'ing
# the Qt-GUI exporter DLL, which is unsafe inside a test process on Windows.
target_sources(test_pluginexamples PRIVATE ${CMAKE_SOURCE_DIR}/plugins/example/ExampleExporterPlugin.cpp)
add_test(NAME m3acceptance_tests COMMAND test_m3acceptance)
add_test(NAME product_browse_tests COMMAND product_browse_tests)
add_test(NAME compare_workflow_tests COMMAND compare_workflow_tests)
add_test(NAME histogram_tests COMMAND histogram_tests)
add_test(NAME ratingstore_tests COMMAND ratingstore_tests)
add_test(NAME analysis_panel_tests COMMAND analysis_panel_tests)
add_test(NAME export_tests COMMAND export_tests)
add_test(NAME export_pipeline_tests COMMAND export_pipeline_tests)

# P3 tail — RatingStore flag persistence (color label / reject / pick / recents)
add_executable(flags_tests
    core/test_flags.cpp
)

target_link_libraries(flags_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(flags_tests PRIVATE cxx_std_20)
target_include_directories(flags_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(flags_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME flags_tests COMMAND flags_tests)

# P2 — AnalyzerRegistry extensibility: a new analyzer is discoverable/runnable
# without any MainWindow/UI change.
add_executable(analyzer_registry_tests
    core/test_register.cpp
)

target_link_libraries(analyzer_registry_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(analyzer_registry_tests PRIVATE cxx_std_20)
target_include_directories(analyzer_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(analyzer_registry_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME analyzer_registry_tests COMMAND analyzer_registry_tests)
add_test(NAME workspace_persist_tests COMMAND workspace_persist_tests)
add_test(NAME assets_acceptance_tests COMMAND test_assets_acceptance)
add_test(NAME analysis_batch_tests COMMAND test_analyzer_batch)
add_test(NAME raw_metadata_tests COMMAND test_raw_metadata)
add_test(NAME crashhandler_tests COMMAND test_crashhandler)
# ─── M13 Phase 1: Product Workflow gate ──────────────────────────────────
# A single ctest that runs the full user-chain in order:
#   Browse -> Compare -> Analyzer -> Export -> Workspace
# (-> Restart -> Restore is exercised inside workspace_persist_tests).
# If any link regresses, the whole workflow gate fails — so no PR can
# silently break the product loop. Orchestration only: a small PowerShell
# script chains the existing per-phase executables (no new C++ test logic).
add_test(NAME product_workflow_gate
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/product_workflow_gate.ps1
        $<TARGET_FILE_DIR:product_browse_tests>)

# ─── M10 Performance Engineering ─────────────────────────────────────────────
# MemoryTracker + benchmark scenario structural tests are folded into core_tests
# (via core/test_m10.cpp) — the known-good consolidated exe link environment.
#
# mviewer_bench: standalone perf harness (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
# CI only runs --smoke (proves it links + runs). The --enforce regression gate
# is roadmap Phase-4 and is NOT wired into ci.yml here.
add_executable(mviewer_bench
    benchmark/main.cpp
    benchmark/corpus.cpp
    benchmark/scenarios.cpp
)

target_link_libraries(mviewer_bench
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        mviewer_core
)

target_compile_features(mviewer_bench PRIVATE cxx_std_20)
target_include_directories(mviewer_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mviewer_bench PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME bench_smoke COMMAND mviewer_bench --smoke)

# M13.3 — Performance Budget as a HARD gate (data-driven via JSON).
# Runs the same binary in --enforce mode against
# benchmark/performance_budget.json. On a budget violation the binary exits
# non-zero and THIS ctest FAILS (so `.\build.ps1 Test` cannot go green
# with a perf regression). The PR-level perf-gate.yml reuses the same
# invocation against the committed baseline.
add_test(NAME bench_enforce
    COMMAND mviewer_bench --enforce
            --budget ${CMAKE_SOURCE_DIR}/benchmark/performance_budget.json)

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
