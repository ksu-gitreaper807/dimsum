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

// KWin 6.7 installs its effect API under <prefix>/include/kwin/ and exports
// that directory as the INTERFACE include path of the KWin::kwin target, so
// consumers include the headers without the leading "kwin/":
//   #include <effect/effect.h>   ->  /usr/include/kwin/effect/effect.h
#include <effect/effect.h>

#include <QSize>
#include <QTimer>
#include <memory>
#include <unordered_map>

class QAction;

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class LogicalOutput;
class RenderTarget;
class RenderViewport;
class RenderView;

/**
 * Multiplies each composited output by the manual ceiling @c m_dimAmount and
 * an optional smoothed content-adaptive gain measured once per second.
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
    void prePaintScreen(ScreenPrePaintData &data) override;

    /**
     * The compositor-wide hook. Called once per output, per frame.
     *
     * Signature matches KWin 6.7.x: the painted region is a KWin::Region in
     * device pixels and the output is a LogicalOutput. (KWin 6.7.90 / Plasma
     * 6.8 additionally changes the return type to [[nodiscard]] bool.)
     */
    bool paintScreen(const RenderTarget &renderTarget,
                     const RenderViewport &viewport,
                     int mask,
                     const Region &deviceRegion,
                     LogicalOutput *screen) override;

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
    void adjustAdaptiveThreshold(int steps);
    void showIndicator(const QString &message);

    /** Allocates/reallocates the offscreen texture + FBO for @p viewport. */
    struct OutputData;
    bool ensureOffscreen(OutputData &output, const RenderTarget &renderTarget, const RenderViewport &viewport);
    void releaseOffscreen();
    void updateAdaptiveTimer();

    std::unique_ptr<GLShader> m_shader;
    std::unique_ptr<GLShader> m_luminanceShader;
    std::unordered_map<RenderView *, std::unique_ptr<OutputData>> m_outputs;
    RenderView *m_currentView = nullptr;
    QTimer m_adaptiveTimer;
    bool m_adaptiveEnabled = true;
    qreal m_adaptiveStrength = 0.8;
    qreal m_adaptiveThreshold = 0.12;

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
