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
#
# Exit status: 0 if everything was found, 1 if anything is missing.

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
        -type f -name 'libkwin4_effect_blur.so' -o -type f -name 'kwin4_effect_blur.so' 2>/dev/null \
        | head -1 | xargs -r dirname || true)"
fi
if [[ -n "$builtin_dir" ]]; then
    say "built-in effects in : $builtin_dir"
else
    say "${YELLOW}No built-in effect .so found; cannot auto-detect the install dir.${OFF}"
    say "  Pass it explicitly:  cmake -B build -DKWIN_EFFECTS_INSTALL_DIR=/path"
fi

# ---------------------------------------------------------------- headers --

hdr "2. Effect headers"

candidates=()
if [[ -n "${KWIN_INCLUDE_ROOT:-}" ]]; then
    # Escape hatch for non-standard prefixes (kdesrc-build, a staged sysroot)
    # and the reason this script is testable at all.
    while IFS= read -r f; do candidates+=("$f"); done < <(
        find "$KWIN_INCLUDE_ROOT" -type f -name kwineffects.h 2>/dev/null
    )
else
    while IFS= read -r f; do candidates+=("$f"); done < <(
        find /usr/include /usr/local/include -type f -name kwineffects.h 2>/dev/null
    )
fi

if [[ ${#candidates[@]} -eq 0 ]]; then
    say "${RED}kwineffects.h not found under ${KWIN_INCLUDE_ROOT:-/usr/include}.${OFF}"
    say "Install the KWin development headers:"
    say "  Arch / CachyOS : sudo pacman -S kwin"
    say "  Debian / Ubuntu: sudo apt install kwin-dev"
    say "  Fedora         : sudo dnf install kwin-devel"
    exit 1
fi

incroot="$(dirname "$(dirname "${candidates[0]}")")"
say "include root        : $incroot"
say "kwineffects.h       : ${candidates[0]}"
say "search path         : $incroot (headers are included as libkwineffects/<name>.h)"

# ---------------------------------------------------------------- symbols --

hdr "3. Symbol check"

# symbol|regex|where-to-look|why-it-matters
check() {
    local label="$1" regex="$2" file="$3"
    local path="$incroot/$file"
    if [[ ! -f "$path" ]]; then
        printf '  %s%-34s%s %sMISSING FILE%s  %s\n' "$BOLD" "$label" "$OFF" "$RED" "$OFF" "$file"
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

check "Effect base class"            'class +[A-Z_]* *Effect'                          'libkwineffects/kwineffects.h'
# NOTE: KWin 6.7 declares `virtual void paintScreen(...)`; from 6.7.90 (Plasma
# 6.8) it is `virtual [[nodiscard]] bool paintScreen(...)`. The regex must match
# both, otherwise the checker reports MISSING on the very version it warns about.
check "paintScreen hook"             'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' 'libkwineffects/kwineffects.h'
check "blocksDirectScanout"          'blocksDirectScanout'                              'libkwineffects/kwineffects.h'
check "requestedEffectChainPosition" 'requestedEffectChainPosition'                     'libkwineffects/kwineffects.h'
check "reconfigure(ReconfigureFlags)" 'reconfigure *\( *ReconfigureFlags'               'libkwineffects/kwineffects.h'
check "OpenGLCompositing"            'OpenGLCompositing'                                'libkwineffects/kwineffects.h'
check "addRepaintFull"               'addRepaintFull'                                   'libkwineffects/kwineffects.h'
check "RenderTarget"                 'class +[A-Z_]* *RenderTarget'                     'libkwineffects/rendertarget.h'
check "RenderViewport"               'class +[A-Z_]* *RenderViewport'                   'libkwineffects/renderviewport.h'
check "GLFramebuffer::create"        'create *\( *GLTexture'                            'libkwineffects/glframebuffer.h'
check "GLFramebuffer::pushFramebuffer" 'pushFramebuffer'                                'libkwineffects/glframebuffer.h'
check "GLFramebuffer::popFramebuffer"  'popFramebuffer'                                 'libkwineffects/glframebuffer.h'
check "GLFramebuffer::valid"         'bool +valid'                                      'libkwineffects/glframebuffer.h'
check "GLTexture::allocate"          'allocate *\('                                     'libkwineffects/gltexture.h'
check "GLTexture::setFilter"         'setFilter'                                        'libkwineffects/gltexture.h'
check "GLTexture::render"            'void +render *\('                                 'libkwineffects/gltexture.h'
check "GLShader::setUniform(name,float)" 'setUniform *\( *const +char'                  'libkwineffects/glshader.h'
check "Mat4Uniform enum"             'Mat4Uniform'                                      'libkwineffects/glshader.h'
check "ModelViewProjectionMatrix"    'ModelViewProjectionMatrix'                        'libkwineffects/glshader.h'
check "ShaderTrait::MapTexture"      'MapTexture'                                       'libkwineffects/glshadermanager.h'
check "generateShaderFromFile"       'generateShaderFromFile'                           'libkwineffects/glshadermanager.h'
check "pushShader(GLShader *)"      'pushShader *\('                                    'libkwineffects/glshadermanager.h'
check "popShader"                    'popShader'                                        'libkwineffects/glshadermanager.h'

# KWIN_EFFECT_CLASS may live in any header under the include root.
if grep -rqE 'define +KWIN_EFFECT_CLASS' "$incroot" 2>/dev/null; then
    printf '  %s%-34s%s %sok%s         %s\n' "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$GREEN" "$OFF" \
        "$(grep -rlE 'define +KWIN_EFFECT_CLASS' "$incroot" | head -1 | sed "s|$incroot/||")"
else
    printf '  %s%-34s%s %sMISSING%s     main.cpp falls back to K_PLUGIN_FACTORY_WITH_JSON\n' \
        "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$YELLOW" "$OFF"
fi

# ---------------------------------------------------------------- report ---

hdr "4. paintScreen signature actually installed"

sig="$(grep -nEA5 'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' "$incroot/libkwineffects/kwineffects.h" 2>/dev/null | head -8)"
if [[ -n "$sig" ]]; then
    printf '%s\n' "$sig" | sed 's/^/    /'
    if grep -qE 'virtual +\[\[nodiscard\]\] +bool +paintScreen' "$incroot/libkwineffects/kwineffects.h"; then
        printf '\n  %sYour KWin returns bool from paintScreen (KWin >= 6.7.90 / Plasma 6.8).%s\n' "$YELLOW" "$OFF"
        printf '  src/softwaredim.h and src/softwaredim.cpp declare it as void — change\n'
        printf '  both to `[[nodiscard]] bool` and `return true;` at the end.\n'
    else
        printf '\n  %sVoid return: matches this source tree as shipped (KWin 6.7.x).%s\n' "$GREEN" "$OFF"
    fi
else
    printf '  %scould not extract the signature — check the header by hand.%s\n' "$YELLOW" "$OFF"
fi

hdr "Result"

if [[ $missing -eq 0 ]]; then
    printf '%sAll required symbols are present. Safe to build.%s\n' "$GREEN" "$OFF"
    exit 0
else
    printf '%s%d symbol(s) not found.%s Fix them before building — see README.md →\n' \
        "$RED" "$missing" "$OFF"
    printf 'Troubleshooting for the exact line to change.\n'
    exit 1
fi
