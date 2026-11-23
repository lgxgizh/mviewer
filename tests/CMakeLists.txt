# Test targets, split by layer so no single file grows without bound
# (review P2-10). Included from src/CMakeLists.txt, not add_subdirectory,
# so CMAKE_CURRENT_SOURCE_DIR still means the product source root.
#
#   tests/core.cmake     per-module core suites and their acceptance tests
#   tests/ui.cmake       ctest registration and UI-level workflow tests
#   tests/workflow.cmake performance engineering and nightly-only tests

include(${CMAKE_SOURCE_DIR}/tests/core.cmake)
include(${CMAKE_SOURCE_DIR}/tests/ui.cmake)
include(${CMAKE_SOURCE_DIR}/tests/workflow.cmake)
