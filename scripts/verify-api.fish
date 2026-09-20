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
# Expected layout (KWin 6.7, installed by the `kwin` / `kwin-dev` / `kwin-devel`
# package):
#
#   <prefix>/include/kwin/config-kwin.h          # version stamps
#   <prefix>/include/kwin/effect/effect.h        # Effect, KWIN_EFFECT_FACTORY_*
#   <prefix>/include/kwin/core/{rendertarget,renderviewport,region,output}.h
#   <prefix>/include/kwin/opengl/{glframebuffer,gltexture,glshader,glshadermanager}.h
#   <prefix>/lib*/cmake/KWin/KWinConfig.cmake    # exports the KWin::kwin target
#
# Header discovery is deliberately layout-agnostic: it probes that relative
# layout across several prefixes, and falls back to asking the package manager
# what the kwin package actually installed. If it cannot find them it prints
# the diagnostics needed to find out why.
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

# Where does KWin load native effect plugins from? Ask Qt itself — the answer
# is <qt plugin dir>/kwin/effects/plugins (see PluginEffectLoader in
# src/effect/effectloader.cpp). The built-in effects are compiled statically
# into kwin_wayland since Plasma 6, so an empty directory is normal.
set plugindir ""
set plugintool ""
if command -q qtpaths6
    set plugindir (qtpaths6 --query QT_INSTALL_PLUGINS 2>/dev/null)
    set plugintool qtpaths6
else if command -q qmake6
    set plugindir (qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null)
    set plugintool qmake6
end
if test -n "$plugindir"
    say "Qt plugin dir       : $plugindir (via $plugintool)"
    say "effect plugin dir   : $plugindir/kwin/effects/plugins"
    if test -d "$plugindir/kwin/effects/plugins"
        set nso (find "$plugindir/kwin/effects/plugins" -maxdepth 1 -name '*.so' 2>/dev/null | wc -l | string trim)
        say "  directory exists, $nso third-party .so file(s) in it"
    else
        say "$YELLOW""  directory does not exist yet — installing this effect will create it.""$OFF"
    end
else
    say "$YELLOW""Neither qtpaths6 nor qmake6 on PATH — cannot query the Qt plugin dir.""$OFF"
    for d in /usr/lib/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins
        if test -d "$d/kwin/effects/plugins"
            say "effect plugin dir   : $d/kwin/effects/plugins (guessed from fixed paths)"
            break
        end
    end
    say "  Pass the install dir explicitly if needed:"
    say "    cmake -B build -DKWIN_EFFECTS_INSTALL_DIR=<qt plugindir>/kwin/effects/plugins"
end

# ---------------------------------------------------------------- headers --

hdr "2. Effect headers"

# The anchor is the relative installed path, not a bare filename: an anchor of
# just "effect.h" would also match unrelated packages.
set anchor_rel "kwin/effect/effect.h"

set roots
if set -q KWIN_INCLUDE_ROOT; and test -n "$KWIN_INCLUDE_ROOT"
    set roots "$KWIN_INCLUDE_ROOT"
else
    set roots /usr/include /usr/local/include /usr/include/x86_64-linux-gnu "$HOME/kde/usr/include"
end

set anchor_path
for root in $roots
    test -d "$root"; or continue
    if test -f "$root/$anchor_rel"
        set anchor_path "$root/$anchor_rel"
        break
    end
    # Some packagers put the tree somewhere unusual; search for the anchor
    # anywhere below the prefix before giving up on it.
    # No -type f: some packagers install headers as symlinks.
    set hit (find "$root" -path "*$anchor_rel" 2>/dev/null | head -n 1)
    if test -n "$hit"
        set anchor_path "$hit"
        break
    end
end

# Fallback: ask the package manager what the kwin package actually installed.
# This is what catches a distro that ships the headers somewhere unexpected.
set pkg_hint
if test -z "$anchor_path"; and command -q pacman
    for pkg in kwin kwin-common kwin-dev
        set hit (pacman -Ql "$pkg" 2>/dev/null | awk '{print $2}' \
            | grep -E '/kwin/effect/effect\.h$' | head -n 1)
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
            | grep -E '/kwin/effect/effect\.h$' | head -n 1)
        if test -n "$hit"; and test -f "$hit"
            set anchor_path "$hit"
            set pkg_hint "dpkg -L $pkg"
            break
        end
    end
end
if test -z "$anchor_path"; and command -q rpm
    for pkg in kwin-devel kwin
        set hit (rpm -ql "$pkg" 2>/dev/null | grep -E '/kwin/effect/effect\.h$' | head -n 1)
        if test -n "$hit"; and test -f "$hit"
            set anchor_path "$hit"
            set pkg_hint "rpm -ql $pkg"
            break
        end
    end
end

# The CMake package config is the other thing we need, and finding it tells us
# the install prefix even when the header search comes up empty.
# The config lives at <prefix>/lib*/cmake/KWin, so search install prefixes
# rather than include directories.
set cmake_roots /usr /usr/local
if set -q KWIN_INCLUDE_ROOT; and test -n "$KWIN_INCLUDE_ROOT"
    set -a cmake_roots (dirname "$KWIN_INCLUDE_ROOT")
