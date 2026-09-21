/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Popup slider for Software Dim — triggered by hotkey, draggable, auto-hides.
*/

#pragma once

#include <QDialog>
#include <QTimer>

class QSlider;
class QLabel;
class QCheckBox;

class SliderPopup : public QDialog
{
    Q_OBJECT

public:
    explicit SliderPopup(QWidget *parent = nullptr);
    ~SliderPopup() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSliderChanged(int value);
    void onToggleChanged(bool checked);
    void loadConfig();
    void saveConfig();
    void triggerReconfigure();
    void hideAfterDelay();
    void resetHideTimer();

private:
    void setupUi();
    void updateLabel(int value);

    QSlider *m_slider = nullptr;
    QLabel *m_label = nullptr;
    QLabel *m_title = nullptr;
    QCheckBox *m_toggle = nullptr;

    QTimer m_hideTimer;
    QPoint m_dragPos;
    bool m_dragging = false;

    qreal m_dimMin = 0.05;
    qreal m_dimAmount = 0.20;
    bool m_enabled = false;

    bool m_updating = false;
};
