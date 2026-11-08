/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Software Dim — compositor-wide software dimmer for KWin 6.7 (Wayland).

    Every KWin API call below is annotated with the upstream source it was
    checked against. Run scripts/verify-api.sh on the target machine before
    building to confirm each of them still exists in your installed headers.
*/

#include "softwaredim.h"
#include "adaptivebrightness.h"
#include "luminancesampler.h"

#include <core/colorspace.h>
#include <core/backendoutput.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/globals.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <scene/scene.h>

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>

#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QPoint>

#include <epoxy/gl.h>
#include <chrono>
#include <cmath>
#include <optional>

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

struct SoftwareDimEffect::OutputData
{
    ~OutputData()
    {
        QObject::disconnect(dpmsConnection);
        QObject::disconnect(enabledConnection);
    }
    QMetaObject::Connection dpmsConnection;
    QMetaObject::Connection enabledConnection;
    std::unique_ptr<GLTexture> texture;
    std::unique_ptr<GLFramebuffer> framebuffer;
    std::unique_ptr<LuminanceSampler> sampler;
    QSize size;
    std::shared_ptr<ColorDescription> colorDescription;
    DimSum::AdaptiveBrightness adaptive;
    std::optional<double> meanLuminance;
    std::chrono::steady_clock::time_point lastSample{};
    std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
    bool adaptiveSupported = false;
    bool samplerWarningShown = false;
};

SoftwareDimEffect::SoftwareDimEffect()
{
    ensureResources();

    // Build the shader. ShaderTrait::MapTexture selects the generated vertex
    // shader variant that feeds the `texcoord0` varying; the fragment shader
    // itself must declare `sampler` / `texcoord0` / `fragColor` (KWin prepends
    // only `#version`, precision qualifiers and TRAIT_* defines — see
    // GLShader::preprocess in src/opengl/glshader.cpp).
    // Pattern: src/plugins/invert/invert.cpp (KWin 6.7):
    //   ShaderManager::instance()->generateShaderFromFile(
    //       ShaderTrait::MapTexture, QString(), QStringLiteral(":/effects/invert/shaders/invert.frag"));
    // The file is loaded verbatim: 6.7.5's generateShaderFromFile does not
    // resolve a "_core" variant, so software_dim.frag is the shader on every
    // context. Both files stay in the .qrc so the pair keeps working if a
    // future loader honours the documented "_core" suffix again.
    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture, QString(), QStringLiteral(":/softwaredim/shaders/software_dim.frag"));

    if (!m_shader) {
        // Fail safe: m_valid stays false, every hook becomes a pass-through
        // and the desktop keeps rendering exactly as before. The effect never
        // blanks the screen on a shader error. (generateShaderFromFile returns
        // nullptr when the file cannot be read or the program fails to link.)
        qCCritical(KWIN_SOFTWARE_DIM) << "Failed to compile the dim shader; the effect will stay inert.";
        return;
    }

    m_valid = true;

    m_luminanceShader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture, QString(), QStringLiteral(":/softwaredim/shaders/luminance.frag"));
    if (!m_luminanceShader) {
        m_luminanceShader.reset();
        qCWarning(KWIN_SOFTWARE_DIM) << "Adaptive shader unavailable; retaining fixed dimming.";
    }
    connect(&m_adaptiveTimer, &QTimer::timeout, this, [this] {
        updateAdaptiveTimer();
        if (m_adaptiveTimer.isActive()) {
            effects->addRepaintFull();
        }
    });
    connect(effects, &EffectsHandler::viewRemoved, this, [this](RenderView *view) {
        effects->makeOpenGLContextCurrent();
        m_outputs.erase(view);
        if (m_currentView == view) {
            m_currentView = nullptr;
        }
        updateAdaptiveTimer();
    });

    loadConfig();
    setupActions();
    updateAdaptiveTimer();

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

