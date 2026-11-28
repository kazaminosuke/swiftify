// SPDX-License-Identifier: GPL-3.0-or-later

#include "Lab.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <unistd.h>
#endif

namespace {
constexpr qsizetype MaxOutput = 16 * 1024 * 1024;
constexpr qsizetype MaxLyrics = 64 * 1024;

bool sameFile(const QString &path, const QString &input)
{
    if (input.isEmpty()) {
        return false;
    }
    return QFileInfo(path).absoluteFilePath() == QFileInfo(input).absoluteFilePath()
        || (QFileInfo(path).exists() && QFileInfo(path).canonicalFilePath() == QFileInfo(input).canonicalFilePath());
}
}

Lab::Lab(QObject *parent) : QObject(parent)
{
#if defined(Q_OS_UNIX)
    m_worker.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    connect(&m_worker, &QProcess::started, this, [this] {
        if (m_job == Job::Run) {
            m_worker.write(m_lyrics);
        }
        m_worker.closeWriteChannel();
        if (m_cancelled) {
            interrupt(15);
        }
    });
    connect(&m_worker, &QProcess::readyReadStandardOutput, this, &Lab::consume);
    connect(&m_worker, &QProcess::readyReadStandardError, this, [this] {
        const auto data = m_worker.readAllStandardError();
        if (m_stderr.size() < 4096) {
            m_stderr.append(data.left(4096 - m_stderr.size()));
        }
    });
    connect(&m_worker, &QProcess::finished, this, &Lab::finished);
    connect(&m_worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_job = Job::Idle;
            fail(tr("Could not launch the Alignment Lab worker (%1).").arg(m_worker.errorString()));
        }
    });
}

Lab::~Lab()
{
    m_worker.disconnect(this);
    if (m_worker.state() != QProcess::NotRunning) {
        interrupt(9);
        m_worker.waitForFinished(500);
    }
}

void Lab::refresh()
{
    if (!busy()) {
        start(Job::List, {QStringLiteral("models")});
    }
}

void Lab::registerModel(QString id, QString profile, const QUrl &file, QString name,
                        QString license, QString source)
{
    if (busy()) {
        return;
    }
    if (!file.isLocalFile()) {
        fail(tr("Choose a local ONNX model file."));
        return;
    }
    start(Job::Modify, {QStringLiteral("register"), QStringLiteral("--id=") + id,
                        QStringLiteral("--profile=") + profile,
                        QStringLiteral("--file=") + file.toLocalFile(),
                        QStringLiteral("--name=") + name, QStringLiteral("--license=") + license,
                        QStringLiteral("--source=") + source});
}

void Lab::removeModel(QString id)
{
    if (!busy()) {
        start(Job::Modify, {QStringLiteral("remove"), QStringLiteral("--id=") + id});
    }
}

void Lab::run(QStringList ids, const QUrl &audio, QString lyrics, QString language,
              QString device, bool benchmark, const QUrl &reference)
{
    if (busy()) {
        return;
    }
    if (ids.isEmpty() || !audio.isLocalFile() || lyrics.trimmed().isEmpty()) {
        fail(tr("Choose a model, a local audio file and lyric text."));
        return;
    }
    m_lyrics = lyrics.toUtf8();
    if (m_lyrics.size() > MaxLyrics) {
        fail(tr("Lyrics exceed 64 KiB."));
        return;
    }
    m_ids = ids;
    m_runCount = ids.size();
    m_progress = 0.0;
    m_reports.clear();
    m_runArgs = {benchmark ? QStringLiteral("benchmark") : QStringLiteral("align"),
                 QStringLiteral("--events"), QStringLiteral("--audio=") + audio.toLocalFile(),
                 QStringLiteral("--lyrics=-"), QStringLiteral("--language=") + language,
                 QStringLiteral("--device=") + device};
    if (reference.isLocalFile()) {
        m_runArgs.append(QStringLiteral("--reference=") + reference.toLocalFile());
    }
    emit reportsChanged();
    nextRun();
}

