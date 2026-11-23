# ─── M10 Performance Engineering ─────────────────────────────────────────────
# MemoryTracker + benchmark scenario structural tests are folded into core_tests
# (via ${CMAKE_SOURCE_DIR}/tests/core/test_m10.cpp) — the known-good consolidated exe link environment.
#
# mviewer_bench: standalone perf harness (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
# CI only runs --smoke (proves it links + runs). The --enforce regression gate
# is roadmap Phase-4 and is NOT wired into ci.yml here.
add_executable(mviewer_bench
    benchmark/main.cpp
    benchmark/bench_progress.cpp
    benchmark/corpus.cpp
    benchmark/scenarios.cpp
    benchmark/scenarios_e2e.cpp
    benchmark/scenarios_pipeline.cpp
    benchmark/scenarios_render.cpp
    core/perf/GpuTracker.cpp
)

target_link_libraries(mviewer_bench
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        mviewer_core
        mviewer_ui
        $<$<PLATFORM_ID:Windows>:dxgi;ole32>
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
# TIMEOUT is headroom above the in-process per-scenario watchdog (default
# 120s, mviewer_bench --scenario-timeout-ms). The watchdog prints a flushed
# `[bench] WATCHDOG <name> exceeded N ms` line and exits non-zero; that is
# the hang guard. 900s lets ctest capture that line. It is not a looser
# performance budget.
set_tests_properties(bench_enforce PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)

add_executable(audit_misc_tests
    ${CMAKE_SOURCE_DIR}/tests/core/test_audit_misc.cpp
)
target_link_libraries(audit_misc_tests
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        mviewer_core
)
target_compile_features(audit_misc_tests PRIVATE cxx_std_20)
target_include_directories(audit_misc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME audit_misc_tests COMMAND audit_misc_tests)
set_tests_properties(audit_misc_tests PROPERTIES RUN_SERIAL TRUE)

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
        "${_mviewer_root}/cache"
        "${_mviewer_root}/home")
    set_tests_properties(${_mviewer_test} PROPERTIES
        ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;MVIEWER_TEST_RUNTIME_ROOT=${_mviewer_root};APPDATA=${_mviewer_root}/appdata;LOCALAPPDATA=${_mviewer_root}/localappdata;TEMP=${_mviewer_root}/temp;TMP=${_mviewer_root}/temp;XDG_CONFIG_HOME=${_mviewer_root}/config;XDG_DATA_HOME=${_mviewer_root}/data;XDG_CACHE_HOME=${_mviewer_root}/cache;HOME=${_mviewer_root}/home")
endforeach()

# ─── Nightly-only tests (review P1-3) ───────────────────────────────────────
# Long soak / torture / perf-enforcement suites carry the "nightly" label. The
# PR gate runs `ctest -LE nightly`; nightly `quality` and the release workflow
# still run the full suite through build.ps1 Test.
set(MVIEWER_NIGHTLY_ONLY_TESTS
    bench_enforce
    m51_rc_soak
    m27_lifecycle_torture
    m47_large_soak
    m53_large_source_soak)
foreach(_mviewer_test IN LISTS MVIEWER_NIGHTLY_ONLY_TESTS)
    if(TEST ${_mviewer_test})
        set_property(TEST ${_mviewer_test} APPEND PROPERTY LABELS nightly)
    endif()
endforeach()

# Real preview Decode + current LOD + visible thumbnails, large DPR2 paint costs.
mviewer_add_test(concurrent_view_cost_tests
    SRCS ${CMAKE_SOURCE_DIR}/tests/test_concurrent_view_cost.cpp
    LIBS mviewer_ui
    TIMEOUT 180 SERIAL)
