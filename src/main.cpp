/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Plugin entry point.

    KWin discovers native effects as Qt/KF6 plugins, not as KPackage bundles:
    the metadata.json sitting next to this file is compiled into the .so by the
    KWIN_EFFECT_FACTORY macro below, and the finished library is dropped into
    KWin's effect plugin directory (see CMakeLists.txt -> KWIN_EFFECTS_INSTALL_DIR).
*/

#include "softwaredim.h"

#ifndef KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED
#include <KPluginFactory>
#endif

#ifdef KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED

namespace KWin
{

/*
 * KWin's own macro, from <effect/effect.h>. It generates the
 * EffectPluginFactory subclass, embeds metadata.json and stamps the plugin IID
 * that this exact KWin version expects. This is what every in-tree effect's
 * main.cpp uses (e.g. src/plugins/invert/main.cpp — same shape, with
 * KWIN_EFFECT_FACTORY_SUPPORTED; the _SUPPORTED_ENABLED form additionally
 * carries our enabledByDefault()).
 */
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(SoftwareDimEffect,
                                      "metadata.json",
                                      return SoftwareDimEffect::supported();,
                                      return SoftwareDimEffect::enabledByDefault();)

} // namespace KWin

#else

/*
 * Fallback for a KWin whose headers do not export KWIN_EFFECT_FACTORY_*.
 *
 * WARNING: this makes the build succeed, but KWin will not load the result.
 * KWin 6.7.5 stamps its effect plugins with a version-specific IID —
 * `org.kde.kwin.EffectPluginFactory6.7.5` (6.7.90 uses
 * `org.kde.kwin.EffectPluginFactory6.7.90`) — and a plain KF6 factory carries
 * no such IID, so the plugin is rejected at load time.
 *
 * So if you land here, do not go debugging the effect. Install the kwin headers
 * that match your running compositor and rebuild so the branch above is taken.
 * On Arch there is no -dev split, so `sudo pacman -S kwin` already covers it.
 * Check which branch you are getting with:
 *
 *   grep -rn "KWIN_EFFECT_FACTORY\|EffectPluginFactory" /usr/include/kwin/effect/
 */
K_PLUGIN_FACTORY_WITH_JSON(SoftwareDimEffectFactory,
                           "metadata.json",
                           registerPlugin<KWin::SoftwareDimEffect>();)

#endif

#include "main.moc"
