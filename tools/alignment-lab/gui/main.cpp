// SPDX-License-Identifier: GPL-3.0-or-later

#include "Lab.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Alignment Lab"));

    Lab lab;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("lab"), QVariant::fromValue(&lab)}});
    engine.loadFromModule(QStringLiteral("Swiftify.AlignmentLab"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    lab.refresh();
    return app.exec();
}
