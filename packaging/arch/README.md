# Arch Linux / CachyOS download

Run `bash scripts/package-arch.sh` from the repository root. This creates a
checksummed source snapshot and builds a standard x86_64 pacman package under
`dist/arch/release-*/`. No installation or system configuration is performed.
Install the build dependencies listed in `PKGBUILD` first if makepkg reports any
missing dependencies. The builder needs the standard Arch `base-devel` group.

Upload the resulting `dimsum-1.2.0-3-x86_64.pkg.tar.zst` to the KDE Store product's
download section and/or attach it to a GitHub release. Users download that one
file. They do not need a compiler or source checkout.

Install with a local-package GUI if one is installed, or run:

```bash
sudo pacman -U ./dimsum-1.2.0-3-x86_64.pkg.tar.zst
```

Arch itself does not ship a graphical local-package installer by default.
Downloading is a single click; graphical installation depends on the user's
package manager and requires authentication.

Enable **Software Dim** in System Settings > Desktop Effects. Press
`Meta+Alt+D` to toggle dimming, `Meta+Alt+F6` for darker and `Meta+Alt+F7` for
brighter. A popup shows the brightness level. When replacing a plugin already
loaded into KWin, log out and back in to load the new library.

Each binary depends on the exact upstream KWin version used to build it
(for example, `kwin=6.8.0`, allowing different distro package releases of 6.8.0).
Build a new package for each KWin version and increment `pkgrel` when publishing
a rebuild of the same DimSum version. A mismatched package must not be installed
using dependency overrides. Arch and CachyOS still need compatible Qt and KDE
libraries; a successful local build is not proof of compatibility with older
systems. For broad releases, build and test on a clean Arch system as well.

If upgrading from `scripts/install.sh`, pacman may report conflicting unowned
plugin and metadata files. Check ownership first:

```bash
pacman -Qo /usr/lib/qt6/plugins/kwin/effects/plugins/kwin4_effect_software_dim.so
pacman -Qo /usr/share/kwin/effects/metadata.json/kwin4_effect_software_dim/metadata.json
```

Only if these are **unowned files from your manual DimSum install**, remove
those two generated installation files with per-file confirmation, then install
the package. They can be restored by reinstalling from source. Do not remove
files owned by another package or use a blanket `--overwrite` option.

```bash
sudo rm -i -- /usr/lib/qt6/plugins/kwin/effects/plugins/kwin4_effect_software_dim.so \
  /usr/share/kwin/effects/metadata.json/kwin4_effect_software_dim/metadata.json
```

Package installation does not alter per-user settings. Log out and back in
after the upgrade; replacing the file does not replace an already loaded library.

Remove the managed installation with `sudo pacman -R dimsum`.