end
set kwin_cmake (find $cmake_roots \
    -path '*cmake/KWin/KWinConfig.cmake' \
    -print -quit 2>/dev/null)

if test -z "$anchor_path"
    say "$RED""No KWin effect headers found.""$OFF"
    say
    say "Searched for: $anchor_rel"
    say "In prefixes : "(string join ' ' -- $roots)
    if test -n "$kwin_cmake"
        say
        say "$GREEN""But the CMake config IS installed:""$OFF"
        say "    $kwin_cmake"
        say "  so the headers are somewhere on this system. Find them with:"
        say "    find /usr -path '*kwin/effect/effect.h' 2>/dev/null"
    else
        say "The KWin CMake config was not found either."
    end
    if test -n "$pkg_hint"
        say "Also queried: $pkg_hint"
    end
    say
    say "Run these and paste the output — they show where (or whether) your"
    say "distribution ships them:"
    say
    say "    pacman -Ql kwin | grep -E '/kwin/effect/' | head -40        # Arch"
    say "    dpkg -L kwin-dev | grep -E '/kwin/effect/' | head -40       # Debian"
    say "    rpm -ql kwin-devel | grep -E '/kwin/effect/' | head -40     # Fedora"
    say "    find / -path '*kwin/effect/effect.h' 2>/dev/null"
    say
    say "If the last one finds them somewhere unusual, re-run as:"
    say "    KWIN_INCLUDE_ROOT=/that/prefix ./scripts/verify-api.fish"
    exit 1
end

# anchor: <incroot>/kwin/effect/effect.h  ->  kwindir = <incroot>/kwin
set kwindir (dirname (dirname "$anchor_path"))

if test -n "$pkg_hint"
    set found_via "$pkg_hint"
else
    set found_via "filesystem search"
end
say "found via           : $found_via"
say "KWin include root   : $kwindir"
say "include style       : #include <effect/effect.h>  (kwin/ is the -I path of KWin::kwin)"
say "anchor              : $anchor_rel"
if test -n "$kwin_cmake"
    say "CMake config        : $kwin_cmake"
else
    say "$YELLOW""CMake config        : KWinConfig.cmake not found — cmake will fail at""$OFF"
    say "$YELLOW""                      find_package(KWin) even though the headers are here.""$OFF"
end

# The version stamps live in the installed config-kwin.h.
set plugver ""
if test -f "$kwindir/config-kwin.h"
    set plugver (grep -oE 'define KWIN_PLUGIN_VERSION_STRING "[0-9.]+"' "$kwindir/config-kwin.h" 2>/dev/null | grep -oE '[0-9.]+' | head -n 1)
    if test -n "$plugver"
        say "header version      : $plugver (KWIN_PLUGIN_VERSION_STRING)"
    end
else
    say "$YELLOW""config-kwin.h        : not beside the headers — version stamp unavailable.""$OFF"
end
say
say "Headers present:"
for sub in effect core opengl
    if test -d "$kwindir/$sub"
        set n (ls -1 "$kwindir/$sub" 2>/dev/null | wc -l | string trim)
        say "  $sub/ ($n files)"
    else
        say "  ""$YELLOW""$sub/ MISSING""$OFF"
    end
end

# Which of the headers this effect includes actually exist here?
say
say "Headers this effect includes:"
for h in effect/effect.h effect/effecthandler.h effect/globals.h \
         core/rendertarget.h core/renderviewport.h core/region.h core/output.h \
         core/colorspace.h \
         opengl/glframebuffer.h opengl/gltexture.h \
         opengl/glshader.h opengl/glshadermanager.h
    if test -f "$kwindir/$h"
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
    set path "$kwindir/$file"
    if not test -f "$path"
        printf '  %s%-34s%s %sNO HEADER%s   %s is not installed\n' \
            "$BOLD" "$label" "$OFF" "$YELLOW" "$OFF" "$file"
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

