# Test targets. Included from src/CMakeLists.txt (not add_subdirectory) so
# CMAKE_CURRENT_SOURCE_DIR still means the product source root and every
# ${CMAKE_CURRENT_SOURCE_DIR} include path below keeps working.

# ─── M6 split test suites (per-module) ────────────────────────────────────
foreach(_t decoder cache mipmap_pyramid repository scheduler metadata metadata_presentation filesystem filesystem_retry eventbus render_pipeline display_resample tilecache jpeg_block_tiles region_tiles pixelcontroller thumbnailpipeline commandstack crop job datamodel pluginregistry pluginregistryrunner m3acceptance analyzer_ext analyzer_batch raw_metadata rawdecode raw_extensions raw_display crashhandler pluginabi pluginexamples encoder imagetransform imagerotate imagerotate_io jpegexif rotatefiles filecopycommand differenceengine blinkcontroller aligner imageoverlay browse_convergence analysisengine usm m26_scheduler m26_metadata m26_thumbnail m26_repository m26_stats m27_thumbnail m27_eventbus m27_scheduler m27_repository result cpu_features)
    add_executable(test_${_t}
        ${CMAKE_SOURCE_DIR}/tests/core/test_${_t}.cpp
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
target_sources(test_m27_scheduler PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/core/test_m27_scheduler_canceltree.cpp)
# Case-only rename and trash-directory fallback. Kept beside ${CMAKE_SOURCE_DIR}/tests/test_commandstack.cpp
# so that file stays under the 800-line complexity cap.
target_sources(test_commandstack PRIVATE ${CMAKE_SOURCE_DIR}/tests/core/test_commandstack_case.cpp)
# The QPainter compositing half of the RenderCommand pipeline is a UI adapter
# (ADR-016); the core render test exercises it without linking all of mviewer_ui.
target_sources(test_render_pipeline PRIVATE rendercommandpainter.cpp)

# M41 cache thread-affinity regression inspects the process-wide Qt SQL
# connection registry directly.
target_link_libraries(test_cache PRIVATE Qt6::Sql)

# P5: headless release self-test gate. Runs the full application with --selftest,
# which decodes a generated image through the real product path and exits 0/1.
add_test(NAME selftest COMMAND MViewer --selftest)
set_tests_properties(selftest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# MViewer.exe must stay a Windows GUI-subsystem binary (no console window).
if(WIN32)
    add_test(NAME gui_subsystem
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/scripts/check_pe_subsystem.ps1
                -Path $<TARGET_FILE:MViewer> -Expected 2)
endif()

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

# i18n gate: C++/CMake sources are English; Chinese lives only in
# translations/*.ts and resources/help/*.zh_CN.html (see scripts/check_no_chinese.py).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME i18n_source_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_no_chinese.py
                --root ${CMAKE_SOURCE_DIR})
    # Bare English literals in widget calls (addAction/addItem/setToolTip/...).
    # --self-test proves the gate rejects its built-in negative samples.
    add_test(NAME i18n_tr_literal_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_tr_literals.py
                --self-test --root ${CMAKE_SOURCE_DIR})
    # Nightly benchmark regression rules (median, floors, stale baseline).
    add_test(NAME perf_regression_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/perf_regression.py --self-test)
    # The embedded .qm must contain every finished .ts translation (lrelease is
    # not part of the normal build; v1.0.172 shipped 19 strings untranslated).
    add_test(NAME i18n_qm_sync_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_qm_sync.py
                --ts ${CMAKE_SOURCE_DIR}/translations/mviewer_zh_CN.ts
                --qm ${CMAKE_SOURCE_DIR}/resources/i18n/mviewer_zh_CN.qm
                --qm ${CMAKE_SOURCE_DIR}/translations/mviewer_zh_CN.qm)
endif()

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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m3_pipeline.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_product_browse.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_compare_workflow.cpp
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
# ─── P0 #③ / P1 core unit tests (histogram) ──────────────────────────────────
add_executable(histogram_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_histogram.cpp
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

add_executable(analysis_panel_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_analysis_panel.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_analyzer_pipeline.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_compare_session.cpp
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

# ─── Beta regression — end-to-end user workflow tests (2026-07 external review) ───
# Workflow 1: open folder → browse → keyboard nav → zoom in → restore → close (real MainWindow)
# Workflow 2: two-image Compare → mode switch/exclusivity → Esc exit → keep browsing (real key events)
# Maps to the Browsing / Compare / View items of docs/beta_checklist.md.
add_executable(workflow_ux_tests
    ${CMAKE_SOURCE_DIR}/tests/test_workflow_ux.cpp
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
# The end-to-end UI flow waits on real async decodes and must run alone:
# next to bench_* the saturated CPU turns the "first frame within budget" check into noise.
set_tests_properties(workflow_ux_tests PROPERTIES RUN_SERIAL TRUE)

# ─── M24: async lifetime & concurrency regressions ─────────────────────────
# Covers the ThumbnailPanel bounded-pool redesign: destroy-mid-scan, 50 rapid
# folder switches, widget-tree teardown during a large scan, view switch during
# dimension resolve, out-of-order completion, and pool quiescence.
add_executable(async_lifetime_tests
    ${CMAKE_SOURCE_DIR}/tests/test_async_lifetime.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_browse_acceptance.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_browse_viewmode.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_browse_row_chrome.cpp
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

# ─── Metadata overlay layout (View → Image &info, I / M): the on-image panel
# sizes itself from its content and stays inside the viewer ───────────────────
add_executable(metadata_overlay_tests
    ${CMAKE_SOURCE_DIR}/tests/test_metadata_overlay.cpp
)

target_link_libraries(metadata_overlay_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(metadata_overlay_tests PRIVATE cxx_std_20)
target_include_directories(metadata_overlay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME metadata_overlay_tests COMMAND metadata_overlay_tests)
set_tests_properties(metadata_overlay_tests PROPERTIES RUN_SERIAL TRUE)

# ─── Viewer line-profile / video scopes (L / W) ──────────────────────────────
add_executable(viewer_scope_tests
    ${CMAKE_SOURCE_DIR}/tests/test_viewer_scope.cpp
)

target_link_libraries(viewer_scope_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(viewer_scope_tests PRIVATE cxx_std_20)
target_include_directories(viewer_scope_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME viewer_scope_tests COMMAND viewer_scope_tests)
set_tests_properties(viewer_scope_tests PROPERTIES RUN_SERIAL TRUE)

# ─── Viewer review markup (A / T, export review image) ───────────────────────
add_executable(viewer_annotate_tests
    ${CMAKE_SOURCE_DIR}/tests/test_viewer_annotate.cpp
)

target_link_libraries(viewer_annotate_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(viewer_annotate_tests PRIVATE cxx_std_20)
target_include_directories(viewer_annotate_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME viewer_annotate_tests COMMAND viewer_annotate_tests)
set_tests_properties(viewer_annotate_tests PROPERTIES RUN_SERIAL TRUE)

# ─── Viewer display adjustment panel (G) ────────────────────────────────────
add_executable(viewer_adjust_tests
    ${CMAKE_SOURCE_DIR}/tests/test_viewer_adjust.cpp
)

target_link_libraries(viewer_adjust_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(viewer_adjust_tests PRIVATE cxx_std_20)
target_include_directories(viewer_adjust_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME viewer_adjust_tests COMMAND viewer_adjust_tests)
set_tests_properties(viewer_adjust_tests PROPERTIES RUN_SERIAL TRUE)

# ─── CV overlay (item 2 extraction) ──────────────────────────────────────────
add_executable(cv_overlay_tests
    ${CMAKE_SOURCE_DIR}/tests/test_cv_overlay.cpp
)

target_link_libraries(cv_overlay_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(cv_overlay_tests PRIVATE cxx_std_20)
target_include_directories(cv_overlay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cv_overlay_tests COMMAND cv_overlay_tests)
set_tests_properties(cv_overlay_tests PROPERTIES RUN_SERIAL TRUE)

# Browse: paste clipboard into current folder + open-folder toolbar action
add_executable(browse_paste_acceptance
    ${CMAKE_SOURCE_DIR}/tests/test_browse_paste.cpp
)

target_link_libraries(browse_paste_acceptance
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(browse_paste_acceptance PRIVATE cxx_std_20)
target_include_directories(browse_paste_acceptance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME browse_paste_acceptance COMMAND browse_paste_acceptance)
set_tests_properties(browse_paste_acceptance PROPERTIES RUN_SERIAL TRUE)

# Browse: Ctrl+C puts the selected files on the clipboard for Explorer paste
add_executable(browse_copy_files_tests
    ${CMAKE_SOURCE_DIR}/tests/test_browse_copy_files.cpp
)

target_link_libraries(browse_copy_files_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(browse_copy_files_tests PRIVATE cxx_std_20)
target_include_directories(browse_copy_files_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME browse_copy_files_tests COMMAND browse_copy_files_tests)
set_tests_properties(browse_copy_files_tests PROPERTIES RUN_SERIAL TRUE)

# Split Browse View (RFC ADR-001/005/014/018)
add_executable(split_browse_acceptance
    ${CMAKE_SOURCE_DIR}/tests/test_split_browse_acceptance.cpp
)
target_link_libraries(split_browse_acceptance
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(split_browse_acceptance PRIVATE cxx_std_20)
target_include_directories(split_browse_acceptance PRIVATE )
mviewer_test_support(split_browse_acceptance)
add_test(NAME split_browse_acceptance COMMAND split_browse_acceptance)
set_tests_properties(split_browse_acceptance PROPERTIES RUN_SERIAL TRUE)

# ─── M45: real MainWindow CommandStack callback + async file-op surface ─────
add_executable(mainwindow_commandstack_acceptance
    ${CMAKE_SOURCE_DIR}/tests/test_mainwindow_commandstack.cpp
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

# Fullscreen viewer "Analyze" must leave fullscreen and show the analysis panel.
add_executable(fullscreen_analysis_tests
    ${CMAKE_SOURCE_DIR}/tests/test_fullscreen_analysis.cpp
)

target_link_libraries(fullscreen_analysis_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(fullscreen_analysis_tests PRIVATE cxx_std_20)
target_include_directories(fullscreen_analysis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fullscreen_analysis_tests COMMAND fullscreen_analysis_tests)
set_tests_properties(fullscreen_analysis_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# UI language: English source text, zh_CN translator and localized help pages.
add_executable(language_tests
    ${CMAKE_SOURCE_DIR}/tests/test_language.cpp
)

target_link_libraries(language_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(language_tests PRIVATE cxx_std_20)
target_include_directories(language_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME language_tests COMMAND language_tests)
set_tests_properties(language_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# Preferences autoCheckUpdates default and round-trip (startup silent update gate).
add_executable(auto_check_updates_pref_tests
    ${CMAKE_SOURCE_DIR}/tests/test_auto_check_updates_pref.cpp
)

target_link_libraries(auto_check_updates_pref_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(auto_check_updates_pref_tests PRIVATE cxx_std_20)
target_include_directories(auto_check_updates_pref_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME auto_check_updates_pref_tests COMMAND auto_check_updates_pref_tests)
set_tests_properties(auto_check_updates_pref_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# USM source ICC embed and RAW default save suffix.
add_executable(usm_dialog_tests
    ${CMAKE_SOURCE_DIR}/tests/test_usm_dialog.cpp
)
target_link_libraries(usm_dialog_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(usm_dialog_tests PRIVATE cxx_std_20)
target_include_directories(usm_dialog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME usm_dialog_tests COMMAND usm_dialog_tests)
set_tests_properties(usm_dialog_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# ADR 032: YUV parameter dialog summary and QSettings persistence.
# ADR 033: plain .raw parameters editor, dialog and Preferences persistence.
add_executable(plainraw_params_dialog_tests
    ${CMAKE_SOURCE_DIR}/tests/test_plainraw_params_dialog.cpp
)
target_link_libraries(plainraw_params_dialog_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(plainraw_params_dialog_tests PRIVATE cxx_std_20)
target_include_directories(plainraw_params_dialog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_params_dialog_tests COMMAND plainraw_params_dialog_tests)
set_tests_properties(plainraw_params_dialog_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_executable(yuv_params_dialog_tests
    ${CMAKE_SOURCE_DIR}/tests/test_yuv_params_dialog.cpp
)
target_link_libraries(yuv_params_dialog_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(yuv_params_dialog_tests PRIVATE cxx_std_20)
target_include_directories(yuv_params_dialog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_params_dialog_tests COMMAND yuv_params_dialog_tests)
set_tests_properties(yuv_params_dialog_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# ADR 032: two-file YUV PSNR / SSIM compare dialog (worker thread, cancel, report).
mviewer_add_test(yuv_compare_dialog_tests
    SRCS ${CMAKE_SOURCE_DIR}/tests/test_yuv_compare_dialog.cpp
    LIBS mviewer_ui
    TIMEOUT 30 SERIAL)

# Browse rotate follows SelectionModel after Viewer close (not stale m_currentPath).
add_executable(rotate_selection_tests
    ${CMAKE_SOURCE_DIR}/tests/test_rotate_selection.cpp
)

target_link_libraries(rotate_selection_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(rotate_selection_tests PRIVATE cxx_std_20)
target_include_directories(rotate_selection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME rotate_selection_tests COMMAND rotate_selection_tests)
set_tests_properties(rotate_selection_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

# ─── M24: Compare workflow acceptance (multi-image entry, mode-preserved
# state, continuous pair navigation, degraded images) ────────────────────────
add_executable(compare_acceptance_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_acceptance.cpp
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

# "Fit window (F)" with Uniform Pixel Scale off: same-aspect, different-resolution panes scale
# inversely to size.
add_executable(compare_best_fit_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_best_fit.cpp
)

target_link_libraries(compare_best_fit_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(compare_best_fit_tests PRIVATE cxx_std_20)
target_include_directories(compare_best_fit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_best_fit_tests COMMAND compare_best_fit_tests)
set_tests_properties(compare_best_fit_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# Three-pane compare: long unbreakable filenames must not squeeze the left
# pane; every pane gets an equal column and its own fit-to-window scale.
add_executable(compare_three_pane_fit_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_three_pane_fit.cpp
)
target_link_libraries(compare_three_pane_fit_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_three_pane_fit_tests PRIVATE cxx_std_20)
target_include_directories(compare_three_pane_fit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_three_pane_fit_tests COMMAND compare_three_pane_fit_tests)
set_tests_properties(compare_three_pane_fit_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M24: Analyze workflow acceptance (throwing-analyzer isolation, result
# currency per image, history/pin persistence) ───────────────────────────────
# Compare diff cache: warm (cached) batches equal cold ones exactly.
add_executable(compare_diff_cache_tests
    ${CMAKE_SOURCE_DIR}/tests/test_compare_diff_cache.cpp
)
target_link_libraries(compare_diff_cache_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(compare_diff_cache_tests PRIVATE cxx_std_20)
target_include_directories(compare_diff_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compare_diff_cache_tests COMMAND compare_diff_cache_tests)

add_executable(cell_image_adjust_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_cell_image_adjust.cpp
)
target_link_libraries(cell_image_adjust_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(cell_image_adjust_tests PRIVATE cxx_std_20)
target_include_directories(cell_image_adjust_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cell_image_adjust_tests COMMAND cell_image_adjust_tests)
set_tests_properties(cell_image_adjust_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_executable(analyze_acceptance_tests
    ${CMAKE_SOURCE_DIR}/tests/test_analyze_acceptance.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_export_acceptance.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_export.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_export_pipeline.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_export_job.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_workspace_persist.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_project.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_pixelinspector.cpp
)

target_link_libraries(test_pixelinspector
    PRIVATE
        Qt6::Core
        Qt6::Gui
        mviewer_core
)

target_compile_features(test_pixelinspector PRIVATE cxx_std_20)
target_include_directories(test_pixelinspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME pixelinspector_tests COMMAND test_pixelinspector)

# ─── YUV format geometry and pixel-format names ───────────────────────────────
add_executable(test_yuv_format ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_format.cpp)
target_link_libraries(test_yuv_format PRIVATE mviewer_core)
target_compile_features(test_yuv_format PRIVATE cxx_std_20)
target_include_directories(test_yuv_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_format_tests COMMAND test_yuv_format)

# ─── YUV4MPEG2 header parsing ──────────────────────────────────────────────────
add_executable(test_yuv_y4m ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_y4m.cpp)
target_link_libraries(test_yuv_y4m PRIVATE mviewer_core)
target_compile_features(test_yuv_y4m PRIVATE cxx_std_20)
target_include_directories(test_yuv_y4m PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_y4m_tests COMMAND test_yuv_y4m)

add_executable(test_yuv_inference ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_inference.cpp)
target_link_libraries(test_yuv_inference PRIVATE mviewer_core)
target_compile_features(test_yuv_inference PRIVATE cxx_std_20)
target_include_directories(test_yuv_inference PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_inference_tests COMMAND test_yuv_inference)

add_executable(test_yuv_layout_reference ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_layout_reference.cpp)
target_link_libraries(test_yuv_layout_reference PRIVATE mviewer_core)
target_compile_features(test_yuv_layout_reference PRIVATE cxx_std_20)
target_include_directories(test_yuv_layout_reference PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_layout_reference_tests COMMAND test_yuv_layout_reference)

add_executable(test_yuv_unpack ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_unpack.cpp)
target_link_libraries(test_yuv_unpack PRIVATE mviewer_core)
target_compile_features(test_yuv_unpack PRIVATE cxx_std_20)
target_include_directories(test_yuv_unpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_unpack_tests COMMAND test_yuv_unpack)

add_executable(test_yuv_to_rgb ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_to_rgb.cpp)
target_link_libraries(test_yuv_to_rgb PRIVATE mviewer_core)
target_compile_features(test_yuv_to_rgb PRIVATE cxx_std_20)
target_include_directories(test_yuv_to_rgb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_to_rgb_tests COMMAND test_yuv_to_rgb)

add_executable(test_yuv_simd ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_simd.cpp)
target_link_libraries(test_yuv_simd PRIVATE mviewer_core)
target_compile_features(test_yuv_simd PRIVATE cxx_std_20)
target_include_directories(test_yuv_simd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_simd_tests COMMAND test_yuv_simd)

# Benchmark only (not registered with CTest): bench_yuv_to_rgb prints Mpx/s per converter.
add_executable(bench_yuv_to_rgb core/bench_yuv_to_rgb.cpp)
target_link_libraries(bench_yuv_to_rgb PRIVATE mviewer_core)
target_compile_features(bench_yuv_to_rgb PRIVATE cxx_std_20)
target_include_directories(bench_yuv_to_rgb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_yuv_source ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_source.cpp)
target_link_libraries(test_yuv_source PRIVATE mviewer_core)
target_compile_features(test_yuv_source PRIVATE cxx_std_20)
target_include_directories(test_yuv_source PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_source_tests COMMAND test_yuv_source)

add_executable(test_yuv_decoder ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_decoder.cpp)
target_link_libraries(test_yuv_decoder PRIVATE mviewer_core)
target_compile_features(test_yuv_decoder PRIVATE cxx_std_20)
target_include_directories(test_yuv_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_decoder_tests COMMAND test_yuv_decoder)

# ADR 033: plain .raw sensor dumps.
add_executable(test_plainraw_format ${CMAKE_SOURCE_DIR}/tests/core/test_plainraw_format.cpp)
target_link_libraries(test_plainraw_format PRIVATE mviewer_core)
target_compile_features(test_plainraw_format PRIVATE cxx_std_20)
target_include_directories(test_plainraw_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_format_tests COMMAND test_plainraw_format)

add_executable(test_plainraw_unpack ${CMAKE_SOURCE_DIR}/tests/core/test_plainraw_unpack.cpp)
target_link_libraries(test_plainraw_unpack PRIVATE mviewer_core)
target_compile_features(test_plainraw_unpack PRIVATE cxx_std_20)
target_include_directories(test_plainraw_unpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_unpack_tests COMMAND test_plainraw_unpack)

add_executable(test_plainraw_render ${CMAKE_SOURCE_DIR}/tests/core/test_plainraw_render.cpp)
target_link_libraries(test_plainraw_render PRIVATE mviewer_core)
target_compile_features(test_plainraw_render PRIVATE cxx_std_20)
target_include_directories(test_plainraw_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_render_tests COMMAND test_plainraw_render)

add_executable(test_plainraw_file ${CMAKE_SOURCE_DIR}/tests/core/test_plainraw_file.cpp)
target_link_libraries(test_plainraw_file PRIVATE mviewer_core)
target_compile_features(test_plainraw_file PRIVATE cxx_std_20)
target_include_directories(test_plainraw_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_file_tests COMMAND test_plainraw_file)

add_executable(test_libraw_quality ${CMAKE_SOURCE_DIR}/tests/core/test_libraw_quality.cpp)
target_link_libraries(test_libraw_quality PRIVATE mviewer_core Qt6::Core)
target_compile_features(test_libraw_quality PRIVATE cxx_std_20)
target_include_directories(test_libraw_quality PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME libraw_quality_tests COMMAND test_libraw_quality)

add_executable(test_plainraw_stride ${CMAKE_SOURCE_DIR}/tests/core/test_plainraw_stride.cpp)
target_link_libraries(test_plainraw_stride PRIVATE mviewer_core)
target_compile_features(test_plainraw_stride PRIVATE cxx_std_20)
target_include_directories(test_plainraw_stride PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME plainraw_stride_tests COMMAND test_plainraw_stride)

add_executable(test_dither_quantize ${CMAKE_SOURCE_DIR}/tests/core/test_dither_quantize.cpp)
target_link_libraries(test_dither_quantize PRIVATE mviewer_core Qt6::Gui)
target_compile_features(test_dither_quantize PRIVATE cxx_std_20)
target_include_directories(test_dither_quantize PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME dither_quantize_tests COMMAND test_dither_quantize)

add_executable(test_yuv_sequence ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_sequence.cpp)
target_link_libraries(test_yuv_sequence PRIVATE mviewer_core)
target_compile_features(test_yuv_sequence PRIVATE cxx_std_20)
target_include_directories(test_yuv_sequence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_sequence_tests COMMAND test_yuv_sequence)

# Report feature 4: HDR (OpenEXR / PFM) decode + tone map.
add_executable(test_hdr_decoder ${CMAKE_SOURCE_DIR}/tests/core/test_hdr_decoder.cpp)
target_link_libraries(test_hdr_decoder PRIVATE mviewer_core)
target_compile_features(test_hdr_decoder PRIVATE cxx_std_20)
target_include_directories(test_hdr_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME hdr_decoder_tests COMMAND test_hdr_decoder)

# Report feature 5: review display adjustment (exposure / gamma / auto-stretch / LUT).
add_executable(test_display_adjust ${CMAKE_SOURCE_DIR}/tests/core/test_display_adjust.cpp)
target_link_libraries(test_display_adjust PRIVATE mviewer_core)
target_compile_features(test_display_adjust PRIVATE cxx_std_20)
target_include_directories(test_display_adjust PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME display_adjust_tests COMMAND test_display_adjust)

# Review P1-05: libjpeg-turbo JPEG fast path - correctness against Qt's decoder.
add_executable(test_jpeg_turbo ${CMAKE_SOURCE_DIR}/tests/core/test_jpeg_turbo.cpp)
target_link_libraries(test_jpeg_turbo PRIVATE mviewer_core Qt6::Gui Qt6::Core)
target_compile_features(test_jpeg_turbo PRIVATE cxx_std_20)
target_include_directories(test_jpeg_turbo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME jpeg_turbo_tests COMMAND test_jpeg_turbo)

# Report feature 6: CIEDE2000 colour difference.
add_executable(test_ciede2000 ${CMAKE_SOURCE_DIR}/tests/core/test_ciede2000.cpp)
target_link_libraries(test_ciede2000 PRIVATE mviewer_core)
target_compile_features(test_ciede2000 PRIVATE cxx_std_20)
target_include_directories(test_ciede2000 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME ciede2000_tests COMMAND test_ciede2000)

# Report feature 6: ColorChecker measurement on top of CIEDE2000.
add_executable(test_color_checker ${CMAKE_SOURCE_DIR}/tests/core/test_color_checker.cpp)
target_link_libraries(test_color_checker PRIVATE mviewer_core)
target_compile_features(test_color_checker PRIVATE cxx_std_20)
target_include_directories(test_color_checker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME color_checker_tests COMMAND test_color_checker)

# Report feature 10: 2D FFT spectrum.
add_executable(test_fft2d ${CMAKE_SOURCE_DIR}/tests/core/test_fft2d.cpp)
target_link_libraries(test_fft2d PRIVATE mviewer_core)
target_compile_features(test_fft2d PRIVATE cxx_std_20)
target_include_directories(test_fft2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fft2d_tests COMMAND test_fft2d)

# Report feature 11: portable session package.
add_executable(test_session_package ${CMAKE_SOURCE_DIR}/tests/core/test_session_package.cpp)
target_link_libraries(test_session_package PRIVATE mviewer_core)
target_compile_features(test_session_package PRIVATE cxx_std_20)
target_include_directories(test_session_package PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME session_package_tests COMMAND test_session_package)

# Item 5: undo for a clipboard paste.
add_executable(test_clipboard_paste_command ${CMAKE_SOURCE_DIR}/tests/core/test_clipboard_paste_command.cpp)
target_link_libraries(test_clipboard_paste_command PRIVATE mviewer_core)
target_compile_features(test_clipboard_paste_command PRIVATE cxx_std_20)
target_include_directories(test_clipboard_paste_command PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME clipboard_paste_command_tests COMMAND test_clipboard_paste_command)

# Main-thread delivery: shutdown must drop, not inline, a late worker callback.
add_executable(test_mainthread_dispatcher ${CMAKE_SOURCE_DIR}/tests/core/test_mainthread_dispatcher.cpp)
target_link_libraries(test_mainthread_dispatcher PRIVATE mviewer_core)
target_compile_features(test_mainthread_dispatcher PRIVATE cxx_std_20)
target_include_directories(test_mainthread_dispatcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME mainthread_dispatcher_tests COMMAND test_mainthread_dispatcher)

# Report feature 9: Python script bridge.
add_executable(python_script_runner_tests ${CMAKE_SOURCE_DIR}/tests/test_python_script_runner.cpp)
target_link_libraries(python_script_runner_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_application
        mviewer_ui
)
target_compile_features(python_script_runner_tests PRIVATE cxx_std_20)
target_include_directories(python_script_runner_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME python_script_runner_tests COMMAND python_script_runner_tests)
set_tests_properties(python_script_runner_tests PROPERTIES RUN_SERIAL TRUE)

# Floating overlay corner slots (review: 4 overlay collisions + resize drift).
add_executable(viewer_overlay_layout_tests ${CMAKE_SOURCE_DIR}/tests/test_viewer_overlays.cpp)
target_link_libraries(viewer_overlay_layout_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(viewer_overlay_layout_tests PRIVATE cxx_std_20)
target_include_directories(viewer_overlay_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME viewer_overlay_layout_tests COMMAND viewer_overlay_layout_tests)
set_tests_properties(viewer_overlay_layout_tests PROPERTIES RUN_SERIAL TRUE)

# Item 4: non-Windows ICC (colord / colour directories on Linux).
add_executable(linux_icc_tests ${CMAKE_SOURCE_DIR}/tests/test_linux_icc.cpp)
target_link_libraries(linux_icc_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)
target_compile_features(linux_icc_tests PRIVATE cxx_std_20)
target_include_directories(linux_icc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME linux_icc_tests COMMAND linux_icc_tests)
set_tests_properties(linux_icc_tests PROPERTIES RUN_SERIAL TRUE)

add_executable(test_yuv_sample ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_sample.cpp)
target_link_libraries(test_yuv_sample PRIVATE mviewer_core)
target_compile_features(test_yuv_sample PRIVATE cxx_std_20)
target_include_directories(test_yuv_sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_sample_tests COMMAND test_yuv_sample)

# ADR 032: per-plane YUV PSNR / SSIM and sequence compare.
add_executable(test_yuv_metrics ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_metrics.cpp)
target_link_libraries(test_yuv_metrics PRIVATE mviewer_core)
target_compile_features(test_yuv_metrics PRIVATE cxx_std_20)
target_include_directories(test_yuv_metrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_metrics_tests COMMAND test_yuv_metrics)

# YUV fast paths (integer SSIM, raw thumbnails, threaded RGB, parallel compare,
# y4m resolve cache) must match the reference results exactly.
add_executable(test_yuv_fast_paths ${CMAKE_SOURCE_DIR}/tests/core/test_yuv_fast_paths.cpp)
target_link_libraries(test_yuv_fast_paths PRIVATE mviewer_core)
target_compile_features(test_yuv_fast_paths PRIVATE cxx_std_20)
target_include_directories(test_yuv_fast_paths PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME yuv_fast_paths_tests COMMAND test_yuv_fast_paths)

# ─── QtConvert SIMD & Fast-Path Tests ─────────────────────────────────────────
add_executable(test_qtconvert
    ${CMAKE_SOURCE_DIR}/tests/core/test_qtconvert.cpp
)

target_link_libraries(test_qtconvert
    PRIVATE
        Qt6::Core
        Qt6::Gui
        mviewer_core
)

target_compile_features(test_qtconvert PRIVATE cxx_std_20)
target_include_directories(test_qtconvert PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME qtconvert_tests COMMAND test_qtconvert)

# ─── M15 P0#4 Metadata Center (unified MetadataModel + QTreeView) ───────────
add_executable(test_metadatamodel
    ${CMAKE_SOURCE_DIR}/tests/test_metadatamodel.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_iccprofile.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/core/test_m36_display.cpp
)
target_link_libraries(test_m36_display PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core)
target_compile_features(test_m36_display PRIVATE cxx_std_20)
target_include_directories(test_m36_display PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m36_display_tests COMMAND test_m36_display)

# M36: real QListView Ctrl/Shift selection and SelectionModel integration.
add_executable(m36_browse_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_m36_browse.cpp
)
target_link_libraries(m36_browse_tests PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core mviewer_ui)
target_compile_features(m36_browse_tests PRIVATE cxx_std_20)
target_include_directories(m36_browse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME m36_browse_tests COMMAND m36_browse_tests)
set_tests_properties(m36_browse_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── M15 Sprint 2-1 Search (global index + query engine) ─────────────────────
add_executable(search_tests
    ${CMAKE_SOURCE_DIR}/tests/test_search.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/test_updatechecker.cpp
)

target_link_libraries(test_updatechecker PRIVATE mviewer_core)
target_compile_features(test_updatechecker PRIVATE cxx_std_20)
target_include_directories(test_updatechecker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME updatechecker_tests COMMAND test_updatechecker)

# ─── M15 Sprint 2-2 Batch Workflow (batch processor + dialog) ────────────────
add_executable(batch_tests
    ${CMAKE_SOURCE_DIR}/tests/test_batch.cpp
)

target_link_libraries(batch_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
        mviewer_ui
)

target_compile_features(batch_tests PRIVATE cxx_std_20)
target_include_directories(batch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(batch_tests PRIVATE MVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME batch_tests COMMAND batch_tests)

# ─── M9-6 Polish (shortcuts route via Command) acceptance tests ──────────
add_executable(shortcuts_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_shortcuts.cpp
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

# Audit #36-#39: probe destroy, plugin deleter, importer unload, ABI gate.
add_executable(test_plugin_audit
    ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin_audit.cpp
)
target_link_libraries(test_plugin_audit
    PRIVATE
        Qt6::Core
        mviewer_core
)
target_compile_features(test_plugin_audit PRIVATE cxx_std_20)
target_include_directories(test_plugin_audit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
add_test(NAME plugin_audit_tests COMMAND test_plugin_audit)

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
    ${CMAKE_SOURCE_DIR}/tests/core/test_assets_acceptance.cpp
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
