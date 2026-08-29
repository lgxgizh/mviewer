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
    runtime_storage.cpp
    core/EventBus.cpp
    core/image/Decoder.cpp
    core/image/decoder/QtDecoder.cpp
    core/image/decoder/QtFallbackDecoder.cpp
    core/image/decoder/DecoderRegistry.cpp
    core/image/decoder/RawDecoder.cpp
    core/image/ImageObject.cpp
    core/image/ImageFrame.cpp
    core/image/ImageRepository.cpp
    core/image/ImageRepository_async.cpp
    core/image/ImageRepository_dir.cpp
    core/image/ImageRepository_load.cpp
    core/image/SourceImage.cpp
    core/image/IccProfile.cpp
    core/image/MetadataReader.cpp
    core/image/RawMetadata.cpp
    core/image/ImageCache.cpp
    core/image/DiskCache.cpp
    core/cache/CacheManager.cpp
    core/image/Encoder.cpp
    core/image/ImageFormats.cpp
    core/image/ImageTransform.cpp
    core/image/QtConvert.cpp
    core/image/ImageStats.cpp
    core/filesystem/FileSystem.cpp
    core/filesystem/AtomicFile.cpp
    core/filesystem/Utf8Path.cpp
    core/scheduler/TaskScheduler.cpp
    core/compare/CompareEngine.cpp
    core/compare/SyncController.cpp
    core/compare/BlinkController.cpp
    core/compare/DifferenceEngine.cpp
    core/compare/Aligner.cpp
    core/RatingStore.cpp
    core/TagStore.cpp
    core/SidecarStore.cpp
    core/CrashHandler.cpp
    core/Logger.cpp
    core/SelfTest.cpp
    core/SettingsIO.cpp
    core/analysis/AnalysisEngine.cpp
    core/analysis/PixelInspector.cpp
    core/analysis/ExportReport.cpp
    core/analysis/ExportReport_serialize.cpp
    core/analysis/ReportHtml.cpp
    core/workspace/WorkspaceSerializer.cpp
    core/project/ProjectSerializer.cpp
    core/analyzer/Analyzer.cpp
    core/analyzer/AnalyzerPipeline.cpp
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
    core/analyzer/BrightnessAnalyzer.cpp
    core/analyzer/ContrastAnalyzer.cpp
    core/analyzer/BlurAnalyzer.cpp
    core/analyzer/ColorCastAnalyzer.cpp
    core/analyzer/ExposureAnalyzer.cpp
    core/render/RenderEngine.cpp
    core/render/AsyncTileRequestManager.cpp
    core/thumbnail/ThumbnailPipeline.h
    core/plugin/PluginLoader.cpp
    core/plugin/PluginManager.cpp
    core/export/IExporter.h
    core/export/ExporterRegistry.cpp
    core/import/ImporterRegistry.cpp
    core/export/ExportManager.cpp
    core/export/ExportJob.cpp
    core/export/ExportJob_run.cpp
    core/search/SearchEngine.cpp
    core/search/MetadataFilter.h
    core/image/ImageSortKeys.cpp
    core/metadata/MetadataIndexer.cpp
    core/metadata/MetadataPresentationService.cpp
    core/batch/BatchProcessor.cpp
    core/update/UpdateChecker.cpp

    core/command/CommandRegistry.cpp
    core/command/RotateCommand.cpp
    core/command/CropCommand.cpp
    core/command/LabelCommand.cpp
    core/command/OpenDirectoryCommand.cpp
    core/command/CompareCommand.cpp
    core/command/RenameCommand.cpp
    core/command/DeleteCommand.cpp
    core/command/FileDeleteCommand.cpp
    core/command/FileRenameCommand.cpp
    core/command/FileMoveCommand.cpp
    core/command/FileSystemAdapter.cpp
    core/command/ToggleHistogramCommand.cpp
    core/command/CallbackCommand.cpp
    core/benchmark/Benchmark.cpp
    core/perf/MemoryTracker.cpp
    core/trace/TraceSink.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql OpenGL OpenGLWidgets)
# psapi: provides GetProcessMemoryInfo used by core/perf/MemoryTracker for the
# OS working-set (RSS) sample. Windows-only; harmless on other platforms.
target_link_libraries(mviewer_core PRIVATE Qt6::Widgets Qt6::Sql
    $<$<PLATFORM_ID:Windows>:psapi;windowscodecs;ole32>)

target_compile_features(mviewer_core PUBLIC cxx_std_20)

