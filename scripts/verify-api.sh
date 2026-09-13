#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# verify-api.sh — check the *installed* KWin headers for every symbol this
# effect uses, before you spend time on a build that will fail.
#
# Why this exists: KWin's effect API changes between Plasma releases and the
# headers are the only authority. This script turns "does my code match my
# KWin?" into a table you can read in two seconds.
#
#   ./scripts/verify-api.sh
#   KWIN_INCLUDE_ROOT=/some/prefix ./scripts/verify-api.sh
#
# Header discovery is deliberately layout-agnostic: it searches for several
# possible header names (KWin renamed and moved these between releases) across
# several prefixes, and falls back to asking the package manager what the kwin
# package actually installed. If it cannot find them it prints the diagnostics
# needed to find out why.
#
# Exit status: 0 if everything was found, 1 otherwise.

set -uo pipefail

BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; OFF=$'\033[0m'
missing=0

say()  { printf '%s\n' "$*"; }
hdr()  { printf '\n%s%s%s\n' "$BOLD" "$*" "$OFF"; }

# ---------------------------------------------------------------- locate ---

hdr "1. KWin installation"

kwin_bin="$(command -v kwin_wayland 2>/dev/null || true)"
if [[ -n "$kwin_bin" ]]; then
    say "kwin_wayland binary : $kwin_bin"
    kwin_ver="$("$kwin_bin" --version 2>/dev/null | awk '/^KWin/{print $NF}' || true)"
    [[ -n "$kwin_ver" ]] && say "reported version    : $kwin_ver"
else
    say "${YELLOW}kwin_wayland not on PATH — skipping version probe.${OFF}"
fi

# Where are KWin's own effect plugins? That is also where we install.
builtin_dir=""
for d in /usr/lib/qt6/plugins/kwin/effects/lib \
         /usr/lib64/qt6/plugins/kwin/effects/lib \
         /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/lib; do
    if compgen -G "$d/*kwin4_effect_*.so" >/dev/null 2>&1; then
        builtin_dir="$d"; break
    fi
done
if [[ -z "$builtin_dir" ]]; then
    builtin_dir="$(find /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu \
        -type f \( -name 'libkwin4_effect_blur.so' -o -name 'kwin4_effect_blur.so' \) 2>/dev/null \
        | head -1 | xargs -r dirname || true)"
fi
if [[ -n "$builtin_dir" ]]; then
    say "built-in effects in : $builtin_dir"
else
    say "${YELLOW}No built-in effect .so found; cannot auto-detect the plugin dir.${OFF}"
    say "  Pass it explicitly:  cmake -B build -DKWIN_EFFECTS_INSTALL_DIR=/path"
fi

# ---------------------------------------------------------------- headers --

hdr "2. Effect headers"

# KWin has moved these around between releases. Any of these is a usable anchor
# for finding the directory the rest live in.
anchors=(kwineffects.h effecthandler.h kwinoffscreeneffect.h)

roots=()
if [[ -n "${KWIN_INCLUDE_ROOT:-}" ]]; then
    roots+=("$KWIN_INCLUDE_ROOT")
else
    roots+=(/usr/include /usr/local/include /usr/include/x86_64-linux-gnu "$HOME/kde/usr/include")
fi

anchor_path=""
for root in "${roots[@]}"; do
    [[ -d "$root" ]] || continue
    for a in "${anchors[@]}"; do
        # No -type f: some packagers install headers as symlinks.
        hit="$(find "$root" -name "$a" 2>/dev/null | head -1)"
        if [[ -n "$hit" ]]; then anchor_path="$hit"; break 2; fi
    done
done

# Fallback: ask the package manager what the kwin package actually installed.
# This is what catches a distro that ships the headers somewhere unexpected.
pkg_hint=""
if [[ -z "$anchor_path" ]] && command -v pacman >/dev/null 2>&1; then
    for pkg in kwin kwin-common kwin-dev; do
        hit="$(pacman -Ql "$pkg" 2>/dev/null | awk '{print $2}' \
               | grep -E '/include/.*(kwineffects|effecthandler|kwinoffscreeneffect)\.h$' | head -1)"
        if [[ -n "$hit" && -f "$hit" ]]; then anchor_path="$hit"; pkg_hint="pacman -Ql $pkg"; break; fi
    done
fi
if [[ -z "$anchor_path" ]] && command -v dpkg >/dev/null 2>&1; then
    for pkg in kwin-dev kwin-wayland kwin-common; do
        hit="$(dpkg -L "$pkg" 2>/dev/null \
               | grep -E '/include/.*(kwineffects|effecthandler|kwinoffscreeneffect)\.h$' | head -1)"
        if [[ -n "$hit" && -f "$hit" ]]; then anchor_path="$hit"; pkg_hint="dpkg -L $pkg"; break; fi
    done
fi

