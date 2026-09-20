#!/usr/bin/env fish
# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# verify-api.fish — check the *installed* KWin headers for every symbol this
# effect uses, before you spend time on a build that will fail.
#
# Why this exists: KWin's effect API changes between Plasma releases and the
# headers are the only authority. This script turns "does my code match my
# KWin?" into a table you can read in two seconds.
#
#   ./scripts/verify-api.fish
#   KWIN_INCLUDE_ROOT=/some/prefix ./scripts/verify-api.fish
#
# Header discovery is deliberately layout-agnostic: it searches for several
# possible header names (KWin renamed and moved these between releases) across
# several prefixes, and falls back to asking the package manager what the kwin
# package actually installed. If it cannot find them it prints the diagnostics
# needed to find out why.
#
# Exit status: 0 if everything was found, 1 otherwise.

set BOLD (printf '\033[1m')
set RED (printf '\033[31m')
set GREEN (printf '\033[32m')
set YELLOW (printf '\033[33m')
set OFF (printf '\033[0m')
set missing 0

function say
    printf '%s\n' (string join ' ' -- $argv)
end

function hdr
    printf '\n%s%s%s\n' "$BOLD" (string join ' ' -- $argv) "$OFF"
end

# ---------------------------------------------------------------- locate ---

hdr "1. KWin installation"

set kwin_bin (command -v kwin_wayland 2>/dev/null)
if test -n "$kwin_bin"
    say "kwin_wayland binary : $kwin_bin"
    set kwin_ver ("$kwin_bin" --version 2>/dev/null | awk '/^KWin/{print $NF}' | head -n 1)
    if test -n "$kwin_ver"
        say "reported version    : $kwin_ver"
    end
else
    say "$YELLOW""kwin_wayland not on PATH — skipping version probe.""$OFF"
end

# Where are KWin's own effect plugins? That is also where we install.
set builtin_dir
for d in /usr/lib/qt6/plugins/kwin/effects/lib \
         /usr/lib64/qt6/plugins/kwin/effects/lib \
         /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/lib
    set hit (find "$d" -maxdepth 1 -name '*kwin4_effect_*.so' -print -quit 2>/dev/null)
    if test -n "$hit"
        set builtin_dir "$d"
        break
    end
end
if test -z "$builtin_dir"
    set builtin_effect (find /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu \
        -type f \( -name 'libkwin4_effect_blur.so' -o -name 'kwin4_effect_blur.so' \) \
        -print -quit 2>/dev/null)
    if test -n "$builtin_effect"
        set builtin_dir (dirname "$builtin_effect")
    end
end
if test -n "$builtin_dir"
    say "built-in effects in : $builtin_dir"
else
    say "$YELLOW""No built-in effect .so found; cannot auto-detect the plugin dir.""$OFF"
    say "  Pass it explicitly:  cmake -B build -DKWIN_EFFECTS_INSTALL_DIR=/path"
end

# ---------------------------------------------------------------- headers --

hdr "2. Effect headers"

# KWin has moved these around between releases. Any of these is a usable anchor
# for finding the directory the rest live in.
set anchors kwineffects.h effecthandler.h kwinoffscreeneffect.h

set roots
if set -q KWIN_INCLUDE_ROOT; and test -n "$KWIN_INCLUDE_ROOT"
    set roots "$KWIN_INCLUDE_ROOT"
else
    set roots /usr/include /usr/local/include /usr/include/x86_64-linux-gnu "$HOME/kde/usr/include"
end

set anchor_path
for root in $roots
    test -d "$root"; or continue
    for a in $anchors
        # No -type f: some packagers install headers as symlinks.
        set hit (find "$root" -name "$a" -print -quit 2>/dev/null)
        if test -n "$hit"
            set anchor_path "$hit"
            break
        end
    end
    if test -n "$anchor_path"
        break
    end
end

