include(${CMAKE_SOURCE_DIR}/cmake/LibRaw.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/LibJpegTurbo.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/MViewerTesting.cmake)

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
    core/async/CooperativeCancel.cpp
    core/EventBus.cpp
    core/FileIdentity.cpp
    core/MainThreadDispatcher.cpp
    core/image/Decoder.cpp
    core/image/decoder/QtDecoder.cpp
    core/image/decoder/QtDecoder_wic.cpp
    core/image/decoder/QtDecoder_bmp.cpp
    core/image/decoder/QtDecoder_png.cpp
    core/image/decoder/QtDecoder_tiff.cpp
    core/image/decoder/JpegTurbo.cpp
    core/image/decoder/QtDecoder_meta.cpp
    core/image/decoder/QtFallbackDecoder.cpp
    core/image/decoder/DecoderRegistry.cpp
    core/image/decoder/RawDecoder.cpp
    core/image/decoder/YuvDecoder.cpp
    core/image/decoder/YuvParallelRows.cpp
    core/image/decoder/PlainRawDecoder.cpp
    core/image/decoder/HdrDecoder.cpp
    core/image/decoder/RawDecoder_libraw.cpp
    core/image/decoder/RawDecodePolicy.cpp
    core/image/DisplayColorContext.h
    core/image/DisplayAdjust.cpp
    core/image/ImageObject.cpp
    core/image/ImageFrame.cpp
    core/image/ImageRepository.cpp
    core/image/ImageRepository_async.cpp
    core/image/ImageRepository_dir.cpp
    core/image/ImageRepository_load.cpp
    core/image/ImageRepository_frames.cpp
    core/image/SourceImage.cpp
    core/image/DisplayMip.cpp
    core/image/IccProfile.cpp
    core/image/MetadataReader.cpp
    core/image/MetadataDpi.cpp
    core/image/RawMetadata.cpp
    core/image/ImageCache.cpp
    core/memory/MemoryBudget.cpp
    core/image/DiskCache.cpp
    core/cache/CacheManager.cpp
    core/cache/MipmapPyramid.cpp
    core/image/Encoder.cpp
    core/image/ImageFormats.cpp
    core/image/FrameSequence.cpp
    core/image/FrameSequence_yuv.cpp
    core/image/yuv/YuvFormat.cpp
    core/image/yuv/YuvPixFmtGeneric.cpp
    core/image/yuv/YuvExtensionMap.cpp
    core/image/yuv/Y4mHeader.cpp
    core/image/yuv/YuvParamInference.cpp
    core/image/yuv/YuvUnpack.cpp
    core/image/yuv/YuvToRgb.cpp
    core/image/yuv/YuvToRgb_simd.cpp
    core/image/yuv/YuvSource.cpp
    core/image/yuv/YuvScaledRender.cpp
    core/image/yuv/YuvFile.cpp
    core/image/yuv/YuvSampleProbe.cpp
    core/image/rawplain/PlainRawFormat.cpp
    core/image/rawplain/PlainRawFormatText.cpp
    core/image/rawplain/PlainRawInference.cpp
    core/image/rawplain/PlainRawUnpack.cpp
    core/image/rawplain/PlainRawRender.cpp
    core/image/rawplain/PlainRawPreview.cpp
    core/image/rawplain/PlainRawWhiteBalance.cpp
    core/image/rawplain/PlainRawSample.cpp
    core/image/rawplain/PlainRawFile.cpp
    core/image/rawplain/PlainRawStride.cpp
    core/image/FrameSequenceProbe.cpp
    core/image/FramePlaybackController.cpp
    core/image/ImageTransform.cpp
    core/image/ImageFileRotate.cpp
    core/image/ImageFileReplace.cpp
    core/image/JpegExif.cpp
    core/image/QtConvert.cpp
    core/image/DitherQuantize.cpp
    core/image/ImageStats.cpp
    core/image/UsmSharpen.cpp
    core/image/CellImageAdjust.cpp
    core/filesystem/FileSystem.cpp
    core/filesystem/DirectorySnapshot.cpp
    core/filesystem/AtomicFile.cpp
    core/filesystem/Utf8Path.cpp
    core/scheduler/TaskScheduler.cpp
    core/scheduler/TaskScheduler_state.cpp
    core/compare/CompareEngine.cpp
    core/compare/SyncController.cpp
    core/compare/BlinkController.cpp
    core/compare/DifferenceEngine.cpp
    core/compare/DifferenceEngine_render.cpp
    core/compare/Aligner.cpp
    core/RecentStore.cpp
    core/FileMetadataRelocation.cpp
    core/CrashHandler.cpp
    core/Logger.cpp
    core/MemoryTrim.cpp
    core/SelfTest.cpp
    core/SettingsIO.cpp
    core/analysis/ImageOverlay.cpp
    core/analysis/AnalysisEngine.cpp
    core/analysis/AnalysisEngine_metrics.cpp
    core/analysis/YuvMetrics.cpp
    core/analysis/PixelInspector.cpp
    core/analysis/PixelInspector_adjust.cpp
    core/analysis/Ciede2000.cpp
    core/analysis/ColorChecker.cpp
    core/analysis/Fft2D.cpp
    core/session/SessionPackage.cpp
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
    core/render/BilinearScale.cpp
    core/render/DisplayResample.cpp
    core/render/DisplayResampleScalar.cpp
    core/render/DisplayResampleThread.cpp
    core/render/JpegBlockTiles.cpp
    core/render/DisplayResamplePyramid.cpp
    core/render/DisplayResampleSimd.cpp
    core/render/AsyncTileRequestManager.cpp
    core/thumbnail/ThumbnailPipeline.cpp
    core/thumbnail/ThumbnailPipeline.h
    core/plugin/PluginLibrary.cpp
    core/plugin/PluginLoader.cpp
    core/plugin/PluginManager.cpp
    core/plugin/PluginPathPolicy.cpp
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
    core/batch/BatchRename.cpp
    core/batch/BatchRenameInPlace.cpp
    core/update/UpdateChecker.cpp
    core/update/UpdatePrompt.cpp

    core/command/CommandRegistry.cpp
    core/command/RotateCommand.cpp
    core/command/CropCommand.cpp
    core/command/LabelCommand.cpp
    core/command/OpenDirectoryCommand.cpp
    core/command/CompareCommand.cpp
    core/command/RenameCommand.cpp
    core/command/DeleteCommand.cpp
    core/command/FileDeleteCommand.cpp
    core/command/RotateFilesCommand.cpp
    core/command/FileRenameCommand.cpp
    core/command/FileMoveCommand.cpp
    core/command/FileCopyCommand.cpp
    core/command/FileSystemAdapter.cpp
    core/command/ToggleHistogramCommand.cpp
    core/command/CallbackCommand.cpp
    core/command/ClipboardPasteCommand.cpp
    core/benchmark/Benchmark.cpp
    core/perf/MemoryTracker.cpp
    core/trace/TraceSink.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql OpenGL OpenGLWidgets)