void Lab::nextRun()
{
    const auto id = m_ids.takeFirst();
    auto arguments = m_runArgs;
    arguments.append(QStringLiteral("--model=") + id);
    start(Job::Run, arguments);
}

void Lab::start(Job job, const QStringList &arguments)
{
    m_job = job;
    m_cancelled = false;
    m_gotReport = false;
    m_stdout.clear();
    m_stderr.clear();
    if (job != Job::Run || m_ids.size() == m_runCount - 1) {
        m_error.clear();
    }

    QString python = qEnvironmentVariable("ALIGNMENT_LAB_PYTHON");
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    }
    if (python.isEmpty()) {
        m_job = Job::Idle;
        fail(tr("Python 3 was not found. Set ALIGNMENT_LAB_PYTHON to its executable."));
        return;
    }
    QDir runtime(QCoreApplication::applicationDirPath());
    if (!runtime.exists(QStringLiteral("alignment_lab/__main__.py"))) {
        runtime.cd(QStringLiteral(ALIGNMENT_LAB_DATA_DIR));
    }
    if (!runtime.exists(QStringLiteral("alignment_lab/__main__.py"))) {
        m_job = Job::Idle;
        fail(tr("The Alignment Lab backend files are missing."));
        return;
    }
    m_status = tr("Starting worker…");
    emit stateChanged();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ALIGNMENT_LAB_BACKGROUND"), QStringLiteral("1"));
    m_worker.setProcessEnvironment(environment);
    m_worker.setWorkingDirectory(runtime.absolutePath());
    m_worker.start(python, QStringList{QStringLiteral("-B"), QStringLiteral("-m"),
                                      QStringLiteral("alignment_lab")} + arguments);
}

void Lab::consume()
{
    const auto data = m_worker.readAllStandardOutput();
    if (m_stdout.size() + data.size() > MaxOutput) {
        fail(tr("The worker exceeded the output limit."));
        interrupt(9);
        return;
    }
    m_stdout += data;
    if (m_job != Job::Run || m_cancelled) {
        return;
    }
    for (qsizetype end = m_stdout.indexOf('\n'); end >= 0; end = m_stdout.indexOf('\n')) {
        const auto event = QJsonDocument::fromJson(m_stdout.first(end)).object();
        m_stdout.remove(0, end + 1);
        const auto type = event.value(QStringLiteral("event")).toString();
        if (type == QStringLiteral("progress")) {
            const int completed = m_runCount - m_ids.size() - 1;
            m_progress = (completed + event.value(QStringLiteral("value")).toDouble()) / m_runCount;
            m_status = event.value(QStringLiteral("stage")).toString();
            emit stateChanged();
        } else if (type == QStringLiteral("result")) {
            m_gotReport = true;
            m_reports.append(event.value(QStringLiteral("report")).toObject().toVariantMap());
            emit reportsChanged();
        } else if (type == QStringLiteral("error")) {
            fail(event.value(QStringLiteral("message")).toString());
        }
    }
}

void Lab::finished(int code, QProcess::ExitStatus exit)
{
    consume();
    const auto previous = m_job;
    if (m_cancelled) {
        m_job = Job::Idle;
        m_ids.clear();
        m_status = tr("Cancelled");
        emit stateChanged();
        return;
    }
    if (exit != QProcess::NormalExit || (code != 0 && !m_gotReport)) {
        auto detail = QString::fromUtf8(m_stderr).trimmed();
        if (!detail.isEmpty() || m_error.isEmpty()) {
            fail(detail.isEmpty() ? tr("Worker exited with code %1.").arg(code) : detail);
        }
    } else if (previous == Job::List) {
        const auto obj = QJsonDocument::fromJson(m_stdout).object();
        m_models = obj.value(QStringLiteral("models")).toVariant().toList();
        emit modelsChanged();
    }
    if (previous == Job::Run && !m_ids.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            if (!m_ids.isEmpty() && !m_cancelled) {
                nextRun();
            } else {
                m_job = Job::Idle;
                emit stateChanged();
            }
        });
        return;
    }
    m_job = Job::Idle;
    if (previous == Job::Modify && code == 0 && exit == QProcess::NormalExit) {
        refresh();
    } else {
        m_status = previous == Job::Run ? tr("Finished") : QString{};
        emit stateChanged();
    }
}

