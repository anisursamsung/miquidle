#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "==> Configuring miquidle ($BUILD_TYPE)..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX="/usr" "$@"

echo "==> Building miquidle..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Build finished successfully! Binary is at $BUILD_DIR/miquidle"

# If invoked with sudo/root, automatically install to system
if [ "${EUID}" -eq 0 ]; then
    echo "==> Installing miquidle to system (/usr/bin/miquidle)..."
    cmake --install "$BUILD_DIR"
    echo "==> miquidle successfully installed to /usr/bin/miquidle!"
else
    echo "==> Built locally in $BUILD_DIR. To install system-wide, run: sudo ./make.sh"
fi
