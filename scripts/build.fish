#!/usr/bin/env fish
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# build.fish — verify the API, then configure and compile.
#
#   ./scripts/build.fish                 # Release build in ./build
#   ./scripts/build.fish --verbose       # show the full compiler command lines

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
echo "==> Building"
cmake --build build --parallel $verbose
or exit $status

echo
echo "==> Built:"
ls -l build/kwin4_effect_software_dim.so
or exit $status
echo
echo "Next:  ./scripts/install.sh"