# Optional. The normal build does not run lrelease; the committed .qm files are
# the source of truth. Invoke this target only when LinguistTools is installed.
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
    add_custom_target(update_translations_qm
        COMMAND Qt6::lrelease
                ${CMAKE_SOURCE_DIR}/translations/mviewer_zh_CN.ts
                -qm ${CMAKE_SOURCE_DIR}/resources/i18n/mviewer_zh_CN.qm
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/resources/i18n/mviewer_zh_CN.qm
                ${CMAKE_SOURCE_DIR}/translations/mviewer_zh_CN.qm
        COMMENT "Regenerate resources/i18n/mviewer_zh_CN.qm from translations/mviewer_zh_CN.ts"
        VERBATIM)
endif()
# psapi: provides GetProcessMemoryInfo used by core/perf/MemoryTracker for the
# OS working-set (RSS) sample. Windows-only; harmless on other platforms.
# Gui/Core explicitly: src/core/.cpp bodies use QImage & friends, and pulling
# them transitively through Qt6::Widgets was the only reason Widgets was linked.
target_link_libraries(mviewer_core PRIVATE Qt6::Gui Qt6::Core Qt6::Sql
    $<$<PLATFORM_ID:Windows>:psapi;windowscodecs;ole32>)
if(MVIEWER_HAS_LIBRAW)
    target_link_libraries(mviewer_core PRIVATE mviewer_libraw)
    target_compile_definitions(mviewer_core PRIVATE MVIEWER_HAS_LIBRAW=1)
endif()
if(MVIEWER_HAS_TURBOJPEG)
    target_link_libraries(mviewer_core PRIVATE mviewer_turbojpeg)
    target_compile_definitions(mviewer_core PRIVATE MVIEWER_HAS_TURBOJPEG=1)
    message(STATUS "MViewer: JPEG fast path via libjpeg-turbo")
endif()