void SoftwareDimEffect::prePaintScreen(ScreenPrePaintData &data)
{
    // Mirrored physical displays can share a LogicalOutput while using
    // different scales/colour spaces. Capture by physical render view.
    m_currentView = data.view;
    effects->prePaintScreen(data);
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

    // Refresh the requested bindings explicitly: saved settings can disagree
    // with the shortcuts currently registered in the running accelerator.
    const auto registerBrightnessShortcut = [](QAction *action, Qt::Key key, Qt::Key legacyKey) {
        const QList<QKeySequence> shortcuts{QKeySequence(Qt::META | Qt::ALT | key)};
        auto *accel = KGlobalAccel::self();
        bool registered = accel->setGlobalShortcut(action, shortcuts);
        const QList<QKeySequence> legacy{QKeySequence(Qt::META | Qt::ALT | legacyKey)};
        if (accel->shortcut(action).isEmpty() || accel->shortcut(action) == legacy || accel->shortcut(action) == shortcuts) {
            registered = accel->setShortcut(action, shortcuts, KGlobalAccel::NoAutoloading);
        }
        if (!registered) {
            qCWarning(KWIN_SOFTWARE_DIM) << "Could not register DimSum shortcut:" << action->objectName();
        }
    };

    // Release both previous function-key bindings before swapping them, so
    // neither action blocks the other during registration.
    auto *accel = KGlobalAccel::self();
    for (const auto &[name, oldKey] : {
             std::pair{QStringLiteral("Software Dim: Increase Brightness"), Qt::Key_F6},
             std::pair{QStringLiteral("Software Dim: Decrease Brightness"), Qt::Key_F7}}) {
        auto *action = new QAction(this);
        action->setObjectName(name);
        accel->setGlobalShortcut(action, QKeySequence(Qt::META | Qt::ALT | oldKey));
        if (accel->shortcut(action) == QList<QKeySequence>{QKeySequence(Qt::META | Qt::ALT | oldKey)}) {
            accel->setShortcut(action, {}, KGlobalAccel::NoAutoloading);
        }
        delete action;
    }

    m_toggleAction = new QAction(this);
    m_toggleAction->setAutoRepeat(false);
    m_toggleAction->setObjectName(QStringLiteral("Software Dim"));
    m_toggleAction->setText(QStringLiteral("Toggle Software Dim"));
    KGlobalAccel::self()->setGlobalShortcut(m_toggleAction, QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
    connect(m_toggleAction, &QAction::triggered, this, &SoftwareDimEffect::toggleDim);

    m_increaseAction = new QAction(this);
    m_increaseAction->setObjectName(QStringLiteral("Software Dim: Increase Brightness"));
    m_increaseAction->setText(QStringLiteral("Software Dim: Increase Brightness"));
    registerBrightnessShortcut(m_increaseAction, Qt::Key_F7, Qt::Key_Up);
    connect(m_increaseAction, &QAction::triggered, this, &SoftwareDimEffect::raiseBrightness);

    m_decreaseAction = new QAction(this);
    m_decreaseAction->setObjectName(QStringLiteral("Software Dim: Decrease Brightness"));
    m_decreaseAction->setText(QStringLiteral("Software Dim: Decrease Brightness"));
    registerBrightnessShortcut(m_decreaseAction, Qt::Key_F6, Qt::Key_Down);
    connect(m_decreaseAction, &QAction::triggered, this, &SoftwareDimEffect::lowerBrightness);

    // Separate from F6/F7: these change the content-adaptive threshold only,
    // never the manual brightness ceiling or whether dimming is enabled.
    const auto registerThresholdShortcut = [this, &registerBrightnessShortcut](const QString &name, Qt::Key key, Qt::Key legacyKey, int steps) {
        auto *action = new QAction(this);
        action->setObjectName(name);
        action->setText(name);
        action->setAutoRepeat(true);
        // Migrate the old defaults in KDE's saved/active registrations too;
        // merely changing a default does not override a saved -/= binding.
        registerBrightnessShortcut(action, key, legacyKey);
        connect(action, &QAction::triggered, this, [this, steps] { adjustAdaptiveThreshold(steps); });
    };
    registerThresholdShortcut(QStringLiteral("Software Dim: Decrease Adaptive Threshold"), Qt::Key_Comma, Qt::Key_Minus, -1);
    registerThresholdShortcut(QStringLiteral("Software Dim: Increase Adaptive Threshold"), Qt::Key_Period, Qt::Key_Equal, 1);
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
    m_adaptiveEnabled = group.readEntry(QStringLiteral("AdaptiveEnabled"), true);
    m_adaptiveStrength = group.readEntry(QStringLiteral("AdaptiveStrength"), 0.8);
    m_adaptiveThreshold = group.readEntry(QStringLiteral("AdaptiveThreshold"), 0.12);
    if (!std::isfinite(m_adaptiveStrength)) {
        m_adaptiveStrength = 0.8;
    }
    if (!std::isfinite(m_adaptiveThreshold)) {
        m_adaptiveThreshold = 0.12;
    }
    m_adaptiveStrength = qBound(0.0, m_adaptiveStrength, 1.0);
    m_adaptiveThreshold = qBound(0.01, m_adaptiveThreshold, 1.0);

    // Clamp anything hand-edited in kwinrc into the supported range.
    if (!std::isfinite(m_dimStep) || m_dimStep <= 0.0 || m_dimStep > 0.5) {
        m_dimStep = kDefaultDimStep;
    }
    if (!std::isfinite(m_dimMin)) {
        m_dimMin = kDefaultDimMin;
    }
    if (!std::isfinite(m_dimAmount)) {
        m_dimAmount = kDefaultDimAmount;
    }
    m_dimMin = qBound(0.01, m_dimMin, kDimMax);
    m_dimAmount = qBound(m_dimMin, m_dimAmount, kDimMax);
}

void SoftwareDimEffect::storeConfig()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group = config->group(QStringLiteral("Effect-kwin4_effect_software_dim"));
    group.writeEntry(QStringLiteral("DimAmount"), m_dimAmount);
    group.writeEntry(QStringLiteral("Enabled"), m_enabled);
    group.writeEntry(QStringLiteral("AdaptiveThreshold"), m_adaptiveThreshold);
    group.sync();
}

void SoftwareDimEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags);
    loadConfig();

    if (!m_valid) {
        m_enabled = false;
    }

    if (!m_enabled) {
        effects->makeOpenGLContextCurrent();
        releaseOffscreen();
    }
    // An unrelated KWin reconfigure must not flash back to manual brightness.
    // Retarget changed settings from the last undimmed mean without resetting
    // the current gain. Pending undimmed measurements remain valid too.
    for (auto &[screen, output] : m_outputs) {
        if (!m_adaptiveEnabled || m_adaptiveStrength == 0) {
            output->adaptive.reset();
            effects->makeOpenGLContextCurrent();
            output->sampler.reset();
            output->lastSample = {};
            output->meanLuminance.reset();
        } else if (output->meanLuminance) {
            output->adaptive.observe(*output->meanLuminance, m_adaptiveThreshold, m_adaptiveStrength, true);
        }
    }
    updateAdaptiveTimer();
    effects->addRepaintFull();
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
        effects->makeOpenGLContextCurrent();
        releaseOffscreen();
    }
    updateAdaptiveTimer();

    storeConfig();
    qCInfo(KWIN_SOFTWARE_DIM) << (m_enabled ? "Enabled" : "Disabled") << "at dimAmount =" << m_dimAmount;

    // Request a full repaint so the change is visible on the next frame
    // without waiting for the desktop to damage itself.
    effects->addRepaintFull();
}

void SoftwareDimEffect::applyDimAmount(qreal amount)
{
    const QString direction = amount > m_dimAmount ? QStringLiteral("↑") : QStringLiteral("↓");
    m_dimAmount = qBound(m_dimMin, amount, kDimMax);
    storeConfig();
    const QString message = QStringLiteral("DimSum %1 %2%%3")
            .arg(direction)
            .arg(qRound(m_dimAmount * 100))
            .arg(!m_enabled ? QStringLiteral(" (dimming off)")
                           : m_adaptiveEnabled && m_adaptiveStrength > 0 ? QStringLiteral(" (adaptive ceiling)") : QString());
    showIndicator(message);

    if (m_enabled) {
        effects->addRepaintFull();
    }
}

void SoftwareDimEffect::showIndicator(const QString &message)
{
    auto call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
        QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));
    call << QStringLiteral("display-brightness") << message;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError()) {
            qCWarning(KWIN_SOFTWARE_DIM) << "DimSum indicator failed:" << reply.error().message();
        }
        finished->deleteLater();
    });
}

