#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OBS_BUILD="$PROJECT_ROOT/build/obs"
WW_BUILD="$PROJECT_ROOT/build/watchwith"

DEPS_VERSION="2025-08-23"
ARCH="$(uname -m)"
DEPS_DIR="$PROJECT_ROOT/.deps"
OBS_DEPS="$DEPS_DIR/macos-deps-$DEPS_VERSION-$ARCH"
QT_DEPS="$DEPS_DIR/macos-deps-qt6-$DEPS_VERSION-$ARCH"

# Find the executable (post-build copy or direct build output)
EXE="$OBS_BUILD/rundir/RelWithDebInfo/bin/watchwith"
if [ ! -f "$EXE" ]; then
    EXE="$WW_BUILD/watchwith"
fi
if [ ! -f "$EXE" ]; then
    echo "Error: watchwith not found."
    echo "  Run:  ./scripts/build-macos.sh"
    exit 1
fi

# Kill any existing instance
killall watchwith 2>/dev/null || true
sleep 0.3

# Library search paths so the executable can find OBS + FFmpeg + Qt dylibs
export DYLD_LIBRARY_PATH="${OBS_BUILD}/libobs:${OBS_BUILD}/libobs-opengl:${OBS_DEPS}/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_FRAMEWORK_PATH="${QT_DEPS}/lib:${OBS_DEPS}/lib:${DYLD_FRAMEWORK_PATH:-}"

# OBS needs a working directory relative to its rundir to find plugins and data
RUNDIR="$OBS_BUILD/rundir/RelWithDebInfo"

cd "$RUNDIR"
exec "$EXE"