check "Effect base class"            'class +[A-Z_]* *Effect'                          'effect/effect.h'
# NOTE: KWin 6.7 declares `virtual void paintScreen(...)`; from 6.7.90 (Plasma
# 6.8) it is `virtual [[nodiscard]] bool paintScreen(...)`. The regex must match
# both, otherwise the checker reports MISSING on the very version it warns about.
check "paintScreen hook"             'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' 'effect/effect.h'
check "blocksDirectScanout"          'blocksDirectScanout'                              'effect/effect.h'
check "requestedEffectChainPosition" 'requestedEffectChainPosition'                     'effect/effect.h'
check "reconfigure(ReconfigureFlags)" 'reconfigure *\( *ReconfigureFlags'               'effect/effect.h'
check "EffectPluginFactory_iid"      'EffectPluginFactory_iid'                          'effect/effect.h'
check "OpenGLCompositing"            'OpenGLCompositing'                                'effect/globals.h'
check "effects global"               'EffectsHandler *\* *effects'                      'effect/effecthandler.h'
check "addRepaintFull"               'addRepaintFull'                                   'effect/effecthandler.h'
check "compositingType()"            'compositingType *\('                              'effect/effecthandler.h'
check "RenderTarget"                 'class +[A-Z_]* *RenderTarget'                     'core/rendertarget.h'
check "RenderViewport"               'class +[A-Z_]* *RenderViewport'                   'core/renderviewport.h'
check "RenderViewport 4-arg ctor"    'RenderViewport *\( *const +RectF'                 'core/renderviewport.h'
check "Region"                       'class +[A-Z_]* *Region'                           'core/region.h'
check "LogicalOutput"                'class +[A-Z_]* *LogicalOutput'                    'core/output.h'
check "GLFramebuffer(GLTexture *)"   'GLFramebuffer *\( *GLTexture'                     'opengl/glframebuffer.h'
check "GLFramebuffer::pushFramebuffer" 'pushFramebuffer'                                'opengl/glframebuffer.h'
check "GLFramebuffer::popFramebuffer"  'popFramebuffer'                                 'opengl/glframebuffer.h'
check "GLFramebuffer::valid"         'bool +valid'                                      'opengl/glframebuffer.h'
check "GLTexture::allocate"          'allocate *\('                                     'opengl/gltexture.h'
check "GLTexture::setFilter"         'setFilter'                                        'opengl/gltexture.h'
check "GLTexture::render"            'void +render *\('                                 'opengl/gltexture.h'
check "GLShader::setUniform(name,float)" 'setUniform *\( *const +char'                  'opengl/glshader.h'
check "Mat4Uniform enum"             'Mat4Uniform'                                      'opengl/glshader.h'
check "ModelViewProjectionMatrix"    'ModelViewProjectionMatrix'                        'opengl/glshader.h'
check "ShaderTrait::MapTexture"      'MapTexture'                                       'opengl/glshadermanager.h'
check "generateShaderFromFile"       'generateShaderFromFile'                           'opengl/glshadermanager.h'
check "pushShader(GLShader *)"       'pushShader *\('                                   'opengl/glshadermanager.h'
check "popShader"                    'popShader'                                        'opengl/glshadermanager.h'

# The paintScreen region/output parameters changed type across KWin 6
# (QRegion/Output -> Region/LogicalOutput). Grep the declaration with context
# so a wrapped signature still matches.
set sig (grep -nA3 -E 'paintScreen *\( *const RenderTarget' "$kwindir/effect/effect.h" 2>/dev/null | head -n 8 | string collect)
if test -n "$sig"; and printf '%s' "$sig" | grep -q 'Region'; and printf '%s' "$sig" | grep -q 'LogicalOutput'
    printf '  %s%-34s%s %sok%s         effect/effect.h (Region + LogicalOutput)\n' \
        "$BOLD" "paintScreen Region/Output types" "$OFF" "$GREEN" "$OFF"
else
    printf '  %s%-34s%s %sMISSING%s     effect/effect.h  (expected Region + LogicalOutput params)\n' \
        "$BOLD" "paintScreen Region/Output types" "$OFF" "$RED" "$OFF"
    set -g missing (math "$missing + 1")
end

# -R, not -r: -r does not follow symlinks encountered while recursing.
if grep -RqE 'define +KWIN_EFFECT_FACTORY' "$kwindir/effect" 2>/dev/null
    set macro_header (grep -RlE 'define +KWIN_EFFECT_FACTORY' "$kwindir/effect" | head -n 1)
    printf '  %s%-34s%s %sok%s         %s\n' "$BOLD" "KWIN_EFFECT_FACTORY macro" "$OFF" "$GREEN" "$OFF" \
        (basename "$macro_header")
else
    printf '  %s%-34s%s %sMISSING%s     main.cpp falls back to a factory KWin will NOT load\n' \
        "$BOLD" "KWIN_EFFECT_FACTORY macro" "$OFF" "$RED" "$OFF"
    set -g missing (math "$missing + 1")
end

# ---------------------------------------------------------------- report ---

hdr "4. paintScreen signature actually installed"

if test -f "$kwindir/effect/effect.h"
    set sig (grep -nEA5 'virtual +(\[\[nodiscard\]\] +)?(void|bool) +paintScreen' "$kwindir/effect/effect.h" 2>/dev/null \
        | head -n 8 | string collect)
    if test -n "$sig"
        printf '%s\n' "$sig" | sed 's/^/    /'
        if grep -qE 'virtual +\[\[nodiscard\]\] +bool +paintScreen' "$kwindir/effect/effect.h"
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
set iid_base (grep -RhoE 'org\.kde\.kwin\.EffectPluginFactory' "$kwindir/effect" 2>/dev/null | head -n 1)
if test -n "$iid_base"; and test -n "$plugver"
    say
    say "Plugin IID your KWin expects: $BOLD""$iid_base$plugver""$OFF"
    say "  (KWIN_EFFECT_FACTORY stamps this; a hand-rolled KF6 factory will not load.)"
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