void SoftwareDimEffect::adjustAdaptiveThreshold(int steps)
{
    const qreal previous = m_adaptiveThreshold;
    m_adaptiveThreshold = DimSum::steppedThreshold(m_adaptiveThreshold, steps);
    storeConfig();
    // Explicit user changes bypass the measurement deadband, but keep the
    // current gain and temporal smoothing. No shader reload or resampling.
    if (m_adaptiveEnabled && m_adaptiveStrength > 0) {
        for (auto &[view, output] : m_outputs) {
            if (output->meanLuminance) {
                output->adaptive.observe(*output->meanLuminance, m_adaptiveThreshold, m_adaptiveStrength, true);
            }
        }
    }
    updateAdaptiveTimer();
    if (m_enabled) {
        effects->addRepaintFull();
    }
    const QString direction = m_adaptiveThreshold > previous ? QStringLiteral("↑")
                            : m_adaptiveThreshold < previous ? QStringLiteral("↓") : QStringLiteral("=");
    const QString state = !m_enabled ? QStringLiteral(" (dimming off)")
                        : !m_adaptiveEnabled || m_adaptiveStrength == 0 ? QStringLiteral(" (adaptive off)") : QString();
    showIndicator(QStringLiteral("DimSum threshold %1 %2%%3")
                      .arg(direction)
                      .arg(QString::number(m_adaptiveThreshold * 100, 'f', 1))
                      .arg(state));
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

bool SoftwareDimEffect::ensureOffscreen(OutputData &output, const RenderTarget &renderTarget, const RenderViewport &viewport)
{
    // Device pixels, including any output transform (rotation swaps W/H).
    // Pattern: ZoomEffect::ensureOffscreenData() in src/plugins/zoom/zoom.cpp.
    const QSize size = viewport.deviceSize();
    if (size.isEmpty()) {
        // Bug 485884: allocating a 0x0 texture produced
        // GL_INVALID_VALUE / GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT and a black
        // screen. Never allocate for an empty viewport.
        return false;
    }

    if (output.framebuffer && output.texture && output.size == size
        && output.colorDescription == renderTarget.colorDescription()) {
        return true;
    }

    output.framebuffer.reset();
    output.texture.reset();
    output.sampler.reset();
    output.adaptive.reset();
    output.meanLuminance.reset();
    output.lastSample = {};

    // HDR outputs need float precision; SDR is fine with 8 bit per channel.
    // Same selection as ZoomEffect::ensureOffscreenData().
    const GLenum textureFormat = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;
    output.texture = GLTexture::allocate(textureFormat, size);
    if (!output.texture) {
        return false;
    }
    output.texture->setFilter(GL_LINEAR);
    output.texture->setWrapMode(GL_CLAMP_TO_EDGE);

    output.framebuffer = std::make_unique<GLFramebuffer>(output.texture.get());
    if (!output.framebuffer->valid()) {
        output.framebuffer.reset();
        output.texture.reset();
        return false;
    }

    // A freshly allocated texture has undefined contents. The frame that
    // triggers a reallocation does not necessarily damage the whole output, so
    // start from opaque black instead of from whatever the driver left in VRAM
    // — otherwise stale garbage can show through until the next full repaint.
    GLFramebuffer::pushFramebuffer(output.framebuffer.get());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLFramebuffer::popFramebuffer();

    output.size = size;
    output.colorDescription = renderTarget.colorDescription();
    return true;
}

void SoftwareDimEffect::releaseOffscreen()
{
    m_outputs.clear();
    m_currentView = nullptr;
}

void SoftwareDimEffect::updateAdaptiveTimer()
{
    if (!isActive() || !m_adaptiveEnabled || m_adaptiveStrength == 0 || !m_luminanceShader) {
        m_adaptiveTimer.stop();
        return;
    }
    int interval = 1000;
    bool anyAwake = false;
    for (const auto &[view, output] : m_outputs) {
        const auto backend = view->backendOutput();
        const bool awake = !backend || (backend->isEnabled() && backend->dpmsMode() == BackendOutput::DpmsMode::On);
        if (!awake || !output->adaptiveSupported || (output->sampler && !output->sampler->valid())) {
            continue;
        }
        anyAwake = true;
        if (output->adaptive.settling()
            || (output->sampler && output->sampler->pending())) {
            interval = 16;
        }
    }
    if (!m_outputs.empty() && !anyAwake) {
        m_adaptiveTimer.stop();
        return;
    }
    if (!m_adaptiveTimer.isActive() || m_adaptiveTimer.interval() != interval) {
        m_adaptiveTimer.start(interval);
    }
}

bool SoftwareDimEffect::paintScreen(const RenderTarget &renderTarget,
                                    const RenderViewport &viewport,
                                    int mask,
                                    const Region &deviceRegion,
                                    LogicalOutput *screen)
{
    // ---------------------------------------------------------------- pass 0
    // Anything unusable falls back to plain compositing. The desktop is never
    // left without content.
    if (!m_valid || !m_shader || !m_currentView || m_currentView->logicalOutput() != screen) {
        return effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
    }

    auto &entry = m_outputs[m_currentView];
    if (!entry) {
        entry = std::make_unique<OutputData>();
        if (auto backend = m_currentView->backendOutput()) {
            const auto wake = [this] {
                updateAdaptiveTimer();
                if (m_adaptiveTimer.isActive()) {
                    effects->addRepaintFull();
                }
            };
            entry->dpmsConnection = connect(backend, &BackendOutput::dpmsModeChanged, this, wake);
            entry->enabledConnection = connect(backend, &BackendOutput::enabledChanged, this, wake);
        }
    }
    auto &output = *entry;
    const bool newCapture = !output.texture || output.size != viewport.deviceSize()
        || output.colorDescription != renderTarget.colorDescription();
    if (!ensureOffscreen(output, renderTarget, viewport)) {
        return effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
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
    // rendering methods"). Same call shape as ZoomEffect::paintScreen() in
    // src/plugins/zoom/zoom.cpp, including the QPoint() render offset.
    const RenderTarget offscreenTarget(output.framebuffer.get(), renderTarget.colorDescription());
    const RenderViewport offscreenViewport(viewport.renderRect(), viewport.scale(), offscreenTarget, QPoint());

    GLFramebuffer::pushFramebuffer(output.framebuffer.get());
    // A new capture must contain the ENTIRE desktop, not just the most recent
    // damage: otherwise the sampler would interpret undefined/black areas as
    // dark content. Afterwards retain per-output contents across partial paints.
    const bool painted = effects->paintScreen(offscreenTarget, offscreenViewport, mask,
                                              newCapture ? Region::infinite() : deviceRegion, screen);
    if (!painted) {
        // KWin's contract requires stopping immediately on a failed paint,
        // notably after a graphics reset. Never sample or draw stale content.
        return false;
    }
    GLFramebuffer::popFramebuffer();

    // ---------------------------------------------------------------- pass 2
    // Draw the captured image back to the real framebuffer through the dim
    // shader. The once-per-second adaptive sample is taken from the ORIGINAL
    // scene here, never from our own dimmed output (which would cause feedback).
    const bool blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);

    const auto now = std::chrono::steady_clock::now();
    const auto tf = renderTarget.colorDescription()->transferFunction().type;
    output.adaptiveSupported = m_adaptiveEnabled && m_adaptiveStrength > 0 && m_luminanceShader
        && (tf == TransferFunction::gamma22 || tf == TransferFunction::sRGB);
    if (output.adaptiveSupported) {
        if (!output.sampler) {
            output.sampler = std::make_unique<LuminanceSampler>();
        }
        if (const auto mean = output.sampler->takeReady()) {
            output.meanLuminance = mean;
            output.adaptive.observe(*mean, m_adaptiveThreshold, m_adaptiveStrength);
        }
        if (output.sampler->valid() && !output.sampler->pending()
            && now - output.lastSample >= std::chrono::seconds(1)) {
            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            // GLES doesn't expose GL_FRAMEBUFFER_SRGB unless write-control is
            // present. Our sample texture is linear RGBA8, never sRGB.
            const bool srgbControl = !epoxy_is_desktop_gl() ? epoxy_has_gl_extension("GL_EXT_sRGB_write_control") : true;
            const bool srgb = srgbControl && glIsEnabled(GL_FRAMEBUFFER_SRGB);
            glDisable(GL_SCISSOR_TEST);
            if (srgbControl) {
                glDisable(GL_FRAMEBUFFER_SRGB);
            }
            const auto &xyz = renderTarget.colorDescription()->containerColorimetry().toXYZ();
            output.sampler->submit(*output.texture, *m_luminanceShader, int(tf), QVector3D(xyz(1, 0), xyz(1, 1), xyz(1, 2)));
            if (scissor) {
                glEnable(GL_SCISSOR_TEST);
            }
            if (srgb) {
                glEnable(GL_FRAMEBUFFER_SRGB);
            }
            output.lastSample = now;
        }
        if (!output.sampler->valid()) {
            if (!output.samplerWarningShown) {
                qCWarning(KWIN_SOFTWARE_DIM) << "Adaptive sampling unavailable on an output; retaining fixed dimming.";
                output.samplerWarningShown = true;
            }
            output.adaptive.reset(); // Unsupported GL path: fixed dimming.
        }
        output.adaptive.advance(std::chrono::duration<double>(now - output.lastFrame).count());
    } else {
        output.adaptive.reset();
    }
    output.lastFrame = now;
    updateAdaptiveTimer();

    ShaderManager::instance()->pushShader(m_shader.get());
    m_shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    m_shader->setUniform("dimAmount", float(m_dimAmount * output.adaptive.gain()));
    output.texture->render(viewport.renderRect().size() * viewport.scale());
    ShaderManager::instance()->popShader();

    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    }

    return painted;
}

} // namespace KWin

#include "moc_softwaredim.cpp"
