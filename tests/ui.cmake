# ─── ctest registration ──────────────────────────────────────────────────────

add_test(NAME core_tests       COMMAND core_tests)
add_test(NAME m3m4m5_tests     COMMAND test_m3m4m5)
add_test(NAME unit_tests       COMMAND mviewer_unit_tests)
add_test(NAME appstate_tests   COMMAND test_appstate)
add_test(NAME ui_models_tests  COMMAND test_ui_models)

# Review P2-08: the MVIEWER_BLITTER A/B switch. No GL context is created (this
# suite runs headless); compile/link/draw parity needs real hardware.
add_executable(blitshader_flag_tests ${CMAKE_SOURCE_DIR}/tests/test_blitshader_flag.cpp)
target_link_libraries(blitshader_flag_tests PRIVATE mviewer_ui Qt6::Widgets Qt6::Gui Qt6::Core)
target_compile_features(blitshader_flag_tests PRIVATE cxx_std_20)
target_include_directories(blitshader_flag_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME blitshader_flag_tests COMMAND blitshader_flag_tests)
add_test(NAME m3pipeline_tests COMMAND m3pipeline_tests)
add_test(NAME decoder_tests    COMMAND test_decoder)
add_test(NAME cache_tests      COMMAND test_cache)
add_test(NAME mipmap_pyramid_tests COMMAND test_mipmap_pyramid)
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
add_test(NAME metadata_presentation_tests COMMAND test_metadata_presentation)
add_test(NAME filesystem_tests COMMAND test_filesystem)
add_test(NAME filesystem_retry_tests COMMAND test_filesystem_retry)
add_test(NAME eventbus_tests   COMMAND test_eventbus)
add_test(NAME result_tests     COMMAND test_result)
add_test(NAME render_pipeline_tests COMMAND test_render_pipeline)
add_test(NAME display_resample_tests COMMAND test_display_resample)
set_tests_properties(cache_tests repository_tests m26_repository_tests m27_repository_tests
    PROPERTIES RUN_SERIAL TRUE
)

# Generous wall-time ceiling for the HQ display resample. Not a perf gate:
# the budget only fails a pathological regression, not noise around the median.
add_executable(bench_display_resample
    core/bench_display_resample.cpp
)
target_link_libraries(bench_display_resample
    PRIVATE
        Qt6::Core
        mviewer_core
)
target_compile_features(bench_display_resample PRIVATE cxx_std_20)
target_include_directories(bench_display_resample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME display_resample_bench COMMAND bench_display_resample)
set_tests_properties(display_resample_bench PROPERTIES TIMEOUT 120)
add_test(NAME tilecache_tests COMMAND test_tilecache)
add_test(NAME jpeg_block_tiles_tests COMMAND test_jpeg_block_tiles)
add_test(NAME region_tiles_tests COMMAND test_region_tiles)
add_test(NAME gputile_tests  COMMAND test_gputile)
add_test(NAME cpu_features_tests COMMAND test_cpu_features)
add_test(NAME rawdecode_tests COMMAND test_rawdecode)
add_test(NAME raw_extensions_tests COMMAND test_raw_extensions)
add_test(NAME raw_display_tests COMMAND test_raw_display)
add_test(NAME pixelcontroller_tests COMMAND test_pixelcontroller)
add_test(NAME thumbnailpipeline_tests COMMAND test_thumbnailpipeline)
add_test(NAME commandstack_tests COMMAND test_commandstack)
add_test(NAME commandstack_callback_regression
    COMMAND test_commandstack --callback-probe)
set_tests_properties(commandstack_callback_regression PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 60)
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
# Wall-clock budgets (directory listing, first thumbnail). Parallel ctest
# makes them flake; the budgets themselves are unchanged. product_workflow_gate
# is a separate chain, not a second registration of this test, and is serial
# below because it launches this executable again.
set_tests_properties(product_browse_tests PROPERTIES RUN_SERIAL TRUE)
add_test(NAME compare_workflow_tests COMMAND compare_workflow_tests)
add_test(NAME histogram_tests COMMAND histogram_tests)
add_test(NAME analysis_panel_tests COMMAND analysis_panel_tests)
add_test(NAME export_tests COMMAND export_tests)
add_test(NAME export_pipeline_tests COMMAND export_pipeline_tests)
add_test(NAME export_job_tests COMMAND export_job_tests)