# Plugin systems require a single, shared core so host and plugin share one
# Analyzer/Command vtable (a static core would give each module its own vtable
# copy and crash on cross-module delete / teardown). Auto-export all symbols
# so consumers link without per-symbol declspec markup.
set_target_properties(mviewer_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(mviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_ui STATIC
    mainwindow.cpp
    mainwindow_ui.cpp
    mainwindow_ui_layout.cpp
    mainwindow_ui_connections.cpp
    mainwindow_commands.cpp
    mainwindow_navigation.cpp
    mainwindow_session.cpp
    mainwindow_session_notifications.cpp
    mainwindow_export.cpp
    mainwindow_view.cpp
    selectionmodel.cpp
    directorymodel.cpp
    imagelistmodel.cpp
    workspacemodel.cpp
    analyzermodel.cpp
    appstate.cpp
    imageviewer.cpp
    imageviewer_loading.cpp
    imageviewer_lod.cpp
    imageviewer_paint.cpp
    imageviewer_contextmenu.cpp
    directorytree.cpp
    breadcrumbbar.cpp
    metadataoverlay.cpp
    thumbnailpanel.cpp
    thumbnailpanel_async.cpp
    thumbnailpanel_selection.cpp
    thumbnailpanel_pipeline.cpp
    thumbnailpanel_filters.cpp
    thumbnailpanel_fileops.cpp
    thumbnailpanel_delegates.cpp
    thumbnailpanel_viewmode.cpp
    thumbnailprovider.cpp
    previewpanel.cpp
    thumbnailcache.cpp
    compareworkspace.cpp
    compareworkspace_display_planner.cpp
    compareworkspace_controls.cpp
    compareworkspace_render.cpp
    compareworkspace_render_materialization.cpp
    compareworkspace_render_diff.cpp
    compareworkspace_render_canvas.cpp
    compareworkspace_analysis.cpp
    compareworkspace_editpanel.cpp
    compareworkspace_interact.cpp
    compareworkspace_nav.cpp
    analysispanel.cpp
    analysispanel_analysis.cpp
    analysispanel_analysis_worker.cpp
    analysispanel_actions.cpp
    analysispanel_buildui.cpp
    analysispanel_pages.cpp
    analysispanel_history.cpp
    exportdialog.cpp
    exportcommand.cpp
    metadatapanel.cpp
    metadatamodel.cpp
    searchpanel.cpp
    batchdialog.cpp
    widgets/rawimageview.cpp
    widgets/ratingwidget.cpp
    widgets/histogramwidget.cpp
    pluginsettings.cpp
    preferencesdialog.cpp
    analysisoverlaydialog.cpp
    gpu/GpuTileUploader.cpp
)

target_link_libraries(mviewer_ui PUBLIC
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    mviewer_core
    mviewer_application
)

target_compile_features(mviewer_ui PUBLIC cxx_std_20)

target_include_directories(mviewer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_application STATIC
    application/ExternalOpen.cpp
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

# ── Auto-deploy Qt runtime next to the executable (windeployqt) ───────────────
# Fixes the recurring "missing DLL" (0xc0000135) errors when launching
# build_msvc/bin/MViewer.exe directly. windeployqt copies exactly the Qt6 DLLs +
# platform/imageformat/sqldrivers plugins the binary imports, so the exe becomes
# self-contained (no Qt on PATH required). Runs on every build and is
# build-frontend agnostic (build.ps1, CMakePresets, IDE). The MSVC CRT is deployed
# separately by build.ps1 (it knows the VS redist location and is more reliable).
if(WIN32 AND TARGET MViewer AND Qt6_FOUND)
    get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXE NAMES windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXE)
        # Deploy against mviewer_core.dll too: the shared core links Qt6::Sql
        # (SQLite DiskCache) which MViewer.exe does not import directly, so
        # windeployqt on MViewer.exe alone can miss Qt6Sql.dll.
        add_custom_command(TARGET MViewer POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-compiler-runtime "$<TARGET_FILE:MViewer>"
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-compiler-runtime "$<TARGET_FILE:mviewer_core>"
            COMMENT "Deploying Qt runtime via windeployqt"
            VERBATIM)
        # Safety net: ensure Qt6Sql.dll ships (DiskCache uses SQLite at runtime).
        if(EXISTS "${_qt_bin_dir}/Qt6Sql.dll")
            add_custom_command(TARGET MViewer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt_bin_dir}/Qt6Sql.dll"
                        "$<TARGET_FILE_DIR:mviewer_core>"
                VERBATIM)
        endif()
        # Safety net: headless tests run with QT_QPA_PLATFORM=offscreen. Once
        # windeployqt anchors Qt's prefix to bin/, the offscreen platform plugin
        # must also live in bin/platforms or every headless GUI test aborts with
        # "no Qt platform plugin could be initialized" (0xc0000409).
        get_filename_component(_qt_plugin_dir "${_qt_bin_dir}/../plugins" ABSOLUTE)
        if(EXISTS "${_qt_plugin_dir}/platforms/qoffscreen.dll")
            add_custom_command(TARGET MViewer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt_plugin_dir}/platforms/qoffscreen.dll"
                        "$<TARGET_FILE_DIR:MViewer>/platforms"
                VERBATIM)
        endif()
    else()
        message(WARNING "windeployqt not found (expected at ${_qt_bin_dir}); "
                        "MViewer.exe may fail to launch without Qt on PATH")
    endif()
endif()