# Perf review P2-4: bounded PNG LOD through the libpng Qt already ships
# (Qt6::BundledLibpng, static). Optional: distro Qt packages without it
# (static-analysis jobs) keep the QImageReader path.
find_package(Qt6BundledLibpng QUIET CONFIG PATHS "${Qt6_DIR}/.." NO_DEFAULT_PATH)
find_path(MVIEWER_QTPNG_INCLUDE_DIR png.h PATHS "${Qt6_DIR}/../../../include/QtPng" NO_DEFAULT_PATH)
if(TARGET Qt6::BundledLibpng AND MVIEWER_QTPNG_INCLUDE_DIR)
    target_link_libraries(mviewer_core PRIVATE Qt6::BundledLibpng)
    target_include_directories(mviewer_core PRIVATE "${MVIEWER_QTPNG_INCLUDE_DIR}")
    target_compile_definitions(mviewer_core PRIVATE MVIEWER_HAS_PNG_LOD=1)
    message(STATUS "MViewer: bounded PNG LOD via Qt6::BundledLibpng")
else()
    message(STATUS "MViewer: Qt6::BundledLibpng not found; PNG LOD uses QImageReader")
endif()

target_compile_features(mviewer_core PUBLIC cxx_std_20)

# Plugin systems require a single, shared core so host and plugin share one
# Analyzer/Command vtable (a static core would give each module its own vtable
# copy and crash on cross-module delete / teardown). Auto-export all symbols
# so consumers link without per-symbol declspec markup.
set_target_properties(mviewer_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_dependencies(mviewer_core mviewer_version_stamp)
mviewer_release_pdb(mviewer_core)

target_include_directories(mviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${MVIEWER_GENERATED_INCLUDE_DIR})

add_library(mviewer_ui STATIC
    mainwindow.cpp
    mainwindow_ui.cpp
    mainwindow_ui_layout.cpp
    mainwindow_ui_toolbar.cpp
    mainwindow_ui_retranslate.cpp
    mainwindow_triplebrowse.cpp
    mainwindow_triplebrowse_ui.cpp
    mainwindow_triplebrowse_state.cpp
    mainwindow_triplebrowse_nav.cpp
    mainwindow_splitbrowse.cpp
    mainwindow_splitbrowse_panes.cpp
    mainwindow_split_transfer.cpp
    mainwindow_splitbrowse_nav.cpp
    mainwindow_emptystate.cpp
    mainwindow_ui_chrome.cpp
    mainwindow_ui_menus.cpp
    mainwindow_filetransform.cpp
    mainwindow_transformsupport.cpp
    mainwindow_yuv.cpp
    mainwindow_ui_connections.cpp
    mainwindow_ui_connections_browse.cpp
    mainwindow_commands.cpp
    mainwindow_navigation.cpp
    mainwindow_session.cpp
    mainwindow_session_recovery.cpp
    mainwindow_session_notifications.cpp
    mainwindow_sessionpackage.cpp
    mainwindow_export.cpp
    mainwindow_view.cpp
    mainwindow_keys.cpp
    mainwindow_viewer_fileops.cpp
    mainwindow_selection.cpp
    selectionmodel.cpp
    directorymodel.cpp
    imagelistmodel.cpp
    workspacemodel.cpp
    analyzermodel.cpp
    appstate.cpp
    imageviewer.cpp
    imageviewer_scope.cpp
    imageviewer_annotate.cpp
    imageviewer_adjust.cpp
    imageviewer_colorchecker.cpp
    imageviewer_spectrum.cpp
    cvoverlay.cpp
    imageviewer_overlays.cpp
    imageviewer_inspector.cpp
    imageviewer_state.cpp
    annotationstore.cpp
    imageviewer_loading.cpp
    imageviewer_browse.cpp
    imageviewer_multiframe.cpp
    imageviewer_lod.cpp
    imageviewer_displaycolor.cpp
    imageviewer_queuedecode.cpp
    imageviewer_paint.cpp
    imageviewer_transition.cpp
    imageviewer_rawdecode.cpp
    imageviewer_loadingbar.cpp
    imageviewer_region_tiles.cpp
    imageviewer_contextmenu.cpp
    imageviewer_selection.cpp
    imageviewer_transform.cpp
    directorytree.cpp
    directorytree_navigate.cpp
    directorymonitor.cpp
    breadcrumbbar.cpp
    metadataoverlay.cpp
    thumbnailpanel.cpp
    thumbnailpanel_live.cpp
    naturalnameorder.cpp
    thumbnailpanel_async.cpp
    thumbnailpanel_scan_guard.cpp
    thumbnailpanel_scan_publish.cpp
    thumbnailpanel_selection.cpp
    thumbnailpanel_pipeline.cpp
    thumbnailpanel_filters.cpp
    gallery_filter_engine.cpp
    thumbnailpanel_typehint.cpp
    browseformats.cpp
    formatfiltercombo.cpp
    thumbnailpanel_fileops.cpp
    thumbnailpanel_batch_export.cpp
    thumbnailpanel_dimensions.cpp
    thumbnailtrash.cpp
    thumbnailpanel_paste.cpp
    thumbnailpanel_transfer.cpp
    thumbnailcard_style.cpp
    thumbnailpanel_delegates.cpp
    thumbnailpanel_delegates_details.cpp
    thumbnailpanel_viewmode.cpp
    thumbnailpanel_contextmenu.cpp
    thumbnailprovider.cpp
    display/DisplayColorContextProvider.cpp
    display/DisplayColorContextProvider_notify.cpp
    display/DisplayProfileWatcher.cpp
    previewpanel.cpp
    previewpanel_paint.cpp
    previewpanel_submit.cpp
    thumbnailcache.cpp
    compareworkspace.cpp
    compareworkspace_display_planner.cpp
    compareworkspace_display_state.cpp
    compareworkspace_display_pyramid.cpp
    compare_session_frame_pool.cpp
    compare_session_state.cpp
    compare_roi_controller.cpp
    compare_diff_renderer.cpp
    rendercommandpainter.cpp
    compare_display_manager.cpp
    compareworkspace_controls.cpp
    compareworkspace_controls_overlay.cpp
    compareworkspace_toolbar.cpp
    compareworkspace_pane_header.cpp
    compareworkspace_render.cpp
    compareworkspace_render_materialization.cpp
    compareworkspace_adjusted_export.cpp
    compareworkspace_composite_gl.cpp
    compareworkspace_composite_paint.cpp
    compareworkspace_render_canvas_paint.cpp
    compareworkspace_pyramid_wire.cpp
    compareworkspace_render_diff.cpp
    compareworkspace_live_diff.cpp
    compareworkspace_render_canvas.cpp
    compareworkspace_roi.cpp
    compare_roi_stats_presenter.cpp
    compareworkspace_roi_overlay.cpp
    compareworkspace_temporary.cpp
    compareworkspace_load.cpp
    compareworkspace_layout.cpp
    compareworkspace_progressive.cpp
    compareworkspace_analysis.cpp
    compareworkspace_editpanel.cpp
    compareworkspace_adjust_export.cpp
    compareworkspace_help.cpp
    compareworkspace_interact.cpp
    compareworkspace_pixellink.cpp
    compareworkspace_keyboard.cpp
    compareworkspace_snapshot.cpp
    compareworkspace_nav.cpp
    analysispanel.cpp
    analysispanel_analysis.cpp
    analysispanel_analysis_worker.cpp
    analysispanel_actions.cpp
    analysispanel_buildui.cpp
    analysispanel_pages.cpp
    analysispanel_history.cpp
    exportdialog.cpp
    exportmessages.cpp
    fileopmessages.cpp
    exportcommand.cpp
    metadatapanel.cpp
    metadatamodel.cpp
    searchpanel.cpp
    batchdialog.cpp
    batchrenamepanel.cpp
    widgets/rawimageview.cpp
    widgets/ViewModeSegment.cpp
    widgets/rawimageview_surface.cpp
    widgets/histogramwidget.cpp
    widgets/scopewidget.cpp
    widgets/displayadjustpanel.cpp
    widgets/displayadjustcontroller.cpp
    widgets/blindtestdialog.cpp
    widgets/pythonscriptdialog.cpp
    pluginsettings.cpp
    preferencesdialog.cpp
    cache_settings.cpp
    usmcomparedialog.cpp
    usmcomparedialog_load.cpp
    yuvcomparedialog.cpp
    yuvparamsdialog.cpp
    plainrawparamseditor.cpp
    plainrawparamsdialog.cpp
    plainrawsettings.cpp
    yuvparamsettings.cpp
    yuvextensionmapeditor.cpp
    yuvpixfmtpicker.cpp
    Theme.cpp
    MenuItemInk.cpp
    platform/WindowChrome.cpp
    Language.cpp
    ThemeIcons.cpp
    hoverfade.cpp
    ${CMAKE_SOURCE_DIR}/resources/mviewer.qrc
    analysisoverlaydialog.cpp
    gpu/GpuTileUploader.cpp
    gpu/ImageBlitterShader.cpp
    imageviewer_gl.cpp
)

# Native window chrome (DWM title bar, corners, Mica). Stubs elsewhere.
if(WIN32)
    target_sources(mviewer_ui PRIVATE platform/WindowChrome_win.cpp)
    target_link_libraries(mviewer_ui PUBLIC dwmapi advapi32)
endif()

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
    application/PythonScriptRunner.cpp
    application/OpenDirectoryUseCase.cpp
    application/Shutdown.cpp
    application/Startup.cpp
)

