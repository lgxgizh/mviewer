#!/usr/bin/env bash
# Single entry point for all build/test commands
# Delegates to PowerShell (canonical build tool)
#
# Usage:
#   bash build.sh          # Release build
#   bash build.sh Debug    # Debug build
#   bash build.sh Test     # Run tests
#   bash build.sh Clean    # Clean build dir

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Run PowerShell build script if available (Windows / WSL environment)
if command -v powershell.exe >/dev/null 2>&1; then
  exec powershell.exe \
    -NoProfile \
    -ExecutionPolicy Bypass \
    -File "$SCRIPT_DIR/build.ps1" \
    "$@"
elif command -v pwsh >/dev/null 2>&1 && [[ "$(uname -s)" =~ ^(CYGWIN|MINGW|MSYS) ]]; then
  exec pwsh \
    -NoProfile \
    -ExecutionPolicy Bypass \
    -File "$SCRIPT_DIR/build.ps1" \
    "$@"
fi

# Native POSIX environment fallback (Linux / macOS without powershell.exe)
TASK="${1:-Release}"
BUILD_DIR="$SCRIPT_DIR/build"
CONFIG="Release"
if [ "$TASK" = "Debug" ]; then
  CONFIG="Debug"
fi

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

case "$TASK" in
  Clean)
    echo "[Clean] Removing $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
    echo "[Clean] Done"
    ;;
  Test)
    echo "[Build] $CONFIG (native POSIX)..."
    mkdir -p "$BUILD_DIR"
    cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
    cmake --build "$BUILD_DIR" -j "$NPROC"
    echo "[Test] Running CTest..."
    export QT_QPA_PLATFORM=offscreen
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    ;;
  *)
    echo "[Build] $CONFIG (native POSIX)..."
    mkdir -p "$BUILD_DIR"
    cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
    cmake --build "$BUILD_DIR" -j "$NPROC"
    echo "[Build] OK - $CONFIG"
    ;;
esac
