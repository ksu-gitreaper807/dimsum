#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# install.sh — build if needed, then install the plugin where KWin looks for it.
#
#   ./scripts/install.sh
#
# KWin loads native effect plugins from the *Qt system plugin directory*, so a
# user-local install does not work reliably: ~/.local/lib is not on Qt's plugin
# search path, and working around that means exporting QT_PLUGIN_PATH before
# KWin starts. This script therefore needs sudo. See README.md ->
# "Why this needs sudo".

set -euo pipefail

cd "$(dirname "$0")/.."

so_file=$(find build -maxdepth 3 -name kwin4_effect_software_dim.so -type f | head -n 1)
if [[ -z "$so_file" ]]; then
    echo "==> No build found, building first"
    ./scripts/build.sh
    so_file=$(find build -maxdepth 3 -name kwin4_effect_software_dim.so -type f | head -n 1)
fi

if [[ -z "$so_file" ]]; then
    echo "!! Build did not produce kwin4_effect_software_dim.so" >&2
    exit 1
fi

# Ask the already-configured build where it intends to install.
dest="$(cmake -LA -N build 2>/dev/null | awk -F= '/^KWIN_EFFECTS_INSTALL_DIR:/{print $2}')"
if [[ -z "$dest" ]]; then
    echo "!! Could not read KWIN_EFFECTS_INSTALL_DIR from build/. Re-run ./scripts/build.sh" >&2
    exit 1
fi
echo "==> Install destination: /$dest"

echo "==> Installing (needs root)"
sudo cmake --install build

# --------------------------------------------------------------------------
# Remove the old scripted prototype if it is still around. A KPackage effect
# and a native plugin must not share the id kwin4_effect_software_dim.
# --------------------------------------------------------------------------
if kpackagetool6 --type=KWin/Effect --list 2>/dev/null | grep -q '^kwin4_effect_software_dim$'; then
    echo
    echo "==> Found the old scripted prototype kwin4_effect_software_dim"
    echo "    A KPackage and a native plugin cannot share an effect id."
    read -r -p "    Remove the scripted prototype now? [Y/n] " answer
    case "${answer:-Y}" in
        [nN]*) echo "    Left in place — unload it manually before testing." ;;
        *)     kpackagetool6 --type=KWin/Effect --remove kwin4_effect_software_dim \
               || echo "    !! kpackagetool6 --remove failed; remove it manually." ;;
    esac
fi

echo
echo "==> Installed. Next steps:"
echo "      ./scripts/test.sh      # stage-by-stage load / toggle / unload check"
echo
echo "    Enable it now without a restart:"
echo "      kwriteconfig6 --file kwinrc --group Plugins --key kwin4_effect_software_dimEnabled true"
echo "      qdbus6 org.kde.KWin /KWin reconfigure"
