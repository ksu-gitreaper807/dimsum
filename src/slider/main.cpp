/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    dimsum-slider — popup slider for Software Dim effect.
    Triggered by hotkey (Meta+Alt+S by default), draggable, auto-hides.
*/

#include "sliderpopup.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("dimsum-slider"));
    QApplication::setApplicationDisplayName(QStringLiteral("Software Dim Slider"));
    QApplication::setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Popup slider for kwin4_effect_software_dim"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("persistent"),
                                        QStringLiteral("Don't quit when popup hides, keep running")));
    parser.process(app);

    SliderPopup popup;
    popup.show();
    popup.activateWindow();
    popup.raise();

    return app.exec();
}
