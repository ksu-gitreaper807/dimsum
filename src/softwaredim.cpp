/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Software Dim — compositor-wide software dimmer for KWin 6.7 (Wayland).

    Every KWin API call below is annotated with the upstream source it was
    checked against. Run scripts/verify-api.sh on the target machine before
    building to confirm each of them still exists in your installed headers.
*/

#include "softwaredim.h"

#include "libkwineffects/glframebuffer.h"
#include "libkwineffects/glshader.h"
#include "libkwineffects/glshadermanager.h"
#include "libkwineffects/gltexture.h"
#include "libkwineffects/kwinglobals.h"
#include "libkwineffects/rendertarget.h"
#include "libkwineffects/renderviewport.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>

#include <QAction>
#include <QKeySequence>
#include <QLoggingCategory>

#include <epoxy/gl.h>

/*
 * Logging. Event based only (load, toggle, reconfigure, shader failure) —
 * nothing in here is reachable from the per-frame paint path, so the journal
 * is never spammed. Read it with:
 *
 *   journalctl -b -u plasma-kwin_wayland --since "5 minutes ago" \
 *       | grep -i software_dim
 */
Q_LOGGING_CATEGORY(KWIN_SOFTWARE_DIM, "kwin_effect_software_dim", QtWarningMsg)

static void ensureResources()
{
    // The plugin is a shared library, so the resource is registered when the
    // library is loaded — but calling this explicitly costs nothing and makes
    // the resource work even if the effect is ever linked statically.
    // Pattern: src/plugins/invert/invert.cpp (KWin 6.7).
    Q_INIT_RESOURCE(softwaredim);
}

namespace KWin
{

static constexpr qreal kDefaultDimAmount = 0.20;
static constexpr qreal kDefaultDimStep = 0.05;
static constexpr qreal kDefaultDimMin = 0.05;
static constexpr qreal kDimMax = 1.00;

SoftwareDimEffect::SoftwareDimEffect()
{
    ensureResources();

    // Build the shader. ShaderTrait::MapTexture makes KWin inject the
    // `sampler` uniform and bind the texture unit for us.
    // Pattern: src/plugins/invert/invert.cpp (KWin 6.7):
    //   ShaderManager::instance()->generateShaderFromFile(
    //       ShaderTrait::MapTexture, QString(), QStringLiteral(":/effects/invert/shaders/invert.frag"));
    // KWin appends "_core" before the extension when the context is desktop
    // OpenGL, which is why both .frag files are shipped in the .qrc.
    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture, QString(), QStringLiteral(":/softwaredim/shaders/software_dim.frag"));

    if (!m_shader || !m_shader->isValid()) {
        // Fail safe: m_valid stays false, every hook becomes a pass-through
        // and the desktop keeps rendering exactly as before. The effect never
        // blanks the screen on a shader error.
        qCCritical(KWIN_SOFTWARE_DIM) << "Failed to compile the dim shader; the effect will stay inert.";
        m_shader.reset();
        return;
    }

    m_valid = true;

    loadConfig();
    setupActions();

    qCInfo(KWIN_SOFTWARE_DIM) << "Loaded. dimAmount =" << m_dimAmount << " enabled =" << m_enabled;
}

SoftwareDimEffect::~SoftwareDimEffect()
{
    // GL objects are owned by unique_ptrs; releasing them here is safe because
    // KWin destroys effects while the OpenGL context is still current.
    releaseOffscreen();
}

bool SoftwareDimEffect::supported()
{
    // The dim pass is a GPU fragment operation, so OpenGL compositing is
    // mandatory. Pattern: src/plugins/invert/invert.cpp (KWin 6.7).
    return effects->compositingType() == OpenGLCompositing;
}

bool SoftwareDimEffect::enabledByDefault()
{
    return false;
}

