/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Plugin entry point.

    KWin discovers native effects as Qt/KF6 plugins, not as KPackage bundles:
    the metadata.json sitting next to this file is compiled into the .so by the
    KWIN_EFFECT_CLASS macro below, and the finished library is dropped into
    KWin's effect plugin directory (see CMakeLists.txt -> KWIN_EFFECTS_INSTALL_DIR).
*/

#include "softwaredim.h"

#ifndef KWIN_EFFECT_CLASS
#include <KPluginFactory>
#endif

#ifdef KWIN_EFFECT_CLASS

namespace KWin
{

/*
 * KWin's own macro. It generates the KPluginFactory subclass, embeds
 * metadata.json and stamps the plugin IID that this exact KWin version expects.
 * This is what every in-tree effect's main.cpp uses (e.g.
 * src/plugins/colorblindnesscorrection/main.cpp — 16 lines, same shape).
 */
KWIN_EFFECT_CLASS(SoftwareDimEffect, "kwin4_effect_software_dim")

} // namespace KWin

#else

/*
 * Fallback for a KWin whose headers do not export KWIN_EFFECT_CLASS.
 *
 * This is a plain KF6 plugin factory. It builds, but KWin may refuse to load
 * the result if your KWin checks the plugin IID (KWin 6.7 stamps a
 * version-specific IID). If the effect does not show up in
 *   qdbus6 org.kde.KWin /Effects listOfEffects
 * that is why: install the matching kwin headers (Arch: `sudo pacman -S kwin`)
 * and rebuild so the KWIN_EFFECT_CLASS branch above is taken instead.
 */
K_PLUGIN_FACTORY_WITH_JSON(SoftwareDimEffectFactory,
                           "metadata.json",
                           registerPlugin<KWin::SoftwareDimEffect>();)

#endif

#include "main.moc"