# The CMake package config is the other thing we need, and finding it tells us
# the install prefix even when the header search comes up empty.
# The config lives at <prefix>/lib*/cmake/KWinEffects, so search install
# prefixes rather than include directories.
cmake_roots=(/usr /usr/local)
if [[ -n "${KWIN_INCLUDE_ROOT:-}" ]]; then
    cmake_roots+=("$(dirname "$KWIN_INCLUDE_ROOT")")
fi
kwineffects_cmake="$(find "${cmake_roots[@]}" \
    \( -name 'KWinEffectsConfig.cmake' -o -name 'kwineffects-config.cmake' \) \
    2>/dev/null | head -1)"

if [[ -z "$anchor_path" ]]; then
    say "${RED}No KWin effect headers found.${OFF}"
    say
    say "Searched for: ${anchors[*]}"
    say "In prefixes : ${roots[*]}"
    if [[ -n "$kwineffects_cmake" ]]; then
        say
        say "${GREEN}But the CMake config IS installed:${OFF}"
        say "    $kwineffects_cmake"
        say "  so the headers are somewhere on this system. Find them with:"
        say "    find /usr -name 'kwinoffscreeneffect.h' -o -name 'effecthandler.h' 2>/dev/null"
    else
        say "The KWinEffects CMake config was not found either."
    fi
    [[ -n "$pkg_hint" ]] && say "Also queried: $pkg_hint"
    say
    say "Run these and paste the output — they show where (or whether) your"
    say "distribution ships them:"
    say
    say "    pacman -Ql kwin | grep -iE '/include/' | head -40        # Arch"
    say "    dpkg -L kwin-dev | grep -iE '/include/' | head -40       # Debian"
    say "    rpm -ql kwin-devel | grep -iE '/include/' | head -40     # Fedora"
    say "    find / -name 'kwinoffscreeneffect.h' 2>/dev/null"
    say
    say "If the last one finds them somewhere unusual, re-run as:"
    say "    KWIN_INCLUDE_ROOT=/that/prefix ./scripts/verify-api.sh"
    exit 1
fi

hdrdir="$(dirname "$anchor_path")"
incprefix="$(basename "$hdrdir")"
incroot="$(dirname "$hdrdir")"

say "found via           : ${pkg_hint:-filesystem search}"
say "header directory    : $hdrdir"
say "include prefix      : #include \"$incprefix/<name>.h\""
say "searched names      : ${anchors[*]}  (anchor: $(basename "$anchor_path"))"
if [[ -n "$kwineffects_cmake" ]]; then
    say "CMake config        : $kwineffects_cmake"
else
    say "${YELLOW}CMake config        : KWinEffectsConfig.cmake not found — cmake will fail at${OFF}"
    say "${YELLOW}                      find_package(KWinEffects) even though the headers are here.${OFF}"
fi
say
say "Headers present in that directory:"
ls -1 "$hdrdir" 2>/dev/null | sed 's/^/    /'

# Which of the headers this effect includes actually exist here?
say
say "Headers this effect includes:"
for h in kwineffects.h effecthandler.h kwinoffscreeneffect.h rendertarget.h \
         renderviewport.h glframebuffer.h glrendertarget.h gltexture.h \
         glshader.h glshadermanager.h kwinglobals.h; do
    if [[ -f "$hdrdir/$h" ]]; then
        printf '    %sok%s       %s\n' "$GREEN" "$OFF" "$h"
    else
        printf '    %sabsent%s   %s\n' "$YELLOW" "$OFF" "$h"
    fi
done

# ---------------------------------------------------------------- symbols --

hdr "3. Symbol check"

check() {
    local label="$1" regex="$2" file="$3"
    local path="$hdrdir/$file"
    if [[ ! -f "$path" ]]; then
        printf '  %s%-34s%s %sNO HEADER%s   %s is not in %s/\n' \
            "$BOLD" "$label" "$OFF" "$YELLOW" "$OFF" "$file" "$incprefix"
        missing=$((missing + 1))
        return
    fi
    local hit
    hit="$(grep -nE "$regex" "$path" | head -1 || true)"
    if [[ -n "$hit" ]]; then
        printf '  %s%-34s%s %sok%s         %s:%s\n' \
            "$BOLD" "$label" "$OFF" "$GREEN" "$OFF" "$file" "${hit%%:*}"
    else
        printf '  %s%-34s%s %sMISSING%s     %s  (expected: %s)\n' \
            "$BOLD" "$label" "$OFF" "$RED" "$OFF" "$file" "$regex"
        missing=$((missing + 1))
    fi
}

# The two names the "effects base class" header has had. Use whichever exists.
effhdr="kwineffects.h"
[[ -f "$hdrdir/kwineffects.h" ]] || effhdr="effecthandler.h"