target_link_libraries(mviewer_application PUBLIC mviewer_core Qt6::Gui)
target_include_directories(mviewer_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mviewer_application PUBLIC cxx_std_20)

add_executable(MViewer
    main.cpp
)
mviewer_release_pdb(MViewer)
if(WIN32)
    # Version resource generated from project(VERSION); see resources/mviewer.rc.in.
    set(MVIEWER_RC_ICON "${CMAKE_SOURCE_DIR}/resources/mviewer.ico")
    configure_file(${CMAKE_SOURCE_DIR}/resources/mviewer.rc.in
                   ${MVIEWER_GENERATED_INCLUDE_DIR}/mviewer.rc @ONLY)
    target_sources(MViewer PRIVATE
        ${MVIEWER_GENERATED_INCLUDE_DIR}/mviewer.rc
        ${CMAKE_SOURCE_DIR}/resources/mviewer.manifest)
    # Windows GUI subsystem: launching from Explorer or a shortcut opens no
    # console window. Qt6::Core links Qt's entry point (WinMain -> main) into
    # WIN32_EXECUTABLE targets. Only MViewer is a GUI app; tests, benchmarks
    # and CLI tools stay console programs. The release workflow and the
    # gui_subsystem test below fail if this ever regresses.
    set_target_properties(MViewer PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        # resources/mviewer.manifest carries the asInvoker trustInfo.
        target_link_options(MViewer PRIVATE /MANIFESTUAC:NO)
    endif()
endif()

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

# Product targets only (cmake/MViewerWarnings.cmake): -Wall -Wextra on GCC/Clang,
# /W4 on MSVC when MVIEWER_MSVC_W4=ON.
mviewer_target_warnings(mviewer_core mviewer_ui mviewer_application MViewer)

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
    ${CMAKE_SOURCE_DIR}/tests/core/test_compare.cpp
    ${CMAKE_SOURCE_DIR}/tests/core/test_m10.cpp
    # M10 suites (incremental isolation)
    ${CMAKE_SOURCE_DIR}/tests/core/test_memorytracker.cpp
    ${CMAKE_SOURCE_DIR}/tests/core/test_benchmark.cpp
    core/perf/GpuTracker.cpp
    benchmark/corpus.cpp
    benchmark/scenarios.cpp
    benchmark/scenarios_pipeline.cpp
)

