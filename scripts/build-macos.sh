#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OBS_SRC="$PROJECT_ROOT/deps/obs-studio"
OBS_BUILD="$PROJECT_ROOT/build/obs"
WW_BUILD="$PROJECT_ROOT/build/watchwith"

DEPS_VERSION="2025-08-23"
DEPS_BASE_URL="https://github.com/obsproject/obs-deps/releases/download"
DEPS_DIR="$PROJECT_ROOT/.deps"

ARCH="$(uname -m)"
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "=== WatchWith Build Script (macOS $ARCH) ==="
echo ""

# ── Prerequisites ──────────────────────────────────────────────────

if ! xcode-select -p &>/dev/null; then
    echo "Error: Xcode Command Line Tools not installed."
    echo "  Run:  xcode-select --install"
    echo "  Then re-run this script."
    exit 1
fi

if ! command -v cmake &>/dev/null; then
    echo "Error: CMake not found."
    echo "  Install via Homebrew:  brew install cmake"
    echo "  Or download from:      https://cmake.org/download/"
    exit 1
fi

echo "  cmake:  $(cmake --version | head -1)"
echo "  arch:   $ARCH"
echo "  cores:  $NCPU"

# ── Helper ─────────────────────────────────────────────────────────

download_and_extract() {
    local url="$1" dest="$2" label="$3"

    if [ -d "$dest/include" ] || [ -d "$dest/lib" ]; then
        echo "  [skip] $label already present"
        return
    fi

    local tmp="/tmp/ww-$label.tar.xz"
    echo "  Downloading $label ..."
    curl -fSL -o "$tmp" "$url"
    echo "  Extracting to $dest ..."
    mkdir -p "$dest"
    tar xf "$tmp" -C "$dest"
    rm -f "$tmp"
}

# ── Step 1: Submodules ─────────────────────────────────────────────

echo ""
echo "[1/5] Initializing submodules..."
cd "$PROJECT_ROOT"
git submodule update --init --depth 1 deps/obs-studio  2>/dev/null || true
git submodule update --init --depth 1 deps/libdatachannel 2>/dev/null || true
git -C deps/libdatachannel submodule update --init --depth 1 2>/dev/null || true

# ── Step 2: Download OBS prebuilt deps ─────────────────────────────

echo ""
echo "[2/5] Downloading OBS dependencies..."

OBS_DEPS_DIR="$DEPS_DIR/macos-deps-$DEPS_VERSION-$ARCH"
QT_DEPS_DIR="$DEPS_DIR/macos-deps-qt6-$DEPS_VERSION-$ARCH"

download_and_extract \
    "$DEPS_BASE_URL/$DEPS_VERSION/macos-deps-$DEPS_VERSION-$ARCH.tar.xz" \
    "$OBS_DEPS_DIR" "obs-deps"

download_and_extract \
    "$DEPS_BASE_URL/$DEPS_VERSION/macos-deps-qt6-$DEPS_VERSION-$ARCH.tar.xz" \
    "$QT_DEPS_DIR" "obs-deps-qt6"

PREFIX_PATH="$OBS_DEPS_DIR;$QT_DEPS_DIR"

# ── Step 3: Prepare OBS source ─────────────────────────────────────

echo ""
echo "[3/5] Preparing OBS source..."

for sub in obs-browser obs-websocket; do
    STUB_DIR="$OBS_SRC/plugins/$sub"
    STUB_FILE="$STUB_DIR/CMakeLists.txt"
    if [ ! -f "$STUB_FILE" ]; then
        mkdir -p "$STUB_DIR"
        cat > "$STUB_FILE" <<STUBEOF
# Auto-generated stub
add_custom_target($sub)
if(COMMAND target_disable)
  target_disable($sub)
endif()
STUBEOF
        echo "  Created stub for $sub"
    fi
done

cd "$OBS_SRC"
if ! git describe --tags --abbrev=0 &>/dev/null; then
    echo "  Fetching OBS version tags..."
    git fetch --deepen=100 origin 2>/dev/null || true
    git fetch --tags origin       2>/dev/null || true
fi
cd "$PROJECT_ROOT"

# ── Step 4: Build OBS ─────────────────────────────────────────────

echo ""
echo "[4/5] Building OBS (libobs + plugins)..."

if [ ! -f "$OBS_BUILD/libobs/libobs.dylib" ]; then
    cmake -S "$OBS_SRC" -B "$OBS_BUILD" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        "-DCMAKE_PREFIX_PATH=$PREFIX_PATH" \
        "-DCMAKE_OSX_ARCHITECTURES=$ARCH" \
        -DENABLE_UI=OFF \
        -DENABLE_SCRIPTING=OFF \
        -DENABLE_BROWSER=OFF \
        -DENABLE_WEBSOCKET=OFF

    cmake --build "$OBS_BUILD" -j "$NCPU"
else
    echo "  [skip] OBS already built"
fi

# ── Step 5: Build WatchWith ────────────────────────────────────────

echo ""
echo "[5/5] Building WatchWith..."

cmake -S "$PROJECT_ROOT" -B "$WW_BUILD" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    "-DCMAKE_PREFIX_PATH=$PREFIX_PATH" \
    "-DCMAKE_OSX_ARCHITECTURES=$ARCH" \
    "-DOBS_BUILDDIR=$OBS_BUILD"

cmake --build "$WW_BUILD" --target watchwith -j "$NCPU"

echo ""
echo "=== Build complete! ==="
echo "To run:  ./scripts/run-macos.sh"
echo ""