check "Effect base class"            'class +[A-Z_]* *Effect'                          "$effhdr"
# NOTE: KWin 6.7 declares `virtual void paintScreen(...)`; from 6.7.90 (Plasma
# 6.8) it is `virtual [[nodiscard]] bool paintScreen(...)`. The regex must match
# both, otherwise the checker reports MISSING on the very version it warns about.
check "paintScreen hook"             'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' "$effhdr"
check "blocksDirectScanout"          'blocksDirectScanout'                              "$effhdr"
check "requestedEffectChainPosition" 'requestedEffectChainPosition'                     "$effhdr"
check "reconfigure(ReconfigureFlags)" 'reconfigure *\( *ReconfigureFlags'               "$effhdr"
check "OpenGLCompositing"            'OpenGLCompositing'                                "$effhdr"
check "addRepaintFull"               'addRepaintFull'                                   "$effhdr"
check "RenderTarget"                 'class +[A-Z_]* *RenderTarget'                     'rendertarget.h'
check "RenderViewport"               'class +[A-Z_]* *RenderViewport'                   'renderviewport.h'
check "GLFramebuffer::create"        'create *\( *GLTexture'                            'glframebuffer.h'
check "GLFramebuffer::pushFramebuffer" 'pushFramebuffer'                                'glframebuffer.h'
check "GLFramebuffer::popFramebuffer"  'popFramebuffer'                                 'glframebuffer.h'
check "GLFramebuffer::valid"         'bool +valid'                                      'glframebuffer.h'
check "GLTexture::allocate"          'allocate *\('                                     'gltexture.h'
check "GLTexture::setFilter"         'setFilter'                                        'gltexture.h'
check "GLTexture::render"            'void +render *\('                                 'gltexture.h'
check "GLShader::setUniform(name,float)" 'setUniform *\( *const +char'                  'glshader.h'
check "Mat4Uniform enum"             'Mat4Uniform'                                      'glshader.h'
check "ModelViewProjectionMatrix"    'ModelViewProjectionMatrix'                        'glshader.h'
check "ShaderTrait::MapTexture"      'MapTexture'                                       'glshadermanager.h'
check "generateShaderFromFile"       'generateShaderFromFile'                           'glshadermanager.h'
check "pushShader(GLShader *)"      'pushShader *\('                                    'glshadermanager.h'
check "popShader"                    'popShader'                                        'glshadermanager.h'

# -R, not -r: -r does not follow symlinks encountered while recursing.
if grep -RqE 'define +KWIN_EFFECT_CLASS' "$hdrdir" 2>/dev/null; then
    printf '  %s%-34s%s %sok%s         %s\n' "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$GREEN" "$OFF" \
        "$(grep -RlE 'define +KWIN_EFFECT_CLASS' "$hdrdir" | head -1 | xargs -r basename)"
else
    printf '  %s%-34s%s %sMISSING%s     main.cpp falls back to a factory KWin will NOT load\n' \
        "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$RED" "$OFF"
    missing=$((missing + 1))
fi

# ---------------------------------------------------------------- report ---

hdr "4. paintScreen signature actually installed"

if [[ -f "$hdrdir/$effhdr" ]]; then
    sig="$(grep -nEA5 'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' "$hdrdir/$effhdr" 2>/dev/null | head -8)"
    if [[ -n "$sig" ]]; then
        printf '%s\n' "$sig" | sed 's/^/    /'
        if grep -qE 'virtual +\[\[nodiscard\]\] +bool +paintScreen' "$hdrdir/$effhdr"; then
            printf '\n  %sYour KWin returns bool from paintScreen (KWin >= 6.7.90 / Plasma 6.8).%s\n' "$YELLOW" "$OFF"
            printf '  src/softwaredim.h and src/softwaredim.cpp declare it as void — change\n'
            printf '  both to `[[nodiscard]] bool` and `return true;` at the end.\n'
        else
            printf '\n  %sVoid return: matches this source tree as shipped (KWin 6.7.x).%s\n' "$GREEN" "$OFF"
        fi
    else
        printf '  %scould not extract the signature — check the header by hand.%s\n' "$YELLOW" "$OFF"
    fi
fi

# The plugin IID is version stamped, so it is worth printing.
iid="$(grep -RhoE 'org\.kde\.kwin\.EffectPluginFactory[0-9.]*' "$hdrdir" 2>/dev/null | head -1)"
if [[ -n "$iid" ]]; then
    say
    say "Plugin IID your KWin expects: ${BOLD}$iid${OFF}"
    say "  (KWIN_EFFECT_CLASS stamps this; a hand-rolled KF6 factory will not load.)"
fi

hdr "Result"

if [[ $missing -eq 0 ]]; then
    printf '%sAll required symbols are present. Safe to build.%s\n' "$GREEN" "$OFF"
    exit 0
else
    printf '%s%d item(s) not found.%s See README.md -> Troubleshooting for the exact\n' \
        "$RED" "$missing" "$OFF"
    printf 'line to change, and paste this output if anything looks unexpected.\n'
    exit 1
fi