void Lab::interrupt(int signal)
{
    if (m_worker.processId() == 0) {
        return;
    }
#if defined(Q_OS_UNIX)
    if (::kill(-static_cast<pid_t>(m_worker.processId()), signal) == 0) {
        return;
    }
#endif
    if (signal == 9) {
        m_worker.kill();
    } else {
        m_worker.terminate();
    }
}

void Lab::cancel()
{
    if (m_job != Job::Run || m_cancelled) {
        return;
    }
    m_cancelled = true;
    m_ids.clear();
    m_status = tr("Cancelling…");
    emit stateChanged();
    interrupt(15);
    const auto pid = m_worker.processId();
    QTimer::singleShot(500, this, [this, pid] {
        if (m_cancelled && m_worker.processId() == pid) {
            interrupt(9);
        }
    });
}

void Lab::fail(const QString &error)
{
    m_error = error;
    emit stateChanged();
}

QString Lab::readLyrics(const QUrl &url)
{
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly)) {
        fail(tr("The lyric file could not be opened."));
        return {};
    }
    auto raw = file.read(MaxLyrics + 1);
    if (raw.size() > MaxLyrics) {
        fail(tr("Lyrics exceed 64 KiB."));
        return {};
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(raw);
    if (utf8.hasError()) {
        fail(tr("The lyric file must be UTF-8."));
        return {};
    }
    m_lyricsPath = file.fileName();
    m_error.clear();
    emit stateChanged();
    return text;
}

void Lab::save(int reportIndex, QString format, const QUrl &url)
{
    if (reportIndex < 0 || reportIndex >= m_reports.size() || !url.isLocalFile()) {
        return;
    }
    const QString path = url.toLocalFile();
    if (sameFile(path, m_lyricsPath)) {
        fail(tr("Choose a destination other than the lyric input file."));
        return;
    }
    for (const auto &argument : m_runArgs) {
        if ((argument.startsWith(QStringLiteral("--audio=")) && sameFile(path, argument.sliced(8)))
            || (argument.startsWith(QStringLiteral("--reference=")) && sameFile(path, argument.sliced(12)))) {
            fail(tr("Choose a destination other than the input or reference."));
            return;
        }
    }
    for (const auto &model : m_models) {
        if (sameFile(path, model.toMap().value(QStringLiteral("path")).toString())) {
            fail(tr("Choose a destination other than a registered model file."));
            return;
        }
    }
    const auto result = m_reports[reportIndex].toMap().value(QStringLiteral("result")).toMap();
    QByteArray data;
    if (format == QStringLiteral("json") && !result.isEmpty()) {
        data = QJsonDocument::fromVariant(result).toJson();
    } else {
        data = result.value(format).toString().toUtf8();
    }
    if (data.isEmpty()) {
        return;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
        fail(tr("The export could not be written (%1).").arg(output.errorString()));
    } else {
        m_error.clear();
        m_status = tr("Saved %1").arg(QFileInfo(output.fileName()).fileName());
        emit stateChanged();
    }
}

QVariantList Lab::cues(int reportIndex, int level) const
{
    if (reportIndex < 0 || reportIndex >= m_reports.size()) {
        return {};
    }
    auto rows = m_reports[reportIndex].toMap().value(QStringLiteral("result")).toMap()
                    .value(QStringLiteral("lines")).toList();
    for (int depth = 0; depth < level; ++depth) {
        QVariantList next;
        for (const auto &entry : rows) {
            next.append(entry.toMap().value(QStringLiteral("children")).toList());
        }
        rows = std::move(next);
    }
    return rows;
}
