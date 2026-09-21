#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# build.sh — verify the API, then configure and compile.
#
#   ./scripts/build.sh                 # Release build in ./build
#   ./scripts/build.sh --verbose       # show the full compiler command lines
#
# Configure step resolves the `KWin` CMake package (KWinConfig.cmake) and its
# KWin::kwin target; the "Configured against" line below confirms which KWin
# the build picked up. Override a non-standard location with:
#
#   cmake -B build -DKWin_DIR=/path/to/cmake/KWin

set -euo pipefail

cd "$(dirname "$0")/.."

VERBOSE=""
[[ "${1:-}" == "--verbose" ]] && VERBOSE="--verbose"

echo "==> Checking the installed KWin headers"
if ! ./scripts/verify-api.sh; then
    echo
    echo "Refusing to build against headers that are missing symbols this effect needs."
    echo "Read the table above and README.md -> Troubleshooting."
    exit 1
fi

echo
echo "==> Configuring"
# Explicitly force Qt6/KF6 - ECM's QtVersionOption defaults to Qt5 if
# QT_MAJOR_VERSION is not set early enough (see CMakeLists.txt comment).
cmake -B build \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
  -DQT_MAJOR_VERSION=6 \
  -DKF_MAJOR_VERSION=6 \
  -DBUILD_WITH_QT6=ON

echo
echo "==> Configured against"
kwin_dir="$(cmake -LA -N build 2>/dev/null | awk -F= '/^KWin_DIR:/{print $2}')"
dest="$(cmake -LA -N build 2>/dev/null | awk -F= '/^KWIN_EFFECTS_INSTALL_DIR:/{print $2}')"
echo "    KWin CMake config : ${kwin_dir:-<unknown>}"
echo "    plugin install dir: ${dest:-<unknown>}"

echo
echo "==> Building"
cmake --build build --parallel "${VERBOSE}"

echo
echo "==> Built:"
# KDECMakeSettings may place the library in build/bin/ or build/lib/; search
# all likely locations.
so_file=$(find build -maxdepth 3 -name kwin4_effect_software_dim.so -type f | head -n 1)
if [[ -n "$so_file" ]]; then
    ls -l "$so_file"
    # Ensure the expected path also exists for scripts that hardcode it
    if [[ "$so_file" != "build/kwin4_effect_software_dim.so" ]]; then
        echo "    (also linking to build/kwin4_effect_software_dim.so for compatibility)"
        ln -sf "$(realpath --relative-to=build "$so_file")" build/kwin4_effect_software_dim.so 2>/dev/null || cp "$so_file" build/kwin4_effect_software_dim.so
    fi
else
    echo "!! Could not find kwin4_effect_software_dim.so under build/" >&2
    find build -name "*.so" -type f | head -n 20
    exit 1
fi
echo
echo "Next:  ./scripts/install.sh"
