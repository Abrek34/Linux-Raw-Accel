#!/bin/bash
# Quick build script (no cmake required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD="$ROOT/build-manual"

EVDEV_CFLAGS="$(pkg-config --cflags libevdev)"
EVDEV_LIBS="$(pkg-config --libs libevdev)"
GTK4_CFLAGS="$(pkg-config --cflags gtk4)"
GTK4_LIBS="$(pkg-config --libs gtk4)"

CXX="${CXX:-g++}"
# -march=native optimises for this machine's CPU but breaks portability.
# Set RAWACCEL_PORTABLE=1 to build a portable binary (e.g. for packaging).
if [ "${RAWACCEL_PORTABLE:-0}" = "1" ]; then
    MARCH=""
else
    MARCH="-march=native"
fi
CXXFLAGS="-std=c++20 -O3 $MARCH -Wall -Wextra -Wno-unused-parameter -I$ROOT/include"

mkdir -p "$BUILD"

echo "[1/3] Building rawaccel-daemon..."
$CXX $CXXFLAGS $EVDEV_CFLAGS \
    "$ROOT/src/config.cpp" \
    "$ROOT/daemon/daemon.cpp" \
    "$ROOT/daemon/main.cpp" \
    $EVDEV_LIBS -lpthread \
    -o "$BUILD/rawaccel-daemon"

echo "[2/3] Building rawaccel-cli..."
$CXX $CXXFLAGS \
    "$ROOT/src/config.cpp" \
    "$ROOT/cli/main.cpp" \
    -o "$BUILD/rawaccel-cli"

echo "[3/3] Building rawaccel-gui..."
$CXX $CXXFLAGS $GTK4_CFLAGS \
    "$ROOT/src/config.cpp" \
    "$ROOT/gui/main.cpp" \
    $GTK4_LIBS \
    -o "$BUILD/rawaccel-gui"

echo ""
echo "Build complete! Binaries in $BUILD/"
echo ""
echo "  $BUILD/rawaccel-daemon   — run as root or with input group"
echo "  $BUILD/rawaccel-cli      — CLI config tool"
echo "  $BUILD/rawaccel-gui      — GUI"