void SoftwareDimEffect::setupActions()
{
    // Pattern: src/plugins/invert/invert.cpp (KWin 6.7) —
    //   QAction *a = new QAction(this);
    //   a->setAutoRepeat(false);
    //   a->setObjectName(QStringLiteral("Invert"));
    //   a->setText(i18n("Toggle Invert Effect"));
    //   KGlobalAccel::self()->setGlobalShortcut(a, QKeySequence(Qt::CTRL | Qt::META | Qt::Key_I));
    //   connect(a, &QAction::triggered, this, &InvertEffect::toggleScreenInversion);
    //
    // setAutoRepeat(false) on the toggle keeps one keypress from flipping the
    // dimmer on and off again while the key is held. The brightness actions
    // deliberately DO repeat.

    m_toggleAction = new QAction(this);
    m_toggleAction->setAutoRepeat(false);
    m_toggleAction->setObjectName(QStringLiteral("Software Dim"));
    m_toggleAction->setText(QStringLiteral("Toggle Software Dim"));
    KGlobalAccel::self()->setGlobalShortcut(m_toggleAction, QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
    connect(m_toggleAction, &QAction::triggered, this, &SoftwareDimEffect::toggleDim);

    m_increaseAction = new QAction(this);
    m_increaseAction->setObjectName(QStringLiteral("Software Dim: Increase Brightness"));
    m_increaseAction->setText(QStringLiteral("Software Dim: Increase Brightness"));
    KGlobalAccel::self()->setGlobalShortcut(m_increaseAction, QKeySequence(Qt::META | Qt::ALT | Qt::Key_Up));
    connect(m_increaseAction, &QAction::triggered, this, &SoftwareDimEffect::raiseBrightness);

    m_decreaseAction = new QAction(this);
    m_decreaseAction->setObjectName(QStringLiteral("Software Dim: Decrease Brightness"));
    m_decreaseAction->setText(QStringLiteral("Software Dim: Decrease Brightness"));
    KGlobalAccel::self()->setGlobalShortcut(m_decreaseAction, QKeySequence(Qt::META | Qt::ALT | Qt::Key_Down));
    connect(m_decreaseAction, &QAction::triggered, this, &SoftwareDimEffect::lowerBrightness);
}

void SoftwareDimEffect::loadConfig()
{
    // Effects own a group named "Effect-<plugin id>" inside kwinrc.
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    const KConfigGroup group = config->group(QStringLiteral("Effect-kwin4_effect_software_dim"));

    m_dimAmount = group.readEntry(QStringLiteral("DimAmount"), kDefaultDimAmount);
    m_dimStep = group.readEntry(QStringLiteral("DimStep"), kDefaultDimStep);
    m_dimMin = group.readEntry(QStringLiteral("DimMin"), kDefaultDimMin);
    m_enabled = group.readEntry(QStringLiteral("Enabled"), false);

    // Clamp anything hand-edited in kwinrc into the supported range.
    m_dimAmount = qBound(kDimMin, m_dimAmount, kDimMax);
    if (m_dimStep <= 0.0 || m_dimStep > 0.5) {
        m_dimStep = kDefaultDimStep;
    }
    m_dimMin = qBound(0.01, m_dimMin, kDimMax);
}

void SoftwareDimEffect::storeConfig()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group = config->group(QStringLiteral("Effect-kwin4_effect_software_dim"));
    group.writeEntry(QStringLiteral("DimAmount"), m_dimAmount);
    group.writeEntry(QStringLiteral("Enabled"), m_enabled);
    group.sync();
}

void SoftwareDimEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags);
    const bool wasEnabled = m_enabled;
    const qreal wasAmount = m_dimAmount;

    loadConfig();

    if (!m_valid) {
        m_enabled = false;
    }

    if (wasEnabled != m_enabled || !qFuzzyCompare(wasAmount, m_dimAmount)) {
        qCInfo(KWIN_SOFTWARE_DIM) << "Reconfigured: enabled =" << m_enabled << " dimAmount =" << m_dimAmount;
        effects->addRepaintFull();
    }
}

void SoftwareDimEffect::toggleDim()
{
    if (!m_valid) {
        qCWarning(KWIN_SOFTWARE_DIM) << "Toggle ignored: the shader did not load.";
        return;
    }

    m_enabled = !m_enabled;

    // Free the offscreen texture immediately when switching off: an effect
    // that is turned off should not keep a full-screen texture allocated.
    if (!m_enabled) {
        releaseOffscreen();
    }

    storeConfig();
    qCInfo(KWIN_SOFTWARE_DIM) << (m_enabled ? "Enabled" : "Disabled") << "at dimAmount =" << m_dimAmount;

    // Request a full repaint so the change is visible on the next frame
    // without waiting for the desktop to damage itself.
    effects->addRepaintFull();
}

void SoftwareDimEffect::applyDimAmount(qreal amount)
{
    m_dimAmount = qBound(m_dimMin, amount, kDimMax);
    storeConfig();

    if (m_enabled) {
        effects->addRepaintFull();
    }
}

void SoftwareDimEffect::raiseBrightness()
{
    applyDimAmount(m_dimAmount + m_dimStep);
    qCInfo(KWIN_SOFTWARE_DIM) << "dimAmount ->" << m_dimAmount;
}

