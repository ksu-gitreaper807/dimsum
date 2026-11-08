# KDE Store publishing

Product title: **DimSum — Not Recommended for Moths**

Published on 2026-11-08 as **DimSum - Not Recommended for Moths**:
<https://store.kde.org/p/2381398/>. The store requires an ASCII hyphen in titles.
Release: <https://github.com/ksu-gitreaper807/dimsum/releases/tag/v1.2.0>.
The release source commit is `512bbd875317f049fc6b6793b57c827c01690be9`.

Category: **KWin Effects** (select the Plasma 6 subcategory if offered).
License: **MIT**. Source: <https://github.com/ksu-gitreaper807/dimsum>.
Use [description.txt](description.txt) for the product description and
[CHANGELOG.md](../../CHANGELOG.md) for the release notes.

## Release order

1. Commit and push the matching KDE source to `main` before publishing artifacts.
2. Run `bash scripts/package-arch.sh`. Its clean source build also runs CTest.
3. Publish GitHub tag/release `v1.2.0` at that commit, with:
   - `dimsum-1.2.0-3-x86_64.pkg.tar.zst`: Arch/CachyOS, **KWin 6.8.2 only**.
   - `dimsum-1.2.0.tar.gz`: complete source, including tests, build scripts,
     packaging files, README, and MIT license.
   - `SHA256SUMS`: checksums of those two downloads.
4. Sign in to KDE Store. Update an existing DimSum product instead of creating a
   duplicate. Otherwise create a product in the category above.
5. Link the public source and release, select the MIT license, and upload both
   binary and source downloads with clear compatibility labels. Do not mark the
   binary or source download as a scripted KPackage / automatic KNewStuff install.
6. Verify the public listing and its downloads after submission. Record the final
   product URL here once it exists.

## KDE Store filenames and checksums

KDE Store normalises underscores in filenames, so its binary is named
`dimsum-1.2.0-3-x86-64.pkg.tar.zst`. The content matches the GitHub binary
`dimsum-1.2.0-3-x86_64.pkg.tar.zst`; only the filename differs. The Store has
`SHA256SUMS-kde.txt`, using its filenames, while GitHub has `SHA256SUMS`, using
the original filenames. The source archive is `dimsum-1.2.0.tar.gz` on both.

The store's login may require a human bot check. Do not store credentials in the
repository or include them in release artifacts. Account terms and login prompts
must be completed by the account owner when required.

## Important distinction

KDE's [sharing guide](https://develop.kde.org/docs/plasma/kwineffect/)
describes JS/QML effects as KPackages. DimSum uses the native C++ KWin plugin
ABI instead. A listing in the Desktop Effects category is a distribution page,
not a claim that the “Get New…” KPackage installer can install this native
plugin. Native installation requires a system package or a build/install step.
KWin's [KNewStuff configuration](https://github.com/KDE/kwin/blob/v6.8.2/src/kcms/effects/kwineffect.knsrc)
uses `KPackageStructure=KWin/Effect` for that installer.
