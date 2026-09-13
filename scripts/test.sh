#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# test.sh — the six acceptance stages, in order, stopping at the first failure.
#
#   ./scripts/test.sh           # full run, including the interactive visual checks
#   ./scripts/test.sh --quick   # stages 1-3 only (no keypresses, no eyeballing)
#
# Recovery at any point:
#   qdbus6 org.kde.KWin /Effects unloadEffect kwin4_effect_software_dim

set -uo pipefail

ID="kwin4_effect_software_dim"
QUICK="${1:-}"

BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; OFF=$'\033[0m'
stage() { printf '\n%s--- %s ---%s\n' "$BOLD" "$*" "$OFF"; }
pass()  { printf '  %sPASS%s  %s\n' "$GREEN" "$OFF" "$*"; }
fail()  { printf '  %sFAIL%s  %s\n' "$RED" "$OFF" "$*"; }
warn()  { printf '  %sWARN%s  %s\n' "$YELLOW" "$OFF" "$*"; }
die()   { printf '  %sFAIL%s  %s\n' "$RED" "$OFF" "$*"; printf '\nAborting.\n'; exit 1; }
pause() { [[ "$QUICK" == "--quick" ]] && return 0; read -r -p "  $* [Enter] " _; }

# --------------------------------------------------------------------------
stage "Stage 1 — plugin is installed where KWin looks"

found=""
for d in /usr/lib/qt6/plugins/kwin/effects/lib \
         /usr/lib64/qt6/plugins/kwin/effects/lib \
         /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/lib \
         "$HOME/.local/lib/qt6/plugins/kwin/effects/lib"; do
    for f in "$d/$ID.so" "$d/lib$ID.so"; do
        [[ -e "$f" ]] && { found="$f"; break 2; }
    done
done

if [[ -n "$found" ]]; then
    pass "$found"
else
    fail "$ID.so not found in any known effect plugin directory"
    echo "        Run ./scripts/install.sh"
    exit 1
fi

# The scripted prototype must be gone, or the two collide on the same id.
if kpackagetool6 --type=KWin/Effect --list 2>/dev/null | grep -q "^$ID$"; then
    warn "a scripted KPackage with the same id is still installed"
    echo "        kpackagetool6 --type=KWin/Effect --remove $ID"
fi

# --------------------------------------------------------------------------
stage "Stage 2 — KWin loads the effect"

if ! command -v qdbus6 >/dev/null 2>&1; then
    die "qdbus6 not on PATH (on Arch it comes from qtchooser / qt6-tools)"
fi

load="$(qdbus6 org.kde.KWin /Effects loadEffect "$ID" 2>&1)"
if [[ "$load" == "true" ]]; then
    pass "loadEffect -> $load"
else
    fail "loadEffect -> ${load:-<no reply>}"
    echo
    echo "        Usually one of:"
    echo "          * the effect id is not in KWin's plugin list — check with"
    echo "              qdbus6 org.kde.KWin /Effects listOfEffects | grep software_dim"
    echo "          * plugin IID mismatch — rebuild against the installed kwin headers"
    echo "          * supported() returned false (compositing is not OpenGL):"
    echo "              qdbus6 org.kde.KWin /KWin supportInformation | head -40"
    echo
    echo "        Recent KWin log:"
    journalctl -b -u plasma-kwin_wayland --since "5 minutes ago" --no-pager 2>/dev/null \
        | grep -iE 'software_dim|software dim|shader|effect' | tail -20 | sed 's/^/          /'
    exit 1
fi

# --------------------------------------------------------------------------
stage "Stage 3 — effect reports as loaded"

loaded="$(qdbus6 org.kde.KWin /Effects isEffectLoaded "$ID" 2>&1)"
[[ "$loaded" == "true" ]] && pass "isEffectLoaded -> $loaded" \
                          || die "isEffectLoaded -> ${loaded:-<no reply>}"

if [[ "$QUICK" == "--quick" ]]; then
    printf '\n%sStages 1-3 passed. Re-run without --quick for the visual stages.%s\n' "$GREEN" "$OFF"
    exit 0
fi

# --------------------------------------------------------------------------
stage "Stage 4 — Meta+Alt+D toggles the dimmer"

echo "  Press Meta+Alt+D once. The whole desktop should get visibly darker."
pause "Done?"
echo "  Press Meta+Alt+D again. Normal brightness should come straight back."
pause "Done?"
echo "  Meta+Alt+Up / Meta+Alt+Down should step the level (0.05 at a time)."
pause "Done?"

# --------------------------------------------------------------------------
stage "Stage 5 — what must be dimmed"

echo "  While the dimmer is ON, check each of these:"
for item in \
    "wallpaper is dimmed" \
    "KDE panels and the taskbar are dimmed" \
    "application windows are dimmed" \
    "notifications are dimmed" \
    "a fullscreen application (F11 in a browser) is dimmed" \
    "the mouse cursor still moves and clicks normally" \
    "no flicker, no black flashes, no corrupted frames" \
    "typing and pointer input are unaffected" \
; do
    read -r -p "    [y/N] $item? " ok
    case "$ok" in
        [yY]*) pass "$item" ;;
        *)     fail "$item"; problems=1 ;;
    esac
done

# --------------------------------------------------------------------------
stage "Stage 6 — disabling restores normal output"

echo "  Press Meta+Alt+D to switch the dimmer off."
pause "Desktop back to normal?"

echo "  Now unload the effect completely:"
unload="$(qdbus6 org.kde.KWin /Effects unloadEffect "$ID" 2>&1)"
[[ "$unload" == "true" ]] && pass "unloadEffect -> $unload" || warn "unloadEffect -> ${unload:-<no reply>}"

loaded="$(qdbus6 org.kde.KWin /Effects isEffectLoaded "$ID" 2>&1)"
[[ "$loaded" == "false" ]] && pass "isEffectLoaded -> $loaded" || warn "isEffectLoaded -> ${loaded:-<no reply>}"

# --------------------------------------------------------------------------
stage "Journal (should contain a handful of lines, never per-frame output)"

journalctl -b -u plasma-kwin_wayland --since "10 minutes ago" --no-pager 2>/dev/null \
    | grep -i 'software_dim' | tail -20 | sed 's/^/    /'

echo
if [[ "${problems:-0}" == "1" ]]; then
    printf '%sSome visual checks failed — see README.md -> Troubleshooting.%s\n' "$RED" "$OFF"
    exit 1
fi
printf '%sAll stages passed.%s\n' "$GREEN" "$OFF"
