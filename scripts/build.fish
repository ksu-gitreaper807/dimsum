#!/usr/bin/env fish
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# build.fish — verify the API, then configure and compile.
#
#   ./scripts/build.fish                 # Release build in ./build
#   ./scripts/build.fish --verbose       # show the full compiler command lines
#
# Configure step resolves the `KWin` CMake package (KWinConfig.cmake) and its
# KWin::kwin target; the "Configured against" lines below confirm which KWin
# the build picked up. Override a non-standard location with:
#
#   cmake -B build -DKWin_DIR=/path/to/cmake/KWin

cd (dirname (status --current-filename))/..
or exit 1

set verbose
if test (count $argv) -gt 0; and test "$argv[1]" = "--verbose"
    set verbose --verbose
end

echo "==> Checking the installed KWin headers"
if not ./scripts/verify-api.fish
    echo
    echo "Refusing to build against headers that are missing symbols this effect needs."
    echo "Read the table above and README.md -> Troubleshooting."
    exit 1
end

echo
echo "==> Configuring"
set build_type Release
if set -q CMAKE_BUILD_TYPE; and test -n "$CMAKE_BUILD_TYPE"
    set build_type "$CMAKE_BUILD_TYPE"
end
cmake -B build -DCMAKE_BUILD_TYPE="$build_type"
or exit $status

echo
echo "==> Configured against"
set kwin_dir (cmake -LA -N build 2>/dev/null | awk -F= '/^KWin_DIR:/{print $2}')
set dest (cmake -LA -N build 2>/dev/null | awk -F= '/^KWIN_EFFECTS_INSTALL_DIR:/{print $2}')
if test -z "$kwin_dir"
    set kwin_dir "<unknown>"
end
if test -z "$dest"
    set dest "<unknown>"
end
echo "    KWin CMake config : $kwin_dir"
echo "    plugin install dir: $dest"

echo
echo "==> Building"
cmake --build build --parallel $verbose
or exit $status

echo
echo "==> Built:"
ls -l build/kwin4_effect_software_dim.so
or exit $status
echo
echo "Next:  ./scripts/install.sh"
