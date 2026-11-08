#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build a downloadable Arch/CachyOS package without installing it.
set -euo pipefail
cd "$(dirname "$0")/.."
repo_dir=$PWD
version=1.2.0
for tool in makepkg pacman cmake tar sha256sum; do
    command -v "$tool" >/dev/null || { printf 'Missing build tool: %s\n' "$tool" >&2; exit 1; }
done
if [[ $(uname -m) != x86_64 ]]; then
    printf 'This package currently supports x86_64 only.\n' >&2
    exit 1
fi

# Isolate makepkg intermediates and avoid overwriting an existing build.
mkdir -p dist/arch
package_dir=$(mktemp -d "$repo_dir/dist/arch/release-XXXXXX")
archive="$package_dir/dimsum-$version.tar.gz"
tar --transform="s,^,dimsum-$version/," -czf "$archive" \
    CMakeLists.txt LICENSE README.md CHANGELOG.md src tests scripts packaging dimsum-logo.svg
cp packaging/arch/PKGBUILD packaging/arch/dimsum.install "$package_dir/"
source_hash=$(sha256sum "$archive")
source_hash=${source_hash%% *}
sed -i "s/SOURCE_SHA256/$source_hash/" "$package_dir/PKGBUILD"
cd "$package_dir"
makepkg --cleanbuild --noconfirm
printf '\nUpload this package to KDE Store or a GitHub release:\n'
makepkg --packagelist
printf '\nInstall locally with: sudo pacman -U <package-file>\n'
