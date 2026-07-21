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

# Run PowerShell build script
exec powershell.exe \
  -NoProfile \
  -ExecutionPolicy Bypass \
  -File "$SCRIPT_DIR/build.ps1" \
  "$@"