# Fallback: ask the package manager what the kwin package actually installed.
# This is what catches a distro that ships the headers somewhere unexpected.
set pkg_hint
if test -z "$anchor_path"; and command -q pacman
    for pkg in kwin kwin-common kwin-dev
        set hit (pacman -Ql "$pkg" 2>/dev/null | awk '{print $2}' \
            | grep -E '/include/.*(kwineffects|effecthandler|kwinoffscreeneffect)\.h$' | head -n 1)
        if test -n "$hit"; and test -f "$hit"
            set anchor_path "$hit"
            set pkg_hint "pacman -Ql $pkg"
            break
        end
    end
end
if test -z "$anchor_path"; and command -q dpkg
    for pkg in kwin-dev kwin-wayland kwin-common
        set hit (dpkg -L "$pkg" 2>/dev/null \
            | grep -E '/include/.*(kwineffects|effecthandler|kwinoffscreeneffect)\.h$' | head -n 1)
        if test -n "$hit"; and test -f "$hit"
            set anchor_path "$hit"
            set pkg_hint "dpkg -L $pkg"
            break
        end
    end
end

# The CMake package config is the other thing we need, and finding it tells us
# the install prefix even when the header search comes up empty.
# The config lives at <prefix>/lib*/cmake/KWinEffects, so search install
# prefixes rather than include directories.
set cmake_roots /usr /usr/local
if set -q KWIN_INCLUDE_ROOT; and test -n "$KWIN_INCLUDE_ROOT"
    set -a cmake_roots (dirname "$KWIN_INCLUDE_ROOT")
end
set kwineffects_cmake (find $cmake_roots \
    \( -name 'KWinEffectsConfig.cmake' -o -name 'kwineffects-config.cmake' \) \
    -print -quit 2>/dev/null)

if test -z "$anchor_path"
    say "$RED""No KWin effect headers found.""$OFF"
    say
    say "Searched for: "(string join ' ' -- $anchors)
    say "In prefixes : "(string join ' ' -- $roots)
    if test -n "$kwineffects_cmake"
        say
        say "$GREEN""But the CMake config IS installed:""$OFF"
        say "    $kwineffects_cmake"
        say "  so the headers are somewhere on this system. Find them with:"
        say "    find /usr -name 'kwinoffscreeneffect.h' -o -name 'effecthandler.h' 2>/dev/null"
    else
        say "The KWinEffects CMake config was not found either."
    end
    if test -n "$pkg_hint"
        say "Also queried: $pkg_hint"
    end
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
    say "    KWIN_INCLUDE_ROOT=/that/prefix ./scripts/verify-api.fish"
    exit 1
end

set hdrdir (dirname "$anchor_path")
set incprefix (basename "$hdrdir")
set incroot (dirname "$hdrdir")

if test -n "$pkg_hint"
    set found_via "$pkg_hint"
else
    set found_via "filesystem search"
end
set searched_names (string join ' ' -- $anchors)
set anchor_name (basename "$anchor_path")
say "found via           : $found_via"
say "header directory    : $hdrdir"
say "include prefix      : #include \"$incprefix/<name>.h\""
say "searched names      : $searched_names  (anchor: $anchor_name)"
if test -n "$kwineffects_cmake"
    say "CMake config        : $kwineffects_cmake"
else
    say "$YELLOW""CMake config        : KWinEffectsConfig.cmake not found — cmake will fail at""$OFF"
    say "$YELLOW""                      find_package(KWinEffects) even though the headers are here.""$OFF"
end
say
say "Headers present in that directory:"
ls -1 "$hdrdir" 2>/dev/null | sed 's/^/    /'

# Which of the headers this effect includes actually exist here?
say
say "Headers this effect includes:"
for h in kwineffects.h effecthandler.h kwinoffscreeneffect.h rendertarget.h \
         renderviewport.h glframebuffer.h glrendertarget.h gltexture.h \
         glshader.h glshadermanager.h kwinglobals.h
    if test -f "$hdrdir/$h"
        printf '    %sok%s       %s\n' "$GREEN" "$OFF" "$h"
    else
        printf '    %sabsent%s   %s\n' "$YELLOW" "$OFF" "$h"
    end
end

# ---------------------------------------------------------------- symbols --

hdr "3. Symbol check"

