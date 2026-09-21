/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT
*/

#include "sliderpopup.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

static constexpr int kHideDelayMs = 3500;
static constexpr int kSliderMinPercent = 5;
static constexpr int kSliderMaxPercent = 100;

SliderPopup::SliderPopup(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFocusPolicy(Qt::StrongFocus);

    setupUi();
    loadConfig();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(kHideDelayMs);
    connect(&m_hideTimer, &QTimer::timeout, this, &SliderPopup::hideAfterDelay);

    // Auto-hide when focus lost (click outside)
    installEventFilter(this);

    // Center on primary screen
    if (auto *screen = QApplication::primaryScreen()) {
        const QRect geo = screen->geometry();
        const QSize sz = sizeHint();
        move(geo.center() - QPoint(sz.width() / 2, sz.height() / 2));
    }

    resetHideTimer();
}

SliderPopup::~SliderPopup() = default;

void SliderPopup::setupUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("root"));
    root->setStyleSheet(R"(
        #root {
            background-color: rgba(35, 38, 46, 230);
            border-radius: 16px;
            border: 1px solid rgba(255,255,255,30);
        }
        QLabel {
            color: white;
        }
        QCheckBox {
            color: white;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 4px;
            border: 1px solid rgba(255,255,255,80);
            background: rgba(255,255,255,20);
        }
        QCheckBox::indicator:checked {
            background: #89b4fa;
            border: 1px solid #89b4fa;
        }
        QSlider::groove:horizontal {
            height: 6px;
            background: rgba(255,255,255,30);
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            width: 22px;
            height: 22px;
            margin: -8px 0;
            border-radius: 11px;
            background: white;
            border: 2px solid #89b4fa;
        }
        QSlider::sub-page:horizontal {
            background: #89b4fa;
            border-radius: 3px;
        }
    )");

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(30);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 8);
    root->setGraphicsEffect(shadow);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->addWidget(root);

    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    m_title = new QLabel(QStringLiteral("Software Dim"), root);
    m_title->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    layout->addWidget(m_title);

    auto *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(12);

    m_slider = new QSlider(Qt::Horizontal, root);
    m_slider->setRange(kSliderMinPercent, kSliderMaxPercent);
    m_slider->setSingleStep(5);
    m_slider->setPageStep(10);
    m_slider->setTracking(true);
    sliderRow->addWidget(m_slider, 1);

    m_label = new QLabel(QStringLiteral("20%"), root);
    m_label->setMinimumWidth(48);
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_label->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 13px;"));
    sliderRow->addWidget(m_label);

    layout->addLayout(sliderRow);

    m_toggle = new QCheckBox(QStringLiteral("Enabled"), root);
    layout->addWidget(m_toggle);

    auto *hint = new QLabel(QStringLiteral("Meta+Alt+D toggle • Meta+Alt+↑/↓ step • Esc close"), root);
    hint->setStyleSheet(QStringLiteral("color: rgba(255,255,255,120); font-size: 10px;"));
    layout->addWidget(hint);

    connect(m_slider, &QSlider::valueChanged, this, &SliderPopup::onSliderChanged);
    connect(m_toggle, &QCheckBox::toggled, this, &SliderPopup::onToggleChanged);

    // Drag to move popup: any mouse drag on root moves window
    root->installEventFilter(this);

    setFixedSize(360, 130);
}

void SliderPopup::loadConfig()
{
    const auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    const KConfigGroup group = config->group(QStringLiteral("Effect-kwin4_effect_software_dim"));

    m_dimAmount = group.readEntry(QStringLiteral("DimAmount"), 0.20);
    m_dimMin = group.readEntry(QStringLiteral("DimMin"), 0.05);
    m_enabled = group.readEntry(QStringLiteral("Enabled"), false);

    m_updating = true;
    const int percent = qBound(kSliderMinPercent, int(m_dimAmount * 100.0), kSliderMaxPercent);
    m_slider->setValue(percent);
    m_toggle->setChecked(m_enabled);
    updateLabel(percent);
    m_updating = false;
}

void SliderPopup::saveConfig()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group = config->group(QStringLiteral("Effect-kwin4_effect_software_dim"));
    group.writeEntry(QStringLiteral("DimAmount"), m_dimAmount);
    group.writeEntry(QStringLiteral("Enabled"), m_enabled);
    group.sync();
}

void SliderPopup::triggerReconfigure()
{
    // Tell KWin to re-read kwinrc. This is what kwriteconfig + qdbus does.
    // Use D-Bus directly for speed, fallback to qdbus6 binary.
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("reconfigure"));

    QDBusConnection::sessionBus().asyncCall(msg);

    // Also ensure full repaint if enabled
    if (m_enabled) {
        // addRepaintFull is called by the effect itself on reconfigure,
        // but we trigger it again via D-Bus to be safe
        QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"),
                QStringLiteral("/Effects"),
                QStringLiteral("org.kde.kwin.Effects"),
                QStringLiteral("reconfigureEffect"),
            ) << QStringLiteral("kwin4_effect_software_dim"));
    }
}

void SliderPopup::onSliderChanged(int value)
{
    if (m_updating) {
        return;
    }

    updateLabel(value);
    m_dimAmount = qBound(m_dimMin, value / 100.0, 1.0);

    saveConfig();
    triggerReconfigure();
    resetHideTimer();
}

void SliderPopup::onToggleChanged(bool checked)
{
    if (m_updating) {
        return;
    }

    m_enabled = checked;
    saveConfig();
    triggerReconfigure();
    resetHideTimer();
}

void SliderPopup::updateLabel(int value)
{
    m_label->setText(QStringLiteral("%1%").arg(value));
}

void SliderPopup::hideAfterDelay()
{
    hide();
    // Quit app when popup hides, unless user wants it persistent
    // For popup behavior, we quit after hide
    QApplication::quit();
}

void SliderPopup::resetHideTimer()
{
    m_hideTimer.stop();
    m_hideTimer.start(kHideDelayMs);
}

void SliderPopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_dragging = true;
    }
    QDialog::mousePressEvent(event);
    resetHideTimer();
}

void SliderPopup::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPos);
    }
    QDialog::mouseMoveEvent(event);
    resetHideTimer();
}

void SliderPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        QApplication::quit();
        return;
    }
    QDialog::keyPressEvent(event);
    resetHideTimer();
}

void SliderPopup::focusOutEvent(QFocusEvent *event)
{
    QDialog::focusOutEvent(event);
    // Don't immediately hide on focus out, give a short grace
    // Actually for popup behavior, hide after delay already handles it
}

bool SliderPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::Wheel) {
        resetHideTimer();
    }
    // Allow dragging from root widget
    if (watched->objectName() == QStringLiteral("root")) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragPos = me->globalPosition().toPoint() - frameGeometry().topLeft();
                m_dragging = true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (m_dragging && (me->buttons() & Qt::LeftButton)) {
                move(me->globalPosition().toPoint() - m_dragPos);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
        }
    }
    return QDialog::eventFilter(watched, event);
}
