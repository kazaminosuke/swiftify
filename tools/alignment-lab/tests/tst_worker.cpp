// SPDX-License-Identifier: GPL-3.0-or-later

#include "Lab.h"

#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class WorkerTest : public QObject
{
    Q_OBJECT
private slots:
    void alignsExportsAndCancels()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
            || (qEnvironmentVariableIsEmpty("ALIGNMENT_LAB_PYTHON")
                && QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty())) {
            QSKIP("FFmpeg and Python 3 are required for the worker test.");
        }
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        qputenv("ALIGNMENT_LAB_REGISTRY", QFile::encodeName(temp.filePath(QStringLiteral("models.json"))));
        const QString input = temp.filePath(QStringLiteral("input.wav"));
        {
            QFile file(input);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream wav(&file);
            wav.setByteOrder(QDataStream::LittleEndian);
            constexpr quint32 samples = 16'000;
            wav.writeRawData("RIFF", 4);
            wav << quint32(36 + samples * 2);
            wav.writeRawData("WAVEfmt ", 8);
            wav << quint32(16) << quint16(1) << quint16(1)
                << quint32(samples) << quint32(samples * 2) << quint16(2) << quint16(16);
            wav.writeRawData("data", 4);
            const QByteArray pcm(samples * 2, '\0');
            wav << quint32(pcm.size());
            QCOMPARE(wav.writeRawData(pcm.constData(), pcm.size()), pcm.size());
        }
        Lab lab;
        lab.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!lab.busy(), 5000);
        QVERIFY2(!lab.models().isEmpty(), qPrintable(lab.error()));
        QCOMPARE(lab.models().front().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("heuristic"));

        const auto run = [&] {
            lab.run({QStringLiteral("heuristic")}, QUrl::fromLocalFile(input),
                    QStringLiteral("A test\nAnd another"), QStringLiteral("en"),
                    QStringLiteral("cpu"), true, QUrl{});
        };
        run();
        lab.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!lab.busy(), 5000);
        QCOMPARE(lab.status(), QStringLiteral("Cancelled"));

        run();
        QTRY_VERIFY_WITH_TIMEOUT(!lab.busy(), 5000);
        QCOMPARE(lab.reports().size(), 1);
        QCOMPARE(lab.cues(0, 0).size(), 2);
        QCOMPARE(lab.cues(0, 1).size(), 4);
        const auto path = temp.filePath(QStringLiteral("out.json"));
        lab.save(0, QStringLiteral("json"), QUrl::fromLocalFile(path));
        QFile output(path);
        QVERIFY2(output.open(QIODevice::ReadOnly), qPrintable(lab.error()));
        QVERIFY(QJsonDocument::fromJson(output.readAll()).isObject());
    }
};

QTEST_GUILESS_MAIN(WorkerTest)
#include "tst_worker.moc"