function check
    set label "$argv[1]"
    set regex "$argv[2]"
    set file "$argv[3]"
    set path "$hdrdir/$file"
    if not test -f "$path"
        printf '  %s%-34s%s %sNO HEADER%s   %s is not in %s/\n' \
            "$BOLD" "$label" "$OFF" "$YELLOW" "$OFF" "$file" "$incprefix"
        set -g missing (math "$missing + 1")
        return
    end
    set hit (grep -nE "$regex" "$path" | head -n 1)
    if test -n "$hit"
        set line_number (string replace -r ':.*$' '' -- "$hit")
        printf '  %s%-34s%s %sok%s         %s:%s\n' \
            "$BOLD" "$label" "$OFF" "$GREEN" "$OFF" "$file" "$line_number"
    else
        printf '  %s%-34s%s %sMISSING%s     %s  (expected: %s)\n' \
            "$BOLD" "$label" "$OFF" "$RED" "$OFF" "$file" "$regex"
        set -g missing (math "$missing + 1")
    end
end

# The two names the "effects base class" header has had. Use whichever exists.
set effhdr kwineffects.h
if not test -f "$hdrdir/kwineffects.h"
    set effhdr effecthandler.h
end

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
check "pushShader(GLShader *)"       'pushShader *\('                                   'glshadermanager.h'
check "popShader"                    'popShader'                                        'glshadermanager.h'

# -R, not -r: -r does not follow symlinks encountered while recursing.
if grep -RqE 'define +KWIN_EFFECT_CLASS' "$hdrdir" 2>/dev/null
    set macro_header (grep -RlE 'define +KWIN_EFFECT_CLASS' "$hdrdir" | head -n 1)
    printf '  %s%-34s%s %sok%s         %s\n' "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$GREEN" "$OFF" \
        (basename "$macro_header")
else
    printf '  %s%-34s%s %sMISSING%s     main.cpp falls back to a factory KWin will NOT load\n' \
        "$BOLD" "KWIN_EFFECT_CLASS macro" "$OFF" "$RED" "$OFF"
    set -g missing (math "$missing + 1")
end

# ---------------------------------------------------------------- report ---

hdr "4. paintScreen signature actually installed"

if test -f "$hdrdir/$effhdr"
    set sig (grep -nEA5 'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' "$hdrdir/$effhdr" 2>/dev/null \
        | head -n 8 | string collect)
    if test -n "$sig"
        printf '%s\n' "$sig" | sed 's/^/    /'
        if grep -qE 'virtual +\[\[nodiscard\]\] +bool +paintScreen' "$hdrdir/$effhdr"
            printf '\n  %sYour KWin returns bool from paintScreen (KWin >= 6.7.90 / Plasma 6.8).%s\n' "$YELLOW" "$OFF"
            printf '  src/softwaredim.h and src/softwaredim.cpp declare it as void — change\n'
            printf '  both to `[[nodiscard]] bool` and `return true;` at the end.\n'
        else
            printf '\n  %sVoid return: matches this source tree as shipped (KWin 6.7.x).%s\n' "$GREEN" "$OFF"
        end
    else
        printf '  %scould not extract the signature — check the header by hand.%s\n' "$YELLOW" "$OFF"
    end
end

# The plugin IID is version stamped, so it is worth printing.
set iid (grep -RhoE 'org\.kde\.kwin\.EffectPluginFactory[0-9.]*' "$hdrdir" 2>/dev/null | head -n 1)
if test -n "$iid"
    say
    say "Plugin IID your KWin expects: $BOLD""$iid""$OFF"
    say "  (KWIN_EFFECT_CLASS stamps this; a hand-rolled KF6 factory will not load.)"
end

hdr "Result"

if test "$missing" -eq 0
    printf '%sAll required symbols are present. Safe to build.%s\n' "$GREEN" "$OFF"
    exit 0
else
    printf '%s%d item(s) not found.%s See README.md -> Troubleshooting for the exact\n' \
        "$RED" "$missing" "$OFF"
    printf 'line to change, and paste this output if anything looks unexpected.\n'
    exit 1
end