# P1-1 — RecentStore (recently-viewed list)
add_executable(recentstore_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_recentstore.cpp
)

target_link_libraries(recentstore_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)

target_compile_features(recentstore_tests PRIVATE cxx_std_20)
target_include_directories(recentstore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(recentstore_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME recentstore_tests COMMAND recentstore_tests)

add_executable(memory_budget_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_memory_budget.cpp
)

target_link_libraries(memory_budget_tests
    PRIVATE
        Qt6::Core
        mviewer_core
)

target_compile_features(memory_budget_tests PRIVATE cxx_std_20)
target_include_directories(memory_budget_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(memory_budget_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME memory_budget_tests COMMAND memory_budget_tests)

# Audit #41 / #47 — preset paths and settings restore.
add_executable(audit_persistence_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_audit_persistence.cpp
)
target_link_libraries(audit_persistence_tests
    PRIVATE
        Qt6::Core
        mviewer_core
)
target_compile_features(audit_persistence_tests PRIVATE cxx_std_20)
target_include_directories(audit_persistence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME audit_persistence_tests COMMAND audit_persistence_tests)

# P2 — AnalyzerRegistry extensibility: a new analyzer is discoverable/runnable
# without any MainWindow/UI change.
add_executable(analyzer_registry_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_register.cpp
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
add_test(NAME imagerotate_tests COMMAND test_imagerotate)
add_test(NAME imagerotate_io_tests COMMAND test_imagerotate_io)
add_test(NAME jpegexif_tests COMMAND test_jpegexif)
add_test(NAME rotatefiles_tests COMMAND test_rotatefiles)
add_test(NAME filecopycommand_tests COMMAND test_filecopycommand)

add_executable(test_fileop_targets
    ${CMAKE_SOURCE_DIR}/tests/test_fileop_targets.cpp
)
target_link_libraries(test_fileop_targets PRIVATE Qt6::Gui Qt6::Core)
target_compile_features(test_fileop_targets PRIVATE cxx_std_20)
target_include_directories(test_fileop_targets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fileop_targets_tests COMMAND test_fileop_targets)

add_executable(test_split_transfer
    ${CMAKE_SOURCE_DIR}/tests/test_split_transfer.cpp
)
target_link_libraries(test_split_transfer PRIVATE Qt6::Core)
target_compile_features(test_split_transfer PRIVATE cxx_std_20)
target_include_directories(test_split_transfer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME split_transfer_tests COMMAND test_split_transfer)
add_test(NAME differenceengine_tests COMMAND test_differenceengine)
add_test(NAME blinkcontroller_tests COMMAND test_blinkcontroller)
add_test(NAME aligner_tests COMMAND test_aligner)
add_test(NAME imageoverlay_tests COMMAND test_imageoverlay)
add_test(NAME browse_convergence_tests COMMAND test_browse_convergence)
add_test(NAME analysisengine_tests COMMAND test_analysisengine)
add_test(NAME usm_tests COMMAND test_usm)

# ─── M25: Browse pipeline convergence (UI level) — thumbnail cache identity,
# pipeline size/generation semantics, panel size-switch and filter behaviors ─
add_executable(browse_convergence_ui_tests
    ${CMAKE_SOURCE_DIR}/tests/test_browse_convergence.cpp
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

# ─── H23: object-level teardown stress (docs/adr/017) ───────────────────────
# Repeatedly creates a real MainWindow (browse + thumbnail + metadata pipelines)
# and a CompareWorkspace with queued decode/diff work, then destroys them while
# the work is in flight. Bounds and justifies the Qt-teardown workarounds the
# acceptance/workflow suites use for process-level teardown.
add_executable(teardown_stress_tests
    ${CMAKE_SOURCE_DIR}/tests/test_teardown_stress.cpp
)

target_link_libraries(teardown_stress_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(teardown_stress_tests PRIVATE cxx_std_20)
target_include_directories(teardown_stress_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME teardown_stress_tests COMMAND teardown_stress_tests)
set_tests_properties(teardown_stress_tests PROPERTIES RUN_SERIAL TRUE)

# --- M27: QObject async lifetime closure regression (UI level) ---
# Child-process destroy-mid-decode crash detection (PreviewPanel / ImageViewer),
# A->B->A newest-generation delivery, rejection terminal state, and the P9 24MP
# UI completion-latency bounds. Heavy but bounded; runs serial.
add_executable(m27_lifetime_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m27_lifetime.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_m27_preview_frame.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_m27_preview_identity.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_m27_preview_source_size.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_m27_preview_submit_reject.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m27_lifecycle.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m46_repository.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m46_thumbnail.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m47_source.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m53_large_source.cpp
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

add_executable(bmp_lod_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_bmp_lod.cpp
)
target_link_libraries(bmp_lod_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(bmp_lod_tests PRIVATE cxx_std_20)
target_include_directories(bmp_lod_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME bmp_lod_tests COMMAND bmp_lod_tests)
set_tests_properties(bmp_lod_tests PROPERTIES TIMEOUT 120)

add_executable(png_lod_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_png_lod.cpp
)
target_link_libraries(png_lod_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(png_lod_tests PRIVATE cxx_std_20)
target_include_directories(png_lod_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME png_lod_tests COMMAND png_lod_tests)
set_tests_properties(png_lod_tests PROPERTIES TIMEOUT 120)

add_executable(tiff_lod_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_tiff_lod.cpp
)
target_link_libraries(tiff_lod_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(tiff_lod_tests PRIVATE cxx_std_20)
target_include_directories(tiff_lod_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME tiff_lod_tests COMMAND tiff_lod_tests)
set_tests_properties(tiff_lod_tests PROPERTIES TIMEOUT 120)

# M53 Native Windows qualification: exercise the real Viewer and Compare UI
# over the 100MP TIFF fixture, including zoom-region refresh and mode churn.
add_executable(m53_large_source_ui_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m53_large_source_ui.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m53_large_source_soak.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m54_perceived_performance.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m55_interactive.cpp
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

# ─── M58: large-directory query snapshots / latest-wins data path ──────────
# The deterministic core gate uses synthetic 10k/50k entries so it remains
# hermetic and fast; native GUI typing/first-paint qualification is recorded as
# MANUAL/BLOCKED in the Phase-0 evidence document.
add_executable(m58_large_directory_query_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m58_large_directory_query.cpp
)
target_link_libraries(m58_large_directory_query_tests PRIVATE mviewer_core Qt6::Core)
target_compile_features(m58_large_directory_query_tests PRIVATE cxx_std_20)
target_include_directories(m58_large_directory_query_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m58_large_directory_query_tests COMMAND m58_large_directory_query_tests)
set_tests_properties(m58_large_directory_query_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M59 Phase 0: source metadata truth baseline ───────────────────────────
# Deliberately RED before the M59 color-managed presentation implementation:
# probe/full/frame-sequence paths must agree on bits-per-channel, exact EXIF
# orientation, canonical color-space labels, and embedded ICC bytes.
add_executable(m59_phase0_metadata_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m59_phase0.cpp
)
target_link_libraries(m59_phase0_metadata_tests PRIVATE mviewer_core Qt6::Gui Qt6::Core)
target_compile_features(m59_phase0_metadata_tests PRIVATE cxx_std_20)
target_include_directories(m59_phase0_metadata_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m59_phase0_metadata_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m59_phase0_metadata_tests COMMAND m59_phase0_metadata_tests)
set_tests_properties(m59_phase0_metadata_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# ─── Big folders: hashed path lookups + progressive scan order ─────────────
mviewer_add_test(big_folder_tests
    SRCS ${CMAKE_SOURCE_DIR}/tests/test_big_folder.cpp
    LIBS mviewer_ui
    TIMEOUT 180 SERIAL)

# ─── M59 Browse regression: production evaluator + latest-wins panel ───────
add_executable(m59_browse_regression_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m59_browse_regression.cpp
)
target_link_libraries(m59_browse_regression_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m59_browse_regression_tests PRIVATE cxx_std_20)
target_include_directories(m59_browse_regression_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
mviewer_test_support(m59_browse_regression_tests)
add_test(NAME m59_browse_regression_tests COMMAND m59_browse_regression_tests)
set_tests_properties(m59_browse_regression_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_executable(gallery_filter_engine_tests
    ${CMAKE_SOURCE_DIR}/tests/test_gallery_filter_engine.cpp
)
target_link_libraries(gallery_filter_engine_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(gallery_filter_engine_tests PRIVATE cxx_std_20)
target_include_directories(gallery_filter_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gallery_filter_engine_tests COMMAND gallery_filter_engine_tests)
set_tests_properties(gallery_filter_engine_tests PROPERTIES TIMEOUT 60)

# ─── M59 adversarial source/presentation color contract ─────────────────────
add_executable(m59_color_managed_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m59_color_managed.cpp
)
target_link_libraries(m59_color_managed_tests PRIVATE mviewer_core Qt6::Gui Qt6::Core)
target_compile_features(m59_color_managed_tests PRIVATE cxx_std_20)
target_include_directories(m59_color_managed_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(m59_color_managed_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME m59_color_managed_tests COMMAND m59_color_managed_tests)
set_tests_properties(m59_color_managed_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# Transition sampling and animation damage use actual viewer draw/resize paths.
mviewer_add_test(viewer_transition_paint_tests
    SRCS ${CMAKE_SOURCE_DIR}/tests/test_viewer_transition_paint.cpp
    LIBS mviewer_ui
    TIMEOUT 60 SERIAL)

# Display ICC cache: Browse/Compare resize must not re-read or re-parse the
# monitor profile. Loader injection keeps the suite platform-independent.
mviewer_add_test(display_color_context_cache_tests
    SRCS ${CMAKE_SOURCE_DIR}/tests/test_display_color_context_cache.cpp
    LIBS mviewer_ui
    TIMEOUT 60 SERIAL)

# ─── M60: linked ROI geometry + source RGB measurement contract ────────────
add_executable(m60_linked_roi_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m60_linked_roi.cpp
)
target_link_libraries(m60_linked_roi_tests PRIVATE mviewer_core Qt6::Core)
target_compile_features(m60_linked_roi_tests PRIVATE cxx_std_20)
target_include_directories(m60_linked_roi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m60_linked_roi_tests COMMAND m60_linked_roi_tests)
set_tests_properties(m60_linked_roi_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# M61: real Grid/Canvas right-drag, editable ROI, HUD and lifecycle gate.
add_executable(m61_roi_workflow_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m61_roi_workflow.cpp
)
target_link_libraries(m61_roi_workflow_tests
    PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core mviewer_ui
)
target_compile_features(m61_roi_workflow_tests PRIVATE cxx_std_20)
target_include_directories(m61_roi_workflow_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m61_roi_workflow_tests COMMAND m61_roi_workflow_tests)
set_tests_properties(m61_roi_workflow_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

# M61 follow-up: Compare Ctrl+C copies the ROI statistics chips (WYSIWYG).
add_executable(m61_roi_copy_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m61_roi_copy.cpp
)
target_link_libraries(m61_roi_copy_tests
    PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core mviewer_ui
)
target_compile_features(m61_roi_copy_tests PRIVATE cxx_std_20)
target_include_directories(m61_roi_copy_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m61_roi_copy_tests COMMAND m61_roi_copy_tests)
set_tests_properties(m61_roi_copy_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

add_executable(m61_roi_benchmark
    ${CMAKE_SOURCE_DIR}/tests/test_m61_roi_benchmark.cpp
)
target_link_libraries(m61_roi_benchmark PRIVATE mviewer_core Qt6::Core)
target_compile_features(m61_roi_benchmark PRIVATE cxx_std_20)
target_include_directories(m61_roi_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m61_roi_benchmark COMMAND m61_roi_benchmark)
set_tests_properties(m61_roi_benchmark PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# Standalone recorder for M55 10k/50k memory and navigation evidence. It is
# intentionally not a gate because its numbers are machine-dependent.
add_executable(m55_interactive_baseline
    ${CMAKE_SOURCE_DIR}/tests/test_m55_interactive_baseline.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m47_viewer_lod.cpp
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

# Four 12.6 MP JPEGs: every later raster must appear after folder-sequence
# growth, neighbor-preload cancel/restart, and rapid current-image changes.
add_executable(lod_four_jpeg_ready_tests
    ${CMAKE_SOURCE_DIR}/tests/test_lod_four_jpeg_ready.cpp
)
target_link_libraries(lod_four_jpeg_ready_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(lod_four_jpeg_ready_tests PRIVATE cxx_std_20)
target_include_directories(lod_four_jpeg_ready_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lod_four_jpeg_ready_tests COMMAND lod_four_jpeg_ready_tests)
set_tests_properties(lod_four_jpeg_ready_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Progressive scan publication: 1k/10k incremental conversions, middle inserts,
# streamable type/date/size first-screen, generation check before dir.exists().
add_executable(scan_publication_tests
    ${CMAKE_SOURCE_DIR}/tests/test_scan_publication.cpp
)
target_link_libraries(scan_publication_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(scan_publication_tests PRIVATE cxx_std_20)
target_include_directories(scan_publication_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)
add_test(NAME scan_publication_tests COMMAND scan_publication_tests)
set_tests_properties(scan_publication_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Name-sort scan batches (empty, 1, 16, 144) must keep a still-needed neighbor
# preload and its warm raster. Shrink/removal still cancels or evicts that work.
add_executable(browse_append_retain_tests
    ${CMAKE_SOURCE_DIR}/tests/test_browse_append_retain.cpp
)
target_link_libraries(browse_append_retain_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(browse_append_retain_tests PRIVATE cxx_std_20)
target_include_directories(browse_append_retain_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME browse_append_retain_tests COMMAND browse_append_retain_tests)
set_tests_properties(browse_append_retain_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# First zoomed-out LOD Sharp-resamples to ImageViewer paint dest (no GUI-thread
# SmoothPixmapTransform of a mismatched multi-megapixel raster).
add_executable(first_lod_sharp_tests
    ${CMAKE_SOURCE_DIR}/tests/test_first_lod_sharp.cpp
)
target_link_libraries(first_lod_sharp_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(first_lod_sharp_tests PRIVATE cxx_std_20)
target_include_directories(first_lod_sharp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME first_lod_sharp_tests COMMAND first_lod_sharp_tests)
set_tests_properties(first_lod_sharp_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Queue wait vs decode for the current display LOD and neighbor display preloads.
add_executable(queue_decode_timing_tests
    ${CMAKE_SOURCE_DIR}/tests/test_queue_decode_timing.cpp
)
target_link_libraries(queue_decode_timing_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(queue_decode_timing_tests PRIVATE cxx_std_20)
target_include_directories(queue_decode_timing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME queue_decode_timing_tests COMMAND queue_decode_timing_tests)
set_tests_properties(queue_decode_timing_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Promoted display-raster callbacks must validate path, generation, target, and
# serial before dropping the in-flight request (#380 recovery stays on the
# current result only). Synchronous; no large fixtures.
add_executable(promoted_raster_identity_tests
    ${CMAKE_SOURCE_DIR}/tests/test_promoted_raster_identity.cpp
)
target_link_libraries(promoted_raster_identity_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(promoted_raster_identity_tests PRIVATE cxx_std_20)
target_include_directories(promoted_raster_identity_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME promoted_raster_identity_tests COMMAND promoted_raster_identity_tests)
set_tests_properties(promoted_raster_identity_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

# ─── M47 Phase 3: Compare source-backed display (UI level) ───────────────────
# Infeasible sources (full RGB > the analysis limit) keep their pane via a
# metadata-only placeholder and display through the source-backed LOD path;
# failures vs skips are accounted separately; zoom re-materializes at a denser
# edge; TIFF honesty and destroy safety are covered.
add_executable(m47_compare_lod_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m47_compare_lod.cpp
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

# Compare pan: a region raster dragged past its margin must expose the
# full-frame backdrop, never the pane background.
add_executable(compare_pan_backdrop_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_pan_backdrop.cpp
)
target_link_libraries(compare_pan_backdrop_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_pan_backdrop_tests PRIVATE cxx_std_20)
target_include_directories(compare_pan_backdrop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_pan_backdrop_tests COMMAND compare_pan_backdrop_tests)

# Compare overlay toggles (sync crosshair, diff heatmap) clear on switch-off.
add_executable(compare_overlay_toggle_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_overlay_toggles.cpp
)
target_link_libraries(compare_overlay_toggle_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_overlay_toggle_tests PRIVATE cxx_std_20)
target_include_directories(compare_overlay_toggle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_overlay_toggle_tests COMMAND compare_overlay_toggle_tests)

# Space shows the other pane's image exactly as that pane shows it, including
# its independent Ctrl+wheel zoom and pan; release restores the held pane.
add_executable(compare_space_local_zoom_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_space_local_zoom.cpp
)
target_link_libraries(compare_space_local_zoom_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_space_local_zoom_tests PRIVATE cxx_std_20)
target_include_directories(compare_space_local_zoom_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_space_local_zoom_tests COMMAND compare_space_local_zoom_tests)
set_tests_properties(compare_space_local_zoom_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Space + ROI: the held pane shows the other image's ROI rectangle and chip at
# that image's viewport; Ctrl+C while held copies what is on screen.
add_executable(compare_space_roi_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_space_roi.cpp
)
target_link_libraries(compare_space_roi_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_space_roi_tests PRIVATE cxx_std_20)
target_include_directories(compare_space_roi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_space_roi_tests COMMAND compare_space_roi_tests)
set_tests_properties(compare_space_roi_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# Unlinked canvas ROI stays on the pane it was drawn on; grid Space ignores
# wheel and pan; canvas pixel grid follows the held image; ROI histogram and
# copy use each pane's selection when sizes differ.
add_executable(compare_unlinked_hold_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_unlinked_hold.cpp
)
target_link_libraries(compare_unlinked_hold_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_unlinked_hold_tests PRIVATE cxx_std_20)
target_include_directories(compare_unlinked_hold_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_unlinked_hold_tests COMMAND compare_unlinked_hold_tests)
set_tests_properties(compare_unlinked_hold_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# M52: pure source-backed Compare display planning regression matrix. The
# planner owns no widget or decoder state, so this remains deterministic and
# proves placeholder/Fit/zoom/pan/DPR/crop/rotation/supersession semantics.
add_executable(compare_display_planner_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_display_planner.cpp
)
add_executable(compare_temporary_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_temporary.cpp
)
target_compile_features(compare_temporary_tests PRIVATE cxx_std_20)
target_include_directories(compare_temporary_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_temporary_tests COMMAND compare_temporary_tests)
add_executable(compare_neighbor_paths_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_neighbor_paths.cpp
)
target_compile_features(compare_neighbor_paths_tests PRIVATE cxx_std_20)
target_include_directories(compare_neighbor_paths_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_neighbor_paths_tests COMMAND compare_neighbor_paths_tests)
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

# Multi-LOD pyramid + display delivery/priority helpers (pure planning).
add_executable(compare_display_pyramid_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_display_pyramid.cpp
)
target_link_libraries(compare_display_pyramid_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_display_pyramid_tests PRIVATE cxx_std_20)
target_include_directories(compare_display_pyramid_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_display_pyramid_tests COMMAND compare_display_pyramid_tests)
set_tests_properties(compare_display_pyramid_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# Soft/warm compare paint geometry: thumbs must not claim full sourceSize.
add_executable(compare_warm_soft_geometry_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_warm_soft_geometry.cpp
)
target_link_libraries(compare_warm_soft_geometry_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
)
target_compile_features(compare_warm_soft_geometry_tests PRIVATE cxx_std_20)
target_include_directories(compare_warm_soft_geometry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_warm_soft_geometry_tests COMMAND compare_warm_soft_geometry_tests)

# Compare display batch carry-over + pixel-grid raster gate (stuck Loading race).
add_executable(compare_display_pending_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_display_pending.cpp
)
target_link_libraries(compare_display_pending_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_display_pending_tests PRIVATE cxx_std_20)
target_include_directories(compare_display_pending_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_display_pending_tests COMMAND compare_display_pending_tests)

# Compare display state machine: table-driven transitions, #202 livelock and
# lost-request regressions, seeded interleaving soak (no pane stays Loading).
add_executable(compare_display_state_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_display_state.cpp
)
target_link_libraries(compare_display_state_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_display_state_tests PRIVATE cxx_std_20)
target_include_directories(compare_display_state_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_display_state_tests COMMAND compare_display_state_tests)

# Compare soft reload at high zoom with two 4096x3072 JPEGs: no stuck Loading.
add_executable(compare_stuck_loading_stress_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_stuck_loading_stress.cpp
)
target_link_libraries(compare_stuck_loading_stress_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_stuck_loading_stress_tests PRIVATE cxx_std_20)
target_include_directories(compare_stuck_loading_stress_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_stuck_loading_stress_tests COMMAND compare_stuck_loading_stress_tests)
set_tests_properties(compare_stuck_loading_stress_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# Compare in-session decoded frame LRU pool (path+frameIndex).
add_executable(compare_session_frame_pool_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_session_frame_pool.cpp
)
target_link_libraries(compare_session_frame_pool_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_session_frame_pool_tests PRIVATE cxx_std_20)
target_include_directories(compare_session_frame_pool_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_session_frame_pool_tests COMMAND compare_session_frame_pool_tests)
set_tests_properties(compare_session_frame_pool_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ADR-014 CompareWorkspace componentization tests (CompareSessionState & CompareRoiController)
add_executable(compare_components_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_components.cpp
)
target_link_libraries(compare_components_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_components_tests PRIVATE cxx_std_20)
target_include_directories(compare_components_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_components_tests COMMAND compare_components_tests)
set_tests_properties(compare_components_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_executable(compare_roi_stats_presenter_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_roi_stats_presenter.cpp
)
target_link_libraries(compare_roi_stats_presenter_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_roi_stats_presenter_tests PRIVATE cxx_std_20)
target_include_directories(compare_roi_stats_presenter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_roi_stats_presenter_tests COMMAND compare_roi_stats_presenter_tests)
set_tests_properties(compare_roi_stats_presenter_tests PROPERTIES TIMEOUT 60)

# ADR-014 CompareWorkspace componentization tests (CompareDiffRenderer)
add_executable(compare_diff_renderer_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_diff_renderer.cpp
)
target_link_libraries(compare_diff_renderer_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_diff_renderer_tests PRIVATE cxx_std_20)
target_include_directories(compare_diff_renderer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_diff_renderer_tests COMMAND compare_diff_renderer_tests)
set_tests_properties(compare_diff_renderer_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ADR-014 CompareWorkspace componentization tests (CompareDisplayManager)
add_executable(compare_display_manager_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_display_manager.cpp
)
target_link_libraries(compare_display_manager_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_display_manager_tests PRIVATE cxx_std_20)
target_include_directories(compare_display_manager_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_display_manager_tests COMMAND compare_display_manager_tests)
set_tests_properties(compare_display_manager_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M47 Phase 4: exact-source consumers with source-backed panes ────────────
# Inspector / diff / PSNR / SSIM / export keep full-resolution correctness when
# source-backed (LOD) panes are present: they never sample the display LOD and
# never trigger implicit materialization; infeasible panes degrade to invalid /
# non-comparable states instead.
add_executable(m47_exact_source_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m47_exact_source.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m47_restore.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m47_large_soak.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m48_phase0.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m48_phase0_failures.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_m46_browse.cpp
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

# First-open stall: GUI stays responsive when a directory probe blocks and
# when every thumbnail decode fails.
add_executable(first_open_stall_tests
    ${CMAKE_SOURCE_DIR}/tests/test_first_open_stall.cpp
)
target_link_libraries(first_open_stall_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(first_open_stall_tests PRIVATE cxx_std_20)
target_include_directories(first_open_stall_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(first_open_stall_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME first_open_stall_tests COMMAND first_open_stall_tests)
set_tests_properties(first_open_stall_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# ─── M46: crash-safe persistence (atomic replace + fault injection) ─────────
add_executable(m46_persistence_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_m46_persistence.cpp
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
set_tests_properties(architecture_gate_regression PROPERTIES TIMEOUT 120)

# ADR gate: ImageRepository, src/core/compare/, and compareworkspace are
# protected, and a failed git diff is a non-zero result rather than a pass.
add_test(NAME adr_gate_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/adr_gate_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(adr_gate_regression PROPERTIES TIMEOUT 120)

# M52: complexity responsibility-TU guardrail regression. A planted 1001-line
# responsibility file must fail, while two legal sub-1000-line TUs remain
# visible as warnings and pass the hard gate.
add_test(NAME complexity_gate_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/complexity_gate_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(complexity_gate_regression PROPERTIES TIMEOUT 120)

# M52: execute the generators through Windows PowerShell 5.1 and validate the
# emitted files as strict UTF-8 without common mojibake tokens.
add_test(NAME script_encoding_regression
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/script_encoding_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(script_encoding_regression PROPERTIES TIMEOUT 120)

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
    ${CMAKE_SOURCE_DIR}/tests/test_m50_navigation.cpp
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

# P0 product logic: single rating/flag pipeline re-evaluates gallery filters;
# empty/new-image ROI reaches the AnalysisPanel.
add_executable(p0_rating_roi_tests
    ${CMAKE_SOURCE_DIR}/tests/test_p0_rating_roi.cpp
)
target_link_libraries(p0_rating_roi_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(p0_rating_roi_tests PRIVATE cxx_std_20)
target_include_directories(p0_rating_roi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME p0_rating_roi_tests COMMAND p0_rating_roi_tests)
set_tests_properties(p0_rating_roi_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# ─── M56: live-folder coherence / incremental Browse reconciliation ─────────
# Snapshot/delta classification is UI-independent; the monitor and gallery
# tests exercise debounce, bounded rescans, disappearance/recovery, selection
# continuity, and model-identity preservation on the real Qt path.
add_executable(m56_directory_snapshot_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_m56_directory_snapshot.cpp
)
target_link_libraries(m56_directory_snapshot_tests
    PRIVATE
        Qt6::Core
        mviewer_core
)
target_compile_features(m56_directory_snapshot_tests PRIVATE cxx_std_20)
target_include_directories(m56_directory_snapshot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m56_directory_snapshot_tests COMMAND m56_directory_snapshot_tests)

add_executable(m56_directory_monitor_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m56_directory_monitor.cpp
    directorymonitor.cpp
)
target_link_libraries(m56_directory_monitor_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(m56_directory_monitor_tests PRIVATE cxx_std_20)
target_include_directories(m56_directory_monitor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m56_directory_monitor_tests COMMAND m56_directory_monitor_tests)
set_tests_properties(m56_directory_monitor_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_executable(m56_live_gallery_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m56_live_gallery.cpp
)
target_link_libraries(m56_live_gallery_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(m56_live_gallery_tests PRIVATE cxx_std_20)
target_include_directories(m56_live_gallery_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m56_live_gallery_tests COMMAND m56_live_gallery_tests)

# ─── M57: unified animated/multi-page image contract ───────────────────────
# The test writes deterministic red/green/blue GIF, WebP and TIFF fixtures,
# records the direct Qt baseline, then verifies the frame-aware core reader,
# repository cache identity, monotonic catch-up timeline, and persistence.
add_executable(m57_multiframe_decoder_tests
    ${CMAKE_SOURCE_DIR}/tests/test_m57_multiframe.cpp
)
target_link_libraries(m57_multiframe_decoder_tests
    PRIVATE
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_application
)
target_compile_features(m57_multiframe_decoder_tests PRIVATE cxx_std_20)
target_include_directories(m57_multiframe_decoder_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m57_multiframe_decoder_tests COMMAND m57_multiframe_decoder_tests)
set_tests_properties(m57_multiframe_decoder_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
set_tests_properties(m56_live_gallery_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

add_executable(mviewer_m56_live_directory_soak
    ${CMAKE_SOURCE_DIR}/benchmarks/m56_live_directory_soak_main.cpp
)
target_link_libraries(mviewer_m56_live_directory_soak PRIVATE Qt6::Core mviewer_core)
target_compile_features(mviewer_m56_live_directory_soak PRIVATE cxx_std_20)
target_include_directories(mviewer_m56_live_directory_soak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m56_live_directory_soak
    COMMAND mviewer_m56_live_directory_soak 10000)
set_tests_properties(m56_live_directory_soak PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# M50: command-line path classification must keep Unix absolute paths openable
# while preserving slash-prefixed Windows switches.
add_executable(commandline_tests
    ${CMAKE_SOURCE_DIR}/tests/test_commandline.cpp
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

# Manual-acceptance wizard: checklist schema + DryRun/NonInteractive (no GUI).
add_test(NAME manual_acceptance_tests
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/scripts/manual_acceptance_test.ps1
        ${CMAKE_SOURCE_DIR})
set_tests_properties(manual_acceptance_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

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
# The chain runs product_browse_tests.exe again. Those wall-clock budgets must
# not share the CPU with the rest of the suite. This is not a duplicate
# registration: the gate also runs compare, analysis, export, and workspace.
set_tests_properties(product_workflow_gate PROPERTIES RUN_SERIAL TRUE)