target_link_libraries(core_tests
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(core_tests PRIVATE cxx_std_20)

target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(mviewer_unit_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_core.cpp
)

target_link_libraries(mviewer_unit_tests
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(mviewer_unit_tests PRIVATE cxx_std_20)

target_include_directories(mviewer_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_appstate
    ${CMAKE_SOURCE_DIR}/tests/test_appstate.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_ui_models.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_menu_ink.cpp
    naturalnameorder.cpp
    runtime_storage.cpp
    selectionmodel.cpp
    directorymodel.cpp
    imagelistmodel.cpp
    workspacemodel.cpp
    analyzermodel.cpp
    Theme.cpp
    MenuItemInk.cpp
    platform/WindowChrome.cpp
    Language.cpp
    ThemeIcons.cpp
    hoverfade.cpp
    breadcrumbbar.cpp
    widgets/ViewModeSegment.cpp
    ${CMAKE_SOURCE_DIR}/resources/mviewer.qrc
)
target_link_libraries(test_ui_models
    PRIVATE
        Qt6::Widgets
        Qt6::Core
)
if(WIN32)
    target_sources(test_ui_models PRIVATE platform/WindowChrome_win.cpp)
    target_link_libraries(test_ui_models PRIVATE dwmapi)
endif()
target_compile_features(test_ui_models PRIVATE cxx_std_20)
target_include_directories(test_ui_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(benchmark
    benchmark_main.cpp
)

# M16: GpuTileUploader bookkeeping (headless; GL upload is a no-op
# offscreen and activates only on real hardware with MVIEWER_GPU=1).
add_executable(test_gputile
    ${CMAKE_SOURCE_DIR}/tests/core/test_gputile.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m3m4m5.cpp
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
# The suite reads testdata/ via MVIEWER_SOURCE_DIR, like every other target.
target_compile_definitions(test_m3m4m5 PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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


# The test targets now live beside the tests themselves (review item 2).
include(${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)

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