void SoftwareDimEffect::lowerBrightness()
{
    applyDimAmount(m_dimAmount - m_dimStep);
    qCInfo(KWIN_SOFTWARE_DIM) << "dimAmount ->" << m_dimAmount;
}

bool SoftwareDimEffect::isActive() const
{
    // While inactive KWin skips this effect entirely: no offscreen texture is
    // allocated and paintScreen() is never entered.
    return m_valid && m_enabled;
}

bool SoftwareDimEffect::blocksDirectScanout() const
{
    // A dimmer that a fullscreen application can bypass is not a dimmer.
    // Returning true while active forces KWin to keep compositing.
    return isActive();
}

int SoftwareDimEffect::requestedEffectChainPosition() const
{
    // Late in the chain, so the offscreen capture already contains the result
    // of every other effect (blur, color picker, other post-processing).
    return 99;
}

bool SoftwareDimEffect::ensureOffscreen(const RenderViewport &viewport)
{
    // Device pixels: renderRect() is in logical coordinates, scale() is the
    // output's fractional scaling factor.
    const QSize size = (QSizeF(viewport.renderRect().size()) * viewport.scale()).toSize();
    if (size.isEmpty()) {
        // Bug 485884: allocating a 0x0 texture produced
        // GL_INVALID_VALUE / GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT and a black
        // screen. Never allocate for an empty viewport.
        return false;
    }

    if (m_framebuffer && m_texture && m_textureSize == size) {
        return true;
    }

    releaseOffscreen();

    m_texture = GLTexture::allocate(GL_RGBA8, size);
    if (!m_texture) {
        return false;
    }
    m_texture->setFilter(GL_LINEAR);

    m_framebuffer = GLFramebuffer::create(m_texture.get());
    if (!m_framebuffer || !m_framebuffer->valid()) {
        releaseOffscreen();
        return false;
    }

    // A freshly allocated texture has undefined contents. The frame that
    // triggers a reallocation does not necessarily damage the whole output, so
    // start from opaque black instead of from whatever the driver left in VRAM
    // — otherwise stale garbage can show through until the next full repaint.
    GLFramebuffer::pushFramebuffer(m_framebuffer.get());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLFramebuffer::popFramebuffer();

    m_textureSize = size;
    return true;
}

void SoftwareDimEffect::releaseOffscreen()
{
    m_framebuffer.reset();
    m_texture.reset();
    m_textureSize = QSize();
}

void SoftwareDimEffect::paintScreen(const RenderTarget &renderTarget,
                                    const RenderViewport &viewport,
                                    int mask,
                                    const QRegion &region,
                                    Output *screen)
{
    // ---------------------------------------------------------------- pass 0
    // Anything unusable falls back to plain compositing. The desktop is never
    // left without content.
    if (!m_valid || !m_shader) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    if (!ensureOffscreen(viewport)) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    // ---------------------------------------------------------------- pass 1
    // Render the entire scene of this output — wallpaper, windows, panels,
    // notifications, other effects — into our own texture.
    //
    // effects->paintScreen() does NOT recurse into this effect; KWin's
    // EffectsHandler tracks the current position in the effect chain and the
    // call continues with the *next* effect, ending in the scene. This is the
    // documented KWin 6 pattern (see Zamundaaa, "Porting away from
    // gbm_surface": "if an effect wants to override the properties now, it
    // just creates its own RenderTarget and RenderViewport and passes that to
    // rendering methods").
    const RenderTarget offscreenTarget(m_framebuffer.get(), renderTarget.colorDescription());
    const RenderViewport offscreenViewport(viewport.renderRect(), viewport.scale(), offscreenTarget);

    GLFramebuffer::pushFramebuffer(m_framebuffer.get());
    effects->paintScreen(offscreenTarget, offscreenViewport, mask, region, screen);
    GLFramebuffer::popFramebuffer();

    // ---------------------------------------------------------------- pass 2
    // Draw the captured image back to the real framebuffer through the dim
    // shader. This is one full-screen textured quad and one trivial fragment
    // multiply — no CPU readback, no per-frame allocations.
    const bool blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);

    ShaderManager::instance()->pushShader(m_shader.get());
    m_shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    m_shader->setUniform("dimAmount", float(m_dimAmount));
    m_texture->render(QSizeF(viewport.renderRect().size()) * viewport.scale());
    ShaderManager::instance()->popShader();

    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    }
}

} // namespace KWin

#include "moc_softwaredim.cpp"
