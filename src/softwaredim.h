/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Software Dim — a compositor-wide software dimmer for KWin 6.7 (Wayland).

    Architecture in one sentence: this effect inserts a post-processing pass at
    the very end of the compositor's per-output paint, so the dimming is applied
    to the final rendered desktop image (wallpaper + windows + panels +
    notifications) instead of to individual windows.

    See README.md, section "How it works", for the full picture.
*/

#pragma once

#include "libkwineffects/kwineffects.h"

#include <QSize>
#include <memory>

class QAction;

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class RenderTarget;
class RenderViewport;

/**
 * Multiplies the fully composited output of every screen by @c m_dimAmount.
 *
 * The effect is implemented as a plain KWin::Effect that overrides
 * paintScreen(): it renders the whole scene of the output into an offscreen
 * texture, then draws that texture back to the real framebuffer through a one
 * multiply fragment shader. This is the same mechanism KWin itself uses for its
 * Zoom/Magnifier effect.
 */
class SoftwareDimEffect : public Effect
{
    Q_OBJECT

public:
    SoftwareDimEffect();
    ~SoftwareDimEffect() override;

    /**
     * Called by KWin before instantiating the effect.
     * Requires OpenGL compositing: the dim pass is a GPU fragment operation.
     */
    static bool supported();

    /**
     * Called by KWin to decide whether to enable the effect on a fresh profile.
     * Kept false: a dimmer that turns itself on at first login is surprising.
     */
    static bool enabledByDefault();

    /**
     * The compositor-wide hook. Called once per output, per frame.
     *
     * Signature matches KWin 6.7 (paintScreen returns void; it returns
     * [[nodiscard]] bool from KWin 6.7.90 / Plasma 6.8 onwards).
     */
    void paintScreen(const RenderTarget &renderTarget,
                     const RenderViewport &viewport,
                     int mask,
                     const QRegion &region,
                     Output *screen) override;

    bool isActive() const override;

    /**
     * Without this, KWin may hand a fullscreen window's buffer straight to the
     * display controller ("direct scanout"), bypassing the compositor entirely.
     * The dim pass would then not run and a fullscreen app would stay bright.
     */
    bool blocksDirectScanout() const override;

    /**
     * High value == late in the effect chain == our offscreen capture sees the
     * result of every other effect.
     */
    int requestedEffectChainPosition() const override;

    /**
     * Re-read kwinrc. Called by KWin on reconfigure (e.g. `qdbus6 org.kde.KWin
     * /KWin reconfigure`).
     */
    void reconfigure(ReconfigureFlags flags) override;

private:
    void setupActions();
    void loadConfig();
    void storeConfig();

    void toggleDim();
    void raiseBrightness();
    void lowerBrightness();
    void applyDimAmount(qreal amount);

    /** Allocates/reallocates the offscreen texture + FBO for @p viewport. */
    bool ensureOffscreen(const RenderViewport &viewport);
    void releaseOffscreen();

    std::unique_ptr<GLShader> m_shader;
    std::unique_ptr<GLTexture> m_texture;
    std::unique_ptr<GLFramebuffer> m_framebuffer;
    QSize m_textureSize;

    qreal m_dimAmount = 0.20;
    qreal m_dimStep = 0.05;
    qreal m_dimMin = 0.05;
    bool m_enabled = false;
    bool m_valid = false;

    QAction *m_toggleAction = nullptr;
    QAction *m_increaseAction = nullptr;
    QAction *m_decreaseAction = nullptr;
};

} // namespace KWin
