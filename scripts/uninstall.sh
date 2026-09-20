#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# uninstall.sh — unload the effect, remove the plugin, drop the config group.
#
#   ./scripts/uninstall.sh
#
# Safe to run twice. Unloading before removing means KWin is never left holding
# a plugin whose .so disappeared from under it.

set -uo pipefail

cd "$(dirname "$0")/.."

ID="kwin4_effect_software_dim"

echo "==> Unloading the running effect (if any)"
qdbus6 org.kde.KWin /Effects unloadEffect "$ID" 2>/dev/null \
    && echo "    unloadEffect -> ok" \
    || echo "    not loaded / KWin not reachable (fine)"

echo
echo "==> Disabling it in kwinrc"
kwriteconfig6 --file kwinrc --group Plugins --key "${ID}Enabled" false 2>/dev/null \
    && echo "    ${ID}Enabled = false" \
    || echo "    kwriteconfig6 unavailable; edit ~/.config/kwinrc by hand"
qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || true

echo
echo "==> Removing the plugin"
dest="$(cmake -LA -N build 2>/dev/null | awk -F= '/^KWIN_EFFECTS_INSTALL_DIR:/{print $2}')"
if [[ -n "$dest" ]]; then
    for f in "/$dest/$ID.so" "/$dest/lib$ID.so"; do
        if [[ -e "$f" ]]; then
            sudo rm -v "$f"
        fi
    done
    sudo rm -rfv "/usr/share/kwin/effects/metadata.json/$ID" 2>/dev/null || true
else
    echo "    No build/ directory to read the install path from; removing from the"
    echo "    usual locations instead."
    for d in /usr/lib/qt6/plugins/kwin/effects/plugins \
             /usr/lib64/qt6/plugins/kwin/effects/plugins \
             /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins \
             /usr/lib/qt6/plugins/kwin/effects/lib \
             /usr/lib64/qt6/plugins/kwin/effects/lib \
             /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/lib; do
        # (the effects/lib entries are pre-port leftovers — never a 6.7 load path)
        for f in "$d/$ID.so" "$d/lib$ID.so"; do
            [[ -e "$f" ]] && sudo rm -v "$f"
        done
    done
    sudo rm -rfv "/usr/share/kwin/effects/metadata.json/$ID" 2>/dev/null || true
fi

echo
echo "==> Removing the leftover scripted prototype, if it exists"
if kpackagetool6 --type=KWin/Effect --list 2>/dev/null | grep -q "^$ID$"; then
    kpackagetool6 --type=KWin/Effect --remove "$ID" || echo "    !! manual removal needed"
else
    echo "    none"
fi

echo
echo "==> Done. A logout/login (or a restart of the Plasma session) clears any"
echo "    cached plugin metadata. Compositing itself is untouched either way:"
echo "    unloading this effect always restores normal rendering immediately."
