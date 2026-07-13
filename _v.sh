#!/bin/bash
# MViewer build + test script (Milestone 2 refactor verification).
# Builds the whole project then runs the core test suites headless.
set -u

export PATH="/c/tools/msys64/mingw64/bin:$PATH"
cd "$(dirname "$0")"

echo "=== CMAKE BUILD (all targets) ==="
cmake --build build -j4
BUILD_EXIT=$?
if [ "$BUILD_EXIT" -ne 0 ]; then
    echo "BUILD_EXIT=$BUILD_EXIT"
    exit "$BUILD_EXIT"
fi
echo "BUILD_EXIT=0"

run_test() {
    local name="$1"; local bin="$2"
    echo "=== RUN $name ==="
    QT_QPA_PLATFORM=offscreen "$bin"
    local rc=$?
    echo "${name}_EXIT=$rc"
    return $rc
}

rc=0
run_test core_tests ./build/bin/core_tests.exe || rc=1

if [ -f ./build/bin/mviewer_unit_tests.exe ]; then
    run_test mviewer_unit_tests ./build/bin/mviewer_unit_tests.exe || rc=1
fi

echo "ALL_TEST_EXIT=$rc"
exit $rc
