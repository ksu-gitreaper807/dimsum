#!/usr/bin/env fish
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# install.fish — build if needed, then install the plugin where KWin looks for it.
#
#   ./scripts/install.fish
#
# KWin loads native effect plugins from the *Qt system plugin directory*, so a
# user-local install does not work reliably: ~/.local/lib is not on Qt's plugin
# search path, and working around that means exporting QT_PLUGIN_PATH before
# KWin starts. This script therefore needs sudo. See README.md ->
# "Why this needs sudo".

cd (dirname (status --current-filename))/..
or exit 1

set so_file (find build -maxdepth 3 -name kwin4_effect_software_dim.so -type f 2>/dev/null | head -n 1)
if test -z "$so_file"
    echo "==> No build found, building first"
    ./scripts/build.fish
    or exit $status
    set so_file (find build -maxdepth 3 -name kwin4_effect_software_dim.so -type f 2>/dev/null | head -n 1)
end

if test -z "$so_file"
    echo "!! Build did not produce kwin4_effect_software_dim.so" >&2
    exit 1
end

# Ask the already-configured build where it intends to install.
set dest (cmake -LA -N build 2>/dev/null | awk -F= '/^KWIN_EFFECTS_INSTALL_DIR:/{print $2}')
if test -z "$dest"
    echo "!! Could not read KWIN_EFFECTS_INSTALL_DIR from build/. Re-run ./scripts/build.fish" >&2
    exit 1
end
echo "==> Install destination: /$dest"

echo "==> Installing (needs root)"
sudo cmake --install build
or exit $status

# --------------------------------------------------------------------------
# Remove the old scripted prototype if it is still around. A KPackage effect
# and a native plugin must not share the id kwin4_effect_software_dim.
# --------------------------------------------------------------------------
if kpackagetool6 --type=KWin/Effect --list 2>/dev/null | grep -q '^kwin4_effect_software_dim$'
    echo
    echo "==> Found the old scripted prototype kwin4_effect_software_dim"
    echo "    A KPackage and a native plugin cannot share an effect id."
    read -l -P "    Remove the scripted prototype now? [Y/n] " answer
    if test -z "$answer"
        set answer Y
    end
    switch "$answer"
        case n N
            echo "    Left in place — unload it manually before testing."
        case '*'
            kpackagetool6 --type=KWin/Effect --remove kwin4_effect_software_dim
            or echo "    !! kpackagetool6 --remove failed; remove it manually."
    end
end

echo
echo "==> Installed. Next steps:"
echo "      ./scripts/test.sh      # stage-by-stage load / toggle / unload check"
echo
echo "    Enable it now without a restart:"
echo "      kwriteconfig6 --file kwinrc --group Plugins --key kwin4_effect_software_dimEnabled true"
echo "      qdbus6 org.kde.KWin /KWin reconfigure"