add_executable(core_tests
    core/test_compare.cpp
    core/test_m10.cpp
    # M10 suites (incremental isolation)
    core/test_memorytracker.cpp
    core/test_benchmark.cpp
    core/perf/GpuTracker.cpp
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

# M19: UI models (Selection / Directory / ImageList / Workspace / Analyzer) headless unit test.
add_executable(test_ui_models
    test_ui_models.cpp
    runtime_storage.cpp
    selectionmodel.cpp
    directorymodel.cpp
    imagelistmodel.cpp
    workspacemodel.cpp
    analyzermodel.cpp
)
target_link_libraries(test_ui_models
    PRIVATE
        Qt6::Widgets
        Qt6::Core
)
target_compile_features(test_ui_models PRIVATE cxx_std_20)
target_include_directories(test_ui_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


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
foreach(_t decoder cache repository scheduler metadata filesystem eventbus render_pipeline tilecache pixelcontroller thumbnailpipeline commandstack crop job datamodel pluginregistry pluginregistryrunner m3acceptance analyzer_ext analyzer_batch raw_metadata rawdecode crashhandler pluginabi pluginexamples encoder imagetransform differenceengine blinkcontroller aligner imageoverlay browse_convergence m26_scheduler m26_metadata m26_thumbnail m26_repository m26_stats m27_thumbnail m27_eventbus m27_scheduler m27_repository)
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

# M41 cache thread-affinity regression inspects the process-wide Qt SQL
# connection registry directly.
target_link_libraries(test_cache PRIVATE Qt6::Sql)

# P5: headless release self-test gate. Runs the full application with --selftest,
# which decodes a generated image through the real product path and exits 0/1.
add_test(NAME selftest COMMAND MViewer --selftest)
set_tests_properties(selftest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# M24: version-consistency gate — CMake project(VERSION) is the single source
# of truth; fails if the generated version file, hard-coded literals, packaging
# scripts, or STATUS.md disagree with it.
add_test(NAME version_consistency
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/scripts/version_gate.ps1
            -SourceDir ${CMAKE_SOURCE_DIR}
            -BuildDir ${CMAKE_BINARY_DIR}
            -Version ${PROJECT_VERSION})
set_tests_properties(version_consistency PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# M35: the canonical local Test entry point must never turn a failing CTest
# run into a successful process exit. Keep this static contract in CTest so a
# future build-script edit cannot silently weaken every downstream gate.
add_test(NAME build_test_exit_gate
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/scripts/build_test_exit_gate.ps1
            -SourceDir ${CMAKE_SOURCE_DIR})
set_tests_properties(build_test_exit_gate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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

# ─── M15 P0#3 Analyzer Pipeline acceptance tests ──────────────────────────
add_executable(analyzer_pipeline_tests
    test_analyzer_pipeline.cpp
)

target_link_libraries(analyzer_pipeline_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(analyzer_pipeline_tests PRIVATE cxx_std_20)
target_include_directories(analyzer_pipeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(analyzer_pipeline_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME analyzer_pipeline_tests COMMAND analyzer_pipeline_tests)

# ─── M15 P0#1 Compare Workspace session round-trip ─────────────────────────
add_executable(compare_session_tests
    test_compare_session.cpp
)

target_link_libraries(compare_session_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(compare_session_tests PRIVATE cxx_std_20)
target_include_directories(compare_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(compare_session_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME compare_session_tests COMMAND compare_session_tests)

# ─── Beta 回归 — 用户 Workflow 端到端测试（2026-07 外部评审）─────────────────
# Workflow 1: 打开目录→浏览→键盘切换→放大→恢复→关闭（真实 MainWindow）
# Workflow 2: 两图 Compare→模式切换/互斥→Esc 退出→继续浏览（真实键盘事件）
# 映射 docs/beta_checklist.md 的 浏览体验 / Compare / View 条目。
add_executable(workflow_ux_tests
    test_workflow_ux.cpp
)

target_link_libraries(workflow_ux_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(workflow_ux_tests PRIVATE cxx_std_20)
target_include_directories(workflow_ux_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(workflow_ux_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME workflow_ux_tests COMMAND workflow_ux_tests)
# 端到端 UI 流程含真实异步解码等待，必须独占运行：
# 与 bench_* 并行时 CPU 饱和会把"首屏 ≤ 预算"检查变成噪声。
set_tests_properties(workflow_ux_tests PROPERTIES RUN_SERIAL TRUE)

# ─── M24: async lifetime & concurrency regressions ─────────────────────────
# Covers the ThumbnailPanel bounded-pool redesign: destroy-mid-scan, 50 rapid
# folder switches, widget-tree teardown during a large scan, view switch during
# dimension resolve, out-of-order completion, and pool quiescence.
add_executable(async_lifetime_tests
    test_async_lifetime.cpp
)

target_link_libraries(async_lifetime_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(async_lifetime_tests PRIVATE cxx_std_20)
target_include_directories(async_lifetime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME async_lifetime_tests COMMAND async_lifetime_tests)
set_tests_properties(async_lifetime_tests PROPERTIES RUN_SERIAL TRUE)

# ─── M24: Browse workflow acceptance (view modes / selection / filter / file
# ops / degraded inputs / metadata overlay currency) ─────────────────────────
add_executable(browse_acceptance_tests
    test_browse_acceptance.cpp
)

target_link_libraries(browse_acceptance_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(browse_acceptance_tests PRIVATE cxx_std_20)
target_include_directories(browse_acceptance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME browse_acceptance_tests COMMAND browse_acceptance_tests)
set_tests_properties(browse_acceptance_tests PROPERTIES RUN_SERIAL TRUE)

# ─── M45: real MainWindow CommandStack callback + async file-op surface ─────
add_executable(mainwindow_commandstack_acceptance
    test_mainwindow_commandstack.cpp
)

target_link_libraries(mainwindow_commandstack_acceptance
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(mainwindow_commandstack_acceptance PRIVATE cxx_std_20)
target_include_directories(mainwindow_commandstack_acceptance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME mainwindow_commandstack_acceptance COMMAND mainwindow_commandstack_acceptance)
set_tests_properties(mainwindow_commandstack_acceptance PROPERTIES RUN_SERIAL TRUE TIMEOUT 12)

# ─── M24: Compare workflow acceptance (multi-image entry, mode-preserved
# state, continuous pair navigation, degraded images) ────────────────────────
add_executable(compare_acceptance_tests
    test_compare_acceptance.cpp
)

target_link_libraries(compare_acceptance_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(compare_acceptance_tests PRIVATE cxx_std_20)
target_include_directories(compare_acceptance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_acceptance_tests COMMAND compare_acceptance_tests)
set_tests_properties(compare_acceptance_tests PROPERTIES RUN_SERIAL TRUE)

# ─── M24: Analyze workflow acceptance (throwing-analyzer isolation, result
# currency per image, history/pin persistence) ───────────────────────────────
add_executable(analyze_acceptance_tests
    test_analyze_acceptance.cpp
)

target_link_libraries(analyze_acceptance_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(analyze_acceptance_tests PRIVATE cxx_std_20)
target_include_directories(analyze_acceptance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME analyze_acceptance_tests COMMAND analyze_acceptance_tests)

# ─── M24: Export workflow acceptance (format fidelity, atomic writes,
# cancellation, degraded output dirs) ────────────────────────────────────────
add_executable(export_acceptance_tests
    test_export_acceptance.cpp
)

target_link_libraries(export_acceptance_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(export_acceptance_tests PRIVATE cxx_std_20)
target_include_directories(export_acceptance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME export_acceptance_tests COMMAND export_acceptance_tests)


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

# M21: unified ExportJob runner tests
add_executable(export_job_tests
    core/test_export_job.cpp
)
target_link_libraries(export_job_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(export_job_tests PRIVATE cxx_std_20)
target_include_directories(export_job_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

# ─── ICC profile parser regression (parseIccProfile robustness + BCD version) ──
add_executable(test_iccprofile
    core/test_iccprofile.cpp
)

target_link_libraries(test_iccprofile
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(test_iccprofile PRIVATE cxx_std_20)
target_include_directories(test_iccprofile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME iccprofile_tests COMMAND test_iccprofile)

# M36: display materialization contracts. Keeps analysis-domain bytes intact
# while asserting the CPU/GPU tile and ICC presentation helpers agree.
add_executable(test_m36_display
    core/test_m36_display.cpp
)
target_link_libraries(test_m36_display PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core)
target_compile_features(test_m36_display PRIVATE cxx_std_20)
target_include_directories(test_m36_display PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m36_display_tests COMMAND test_m36_display)

# M36: real QListView Ctrl/Shift selection and SelectionModel integration.
add_executable(m36_browse_tests
    core/test_m36_browse.cpp
)
target_link_libraries(m36_browse_tests PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core mviewer_ui)
target_compile_features(m36_browse_tests PRIVATE cxx_std_20)
target_include_directories(m36_browse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m36_browse_tests COMMAND m36_browse_tests)
set_tests_properties(m36_browse_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M15 Sprint 2-1 Search (global index + query engine) ─────────────────────
add_executable(search_tests
    test_search.cpp
)

target_link_libraries(search_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(search_tests PRIVATE cxx_std_20)
target_include_directories(search_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(search_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME search_tests COMMAND search_tests)

# M25: offline update-checker reliability and endpoint policy regressions.
add_executable(test_updatechecker
    test_updatechecker.cpp
)

target_link_libraries(test_updatechecker PRIVATE mviewer_core)
target_compile_features(test_updatechecker PRIVATE cxx_std_20)
target_include_directories(test_updatechecker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME updatechecker_tests COMMAND test_updatechecker)

# ─── M15 Sprint 2-2 Batch Workflow (batch processor + dialog) ────────────────
add_executable(batch_tests
    test_batch.cpp
)

target_link_libraries(batch_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(batch_tests PRIVATE cxx_std_20)
target_include_directories(batch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(batch_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME batch_tests COMMAND batch_tests)

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

# M20 P0#4 — Golden image regression gate: render output is compared against
# committed references in golden/ using PSNR / SSIM / pixel-diff thresholds.
add_test(NAME golden_image COMMAND golden_main --compare)

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

# M24 Phase 7: real-scale soak / resource-stability tool. NOT a CTest gate
# (keeps CI fast); run manually or on nightly:
#   build_msvc\bin\mviewer_m24_soak.exe [--out results.json]
add_executable(mviewer_m24_soak
    ${CMAKE_SOURCE_DIR}/benchmarks/m24_soak_main.cpp
)

target_link_libraries(mviewer_m24_soak
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
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

# M28 P0-01: clean-checkout reproducibility gate. testdata/ is git-ignored
# except for the tracked generator; this CTest proves a fresh checkout can
# rebuild and validate the fixture corpus (and that the script was not
# re-ignored). Skips only when no Python interpreter is available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME testdata_generator_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/testdata/generate_fixtures.py --ensure --size 256)
    set_tests_properties(testdata_generator_gate PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # M47 Phase 0: deterministic large-image fixture corpus must be generated
    # and validated in a clean checkout (generator is tracked; binaries stay
    # git-ignored).
    add_test(NAME large_fixture_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/testdata/generate_large_fixtures.py --ensure)
    set_tests_properties(large_fixture_gate PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 900)
endif()

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


# ─── Clang-Tidy (opt-in) ────────────────────────────────────────────────────
# M28 CI fix: auto-enabling CMAKE_CXX_CLANG_TIDY whenever clang-tidy happens
# to be on PATH made the Windows build depend on runner-local tooling
# (clang-tidy found inside VS -> every TU ran with MSVC flags and failed on
# M27 try blocks with "cannot use 'try' with exceptions disabled"; machines
# without clang-tidy built fine). Inline clang-tidy is now an explicit opt-in
# (MVIEWER_ENABLE_CLANG_TIDY=ON); the separate CI clang-tidy job
# (compile_commands + changed files) is unaffected.

option(MVIEWER_ENABLE_CLANG_TIDY
       "Run clang-tidy inline on every TU (opt-in; the CI clang-tidy job is the default gate)"
       OFF)
if(MVIEWER_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy inline: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "MVIEWER_ENABLE_CLANG_TIDY=ON but clang-tidy not found; inline checks disabled")
    endif()
endif()

# ─── ctest registration ──────────────────────────────────────────────────────

add_test(NAME core_tests       COMMAND core_tests)
add_test(NAME m3m4m5_tests     COMMAND test_m3m4m5)
add_test(NAME unit_tests       COMMAND mviewer_unit_tests)
add_test(NAME appstate_tests   COMMAND test_appstate)
add_test(NAME ui_models_tests  COMMAND test_ui_models)
add_test(NAME m3pipeline_tests COMMAND m3pipeline_tests)
add_test(NAME decoder_tests    COMMAND test_decoder)
add_test(NAME cache_tests      COMMAND test_cache)
add_test(NAME repository_tests COMMAND test_repository)
add_test(NAME scheduler_tests  COMMAND test_scheduler)
add_test(NAME m26_scheduler_tests COMMAND test_m26_scheduler)
add_test(NAME m26_metadata_tests COMMAND test_m26_metadata)
add_test(NAME m26_thumbnail_tests COMMAND test_m26_thumbnail)
add_test(NAME m26_repository_tests COMMAND test_m26_repository)
add_test(NAME m26_stats_tests COMMAND test_m26_stats)
add_test(NAME m27_thumbnail_tests COMMAND test_m27_thumbnail)
add_test(NAME m27_eventbus_tests COMMAND test_m27_eventbus)
add_test(NAME m27_scheduler_tests COMMAND test_m27_scheduler)
add_test(NAME m27_repository_tests COMMAND test_m27_repository)
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
add_test(NAME commandstack_callback_regression
    COMMAND test_commandstack --callback-probe)
set_tests_properties(commandstack_callback_regression PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 3)
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
add_test(NAME export_job_tests COMMAND export_job_tests)

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
add_test(NAME encoder_tests COMMAND test_encoder)
add_test(NAME imagetransform_tests COMMAND test_imagetransform)
add_test(NAME differenceengine_tests COMMAND test_differenceengine)
add_test(NAME blinkcontroller_tests COMMAND test_blinkcontroller)
add_test(NAME aligner_tests COMMAND test_aligner)
add_test(NAME imageoverlay_tests COMMAND test_imageoverlay)
add_test(NAME browse_convergence_tests COMMAND test_browse_convergence)

# ─── M25: Browse pipeline convergence (UI level) — thumbnail cache identity,
# pipeline size/generation semantics, panel size-switch and filter behaviors ─
add_executable(browse_convergence_ui_tests
    test_browse_convergence.cpp
)

target_link_libraries(browse_convergence_ui_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(browse_convergence_ui_tests PRIVATE cxx_std_20)
target_include_directories(browse_convergence_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME browse_convergence_ui_tests COMMAND browse_convergence_ui_tests)
set_tests_properties(browse_convergence_ui_tests PROPERTIES RUN_SERIAL TRUE)

# --- M27: QObject async lifetime closure regression (UI level) ---
# Child-process destroy-mid-decode crash detection (PreviewPanel / ImageViewer),
# A->B->A newest-generation delivery, rejection terminal state, and the P9 24MP
# UI completion-latency bounds. Heavy but bounded; runs serial.
add_executable(m27_lifetime_tests
    test_m27_lifetime.cpp
)
target_link_libraries(m27_lifetime_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m27_lifetime_tests PRIVATE cxx_std_20)
target_include_directories(m27_lifetime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m27_lifetime_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m27_lifetime_tests COMMAND m27_lifetime_tests)
set_tests_properties(m27_lifetime_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

# --- M27: close/shutdown lifecycle torture (100 rounds) ---
# Real MainWindow open -> browse -> viewer A/B/A -> compare diff -> destroy,
# with per-round subsystem convergence and RSS/handle/thread leak checks.
add_executable(m27_lifecycle_torture
    test_m27_lifecycle.cpp
)
target_link_libraries(m27_lifecycle_torture
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m27_lifecycle_torture PRIVATE cxx_std_20)
target_include_directories(m27_lifecycle_torture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m27_lifecycle_torture PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m27_lifecycle_torture COMMAND m27_lifecycle_torture)
set_tests_properties(m27_lifecycle_torture PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800)

# ─── M46: async lifetime / cancellation delivery-gate contract (core) ──────
# Deterministic interleavings (decode-done vs cancel vs consumer-death) built
# with latches + the repository delivery-gate hooks, never sleep-based timing.
add_executable(m46_repository_tests
    core/test_m46_repository.cpp
)
target_link_libraries(m46_repository_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m46_repository_tests PRIVATE cxx_std_20)
target_include_directories(m46_repository_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m46_repository_tests COMMAND m46_repository_tests)
set_tests_properties(m46_repository_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ─── M46: ThumbnailPipeline mid-flight resize supersession (core) ───────────
add_executable(m46_thumbnail_tests
    core/test_m46_thumbnail.cpp
)
target_link_libraries(m46_thumbnail_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m46_thumbnail_tests PRIVATE cxx_std_20)
target_include_directories(m46_thumbnail_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m46_thumbnail_tests COMMAND m46_thumbnail_tests)
set_tests_properties(m46_thumbnail_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ─── M47 Phase 1: source-backed image abstraction ────────────────────────────
# Capability contract (probe without decode, native-LOD, bounded region,
# native-vs-fallback classification) over the deterministic large corpus.
add_executable(m47_source_tests
    core/test_m47_source.cpp
)
target_link_libraries(m47_source_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m47_source_tests PRIVATE cxx_std_20)
target_include_directories(m47_source_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_source_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_source_tests COMMAND m47_source_tests)
set_tests_properties(m47_source_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ─── M53 Phase 0/1/2: large-source parity and bounded RAW preview ───────────
# Tests-first characterization: this target freezes the large-source contract
# established red on the M52 baseline and now runs as a release regression gate.
add_executable(m53_large_source_tests
    core/test_m53_large_source.cpp
)
target_link_libraries(m53_large_source_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m53_large_source_tests PRIVATE cxx_std_20)
target_include_directories(m53_large_source_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m53_large_source_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m53_large_source_tests COMMAND m53_large_source_tests)
set_tests_properties(m53_large_source_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# M53 Native Windows qualification: exercise the real Viewer and Compare UI
# over the 100MP TIFF fixture, including zoom-region refresh and mode churn.
add_executable(m53_large_source_ui_tests
    test_m53_large_source_ui.cpp
)
target_link_libraries(m53_large_source_ui_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m53_large_source_ui_tests PRIVATE cxx_std_20)
target_include_directories(m53_large_source_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m53_large_source_ui_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m53_large_source_ui_tests COMMAND m53_large_source_ui_tests)
set_tests_properties(m53_large_source_ui_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

# M53 release soak: repeat large-TIFF Viewer/Compare destruction and
# latest-generation replacement to qualify long-session cleanup.
add_executable(m53_large_source_soak
    test_m53_large_source_soak.cpp
)
target_link_libraries(m53_large_source_soak
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m53_large_source_soak PRIVATE cxx_std_20)
target_include_directories(m53_large_source_soak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m53_large_source_soak PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m53_large_source_soak COMMAND m53_large_source_soak)
set_tests_properties(m53_large_source_soak PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800)

# ─── M54: perceived-performance Browse E2E benchmark/regression ────────────
# Measures the real ThumbnailPanel + PreviewPanel path at 100/1000/10000
# images and a 50k-entry directory. The output is copied into the M54 review
# baseline/final documents; the target remains a release test-gate executable
# so a future browse regression is visible in the normal local Test run.
add_executable(m54_perceived_performance_tests
    test_m54_perceived_performance.cpp
)
target_link_libraries(m54_perceived_performance_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m54_perceived_performance_tests PRIVATE cxx_std_20)
target_include_directories(m54_perceived_performance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m54_perceived_performance_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m54_perceived_performance_tests COMMAND m54_perceived_performance_tests)
set_tests_properties(m54_perceived_performance_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800)

# ─── M55: bounded UI raster cache / interactive navigation regressions ──────
# Drives a real ThumbnailPanel over a 420-image corpus and verifies the
# QPixmap working set remains bounded while the viewport visits disjoint rows.
add_executable(m55_interactive_tests
    test_m55_interactive.cpp
)
target_link_libraries(m55_interactive_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m55_interactive_tests PRIVATE cxx_std_20)
target_include_directories(m55_interactive_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m55_interactive_tests COMMAND m55_interactive_tests)
set_tests_properties(m55_interactive_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# Standalone recorder for M55 10k/50k memory and navigation evidence. It is
# intentionally not a gate because its numbers are machine-dependent.
add_executable(m55_interactive_baseline
    test_m55_interactive_baseline.cpp
)
target_link_libraries(m55_interactive_baseline
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m55_interactive_baseline PRIVATE cxx_std_20)
target_include_directories(m55_interactive_baseline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ─── M47 Phase 2: Viewer LOD-first display (UI level) ─────────────────────────
# Large sources display through bounded LOD/region rasters (no full-resolution
# materialization for fit-to-window), with supersession/destroy safety and
# idle convergence; the small-image fast path is preserved.
add_executable(m47_viewer_lod_tests
    test_m47_viewer_lod.cpp
)
target_link_libraries(m47_viewer_lod_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_viewer_lod_tests PRIVATE cxx_std_20)
target_include_directories(m47_viewer_lod_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_viewer_lod_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_viewer_lod_tests COMMAND m47_viewer_lod_tests)
set_tests_properties(m47_viewer_lod_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# ─── M47 Phase 3: Compare source-backed display (UI level) ───────────────────
# Infeasible sources (full RGB > the analysis limit) keep their pane via a
# metadata-only placeholder and display through the source-backed LOD path;
# failures vs skips are accounted separately; zoom re-materializes at a denser
# edge; TIFF honesty and destroy safety are covered.
add_executable(m47_compare_lod_tests
    test_m47_compare_lod.cpp
)
target_link_libraries(m47_compare_lod_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_compare_lod_tests PRIVATE cxx_std_20)
target_include_directories(m47_compare_lod_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_compare_lod_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_compare_lod_tests COMMAND m47_compare_lod_tests)
set_tests_properties(m47_compare_lod_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# M52: pure source-backed Compare display planning regression matrix. The
# planner owns no widget or decoder state, so this remains deterministic and
# proves placeholder/Fit/zoom/pan/DPR/crop/rotation/supersession semantics.
add_executable(compare_display_planner_tests
    test_compare_display_planner.cpp
)
target_link_libraries(compare_display_planner_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_display_planner_tests PRIVATE cxx_std_20)
target_include_directories(compare_display_planner_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_display_planner_tests COMMAND compare_display_planner_tests)
set_tests_properties(compare_display_planner_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M47 Phase 4: exact-source consumers with source-backed panes ────────────
# Inspector / diff / PSNR / SSIM / export keep full-resolution correctness when
# source-backed (LOD) panes are present: they never sample the display LOD and
# never trigger implicit materialization; infeasible panes degrade to invalid /
# non-comparable states instead.
add_executable(m47_exact_source_tests
    test_m47_exact_source.cpp
)
target_link_libraries(m47_exact_source_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_exact_source_tests PRIVATE cxx_std_20)
target_include_directories(m47_exact_source_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_exact_source_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_exact_source_tests COMMAND m47_exact_source_tests)
set_tests_properties(m47_exact_source_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# ─── M47 Phase 5: transactional async Workspace/Project restore (UI level) ───
# openWorkspaceFile/openProjectFile read + deserialize on a background worker
# and apply the parsed document atomically for the current generation only:
# failed opens never touch the live session, newer opens supersede in-flight
# ones, and destroy mid-restore is safe.
add_executable(m47_restore_tests
    test_m47_restore.cpp
)
target_link_libraries(m47_restore_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_restore_tests PRIVATE cxx_std_20)
target_include_directories(m47_restore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_restore_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_restore_tests COMMAND m47_restore_tests)
set_tests_properties(m47_restore_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ─── M47 Phase 6: deterministic large-image soak (UI level) ──────────────────
# Repeated 100MP-class display churn through the Viewer LOD-first path and the
# Compare source-backed panes: open -> ready -> zoom churn -> A/B/A supersede,
# destroy mid-request on alternating rounds, per-round pool drain + bounded
# RSS, zero full-decode fallbacks, and a converged scheduler dependency graph.
add_executable(m47_large_soak
    test_m47_large_soak.cpp
)
target_link_libraries(m47_large_soak
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_large_soak PRIVATE cxx_std_20)
target_include_directories(m47_large_soak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_large_soak PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_large_soak COMMAND m47_large_soak)
set_tests_properties(m47_large_soak PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# ─── M48 Phase 0: deterministic regression gate (UI level) ───────────────────
# Freeze the M48 contracts BEFORE implementation: ICC display fidelity across
# full-frame / viewer-LOD / compare source-backed paths, EXIF orientation
# region-coordinate correctness (oriented vs raw), LOD-only resize/fullscreen,
# Compare deep-zoom density (no whole-image LOD ceiling), placeholder metadata
# completeness, and decoder exception terminal states. Cases are expected to
# FAIL until the corresponding M48 Phase 1/2 fixes land.
add_executable(m48_phase0_regressions
    test_m48_phase0.cpp
)
target_link_libraries(m48_phase0_regressions
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m48_phase0_regressions PRIVATE cxx_std_20)
target_include_directories(m48_phase0_regressions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m48_phase0_regressions PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m48_phase0_regressions COMMAND m48_phase0_regressions)
# M48 Phase 0 was frozen as RED before the implementation work. The ICC,
# oriented-region, deep-zoom density, metadata, and decoder-terminal contracts
# are now implemented, so keep this suite as a normal green release gate.
set_tests_properties(m48_phase0_regressions PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

add_executable(m48_phase0_failure_regressions
    test_m48_phase0_failures.cpp
)
target_link_libraries(m48_phase0_failure_regressions
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m48_phase0_failure_regressions PRIVATE cxx_std_20)
target_include_directories(m48_phase0_failure_regressions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m48_phase0_failure_regressions PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m48_phase0_failure_regressions COMMAND m48_phase0_failure_regressions)
set_tests_properties(m48_phase0_failure_regressions PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

# ─── M46: Browse real-world reliability (UI level) ──────────────────────────
# Paint-path filesystem freedom (render-identical after source deletion),
# scan/dimension supersession bounds, busy-cursor ownership, and deterministic
# viewer/compare destroy + A->B->A races.
add_executable(m46_browse_tests
    test_m46_browse.cpp
)
target_link_libraries(m46_browse_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m46_browse_tests PRIVATE cxx_std_20)
target_include_directories(m46_browse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m46_browse_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m46_browse_tests COMMAND m46_browse_tests)
set_tests_properties(m46_browse_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# ─── M46: crash-safe persistence (atomic replace + fault injection) ─────────
add_executable(m46_persistence_tests
    core/test_m46_persistence.cpp
)
target_link_libraries(m46_persistence_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m46_persistence_tests PRIVATE cxx_std_20)
target_include_directories(m46_persistence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m46_persistence_tests COMMAND m46_persistence_tests)
set_tests_properties(m46_persistence_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ─── M46: architecture gate regression ──────────────────────────────────────
# Proves the gate flags a direct Repository include from a UI/Compare TU and
# that the current tree reports zero warnings (the stale imageviewer/preview
# loading-boundary exemption is gone — loading converges on the facade).
add_test(NAME architecture_gate_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/architecture_gate_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(architecture_gate_regression PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# M52: complexity responsibility-TU guardrail regression. A planted 1001-line
# responsibility file must fail, while two legal sub-1000-line TUs remain
# visible as warnings and pass the hard gate.
add_test(NAME complexity_gate_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/complexity_gate_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(complexity_gate_regression PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# M52: execute the generators through Windows PowerShell 5.1 and validate the
# emitted files as strict UTF-8 without common mojibake tokens.
add_test(NAME script_encoding_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/script_encoding_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(script_encoding_regression PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M46: real-workflow soak (short CI form; --extended for Release) ────────
# One workload driver, two durations: the CTest here runs the short form; the
# extended manual qualification uses --extended (see benchmarks/m46_workflow_soak_main.cpp).
add_executable(mviewer_m46_workflow_soak
    ${CMAKE_SOURCE_DIR}/benchmarks/m46_workflow_soak_main.cpp
)
target_link_libraries(mviewer_m46_workflow_soak
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(mviewer_m46_workflow_soak PRIVATE cxx_std_20)
target_include_directories(mviewer_m46_workflow_soak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mviewer_m46_workflow_soak PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m46_workflow_soak COMMAND mviewer_m46_workflow_soak --iterations 8)
set_tests_properties(m46_workflow_soak PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

# M51: Release Candidate qualification extends the real product-loop soak
# beyond the short regression form. The 20-iteration CTest gate is the
# repeatable CI qualification; --extended remains available for 40 iterations
# on a physical Windows target.
add_test(NAME m51_rc_soak COMMAND mviewer_m46_workflow_soak --iterations 20)
set_tests_properties(m51_rc_soak PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800)

# ─── M50: directory-transition + async Sidecar convergence  ────────────────
# Exercises the production MainWindow/DirectoryTree signal path with Unicode
# paths, active rating filtering, Back/Forward, path edit, and teardown while
# background import work is owned by the scheduler.
add_executable(m50_navigation_tests
    test_m50_navigation.cpp
)
target_link_libraries(m50_navigation_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m50_navigation_tests PRIVATE cxx_std_20)
target_include_directories(m50_navigation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m50_navigation_tests COMMAND m50_navigation_tests)
set_tests_properties(m50_navigation_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# M50: command-line path classification must keep Unix absolute paths openable
# while preserving slash-prefixed Windows switches.
add_executable(commandline_tests
    test_commandline.cpp
)
target_link_libraries(commandline_tests PRIVATE Qt6::Core mviewer_application)
target_compile_features(commandline_tests PRIVATE cxx_std_20)
target_include_directories(commandline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME commandline_tests COMMAND commandline_tests)

# M51: package/installer version identity must be deterministic and must not
# silently fall back to NSIS's development version.
add_test(NAME release_version_tests
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/release_version_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(release_version_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME release_contract_tests
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/release_contract_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(release_contract_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

# ─── M47 Phase 0: large-image data-path BASELINE recorder ───────────────────
# Records the pre-Phase-1 behavior of the current data path (decode entry
# points, latency, materialized bytes, RSS) for the large fixture corpus.
# No pass/fail thresholds here — it produces the baseline that Phase 6 gates
# will compare against (see benchmarks/m47_large_image_baseline_main.cpp).
add_executable(m47_large_image_baseline
    ${CMAKE_SOURCE_DIR}/benchmarks/m47_large_image_baseline_main.cpp
)
target_link_libraries(m47_large_image_baseline
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m47_large_image_baseline PRIVATE cxx_std_20)
target_include_directories(m47_large_image_baseline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m47_large_image_baseline PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m47_large_image_baseline
    COMMAND m47_large_image_baseline
            --out ${CMAKE_SOURCE_DIR}/benchmark/report/large_image_baseline_phase0.json)
set_tests_properties(m47_large_image_baseline PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

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
    core/perf/GpuTracker.cpp
)

target_link_libraries(mviewer_bench
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        mviewer_core
        dxgi
        ole32
)

target_compile_features(mviewer_bench PRIVATE cxx_std_20)
target_include_directories(mviewer_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mviewer_bench PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME bench_smoke COMMAND mviewer_bench --smoke)

# M13.3 — Performance Budget as a HARD gate (data-driven via JSON).
# Runs the same binary in --enforce mode against
# benchmark/performance_budget.json. On a budget violation the binary exits
# non-zero and THIS ctest FAILS (so `.\build.ps1 Test` cannot go green
# with a perf regression). The PR-level ci.yml `test` job runs this same
# invocation against the committed baseline.
add_test(NAME bench_enforce
    COMMAND mviewer_bench --enforce
            --profile auto
            --budget ${CMAKE_SOURCE_DIR}/benchmark/performance_budget.json)

# Benchmark timings must not share CPU time with other CTest cases. The
# profile adapts the budget for small local machines; RUN_SERIAL keeps the
# enforced measurement itself free from test-suite contention. The smoke test
# remains parallelizable because it only proves the harness can execute.
set_tests_properties(bench_enforce PROPERTIES RUN_SERIAL TRUE)

# M41: every CTest process gets a fresh, per-test runtime root before Qt or
# QStandardPaths is initialized. The random configure id makes repeated
# `build.ps1 Test` runs independent, while the test name prevents parallel
# processes from sharing AppData, CacheLocation, SQLite, or TEMP files.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef MVIEWER_TEST_RUN_ID)
set(MVIEWER_TEST_RUNTIME_BASE "${CMAKE_CURRENT_BINARY_DIR}/test-runtime/${MVIEWER_TEST_RUN_ID}")
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/test-runtime")
get_property(MVIEWER_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
foreach(_mviewer_test IN LISTS MVIEWER_REGISTERED_TESTS)
    string(MAKE_C_IDENTIFIER "${_mviewer_test}" _mviewer_test_id)
    set(_mviewer_root "${MVIEWER_TEST_RUNTIME_BASE}/${_mviewer_test_id}")
    file(MAKE_DIRECTORY
        "${_mviewer_root}/appdata"
        "${_mviewer_root}/localappdata"
        "${_mviewer_root}/temp"
        "${_mviewer_root}/config"
        "${_mviewer_root}/data"
        "${_mviewer_root}/cache")
    set_tests_properties(${_mviewer_test} PROPERTIES
        ENVIRONMENT
        "MVIEWER_TEST_RUNTIME_ROOT=${_mviewer_root};APPDATA=${_mviewer_root}/appdata;LOCALAPPDATA=${_mviewer_root}/localappdata;TEMP=${_mviewer_root}/temp;TMP=${_mviewer_root}/temp;XDG_CONFIG_HOME=${_mviewer_root}/config;XDG_DATA_HOME=${_mviewer_root}/data;XDG_CACHE_HOME=${_mviewer_root}/cache")
endforeach()

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
