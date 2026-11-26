// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class Lab : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QVariantList reports READ reports NOTIFY reportsChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)

public:
    explicit Lab(QObject *parent = nullptr);
    ~Lab() override;

    QVariantList models() const { return m_models; }
    QVariantList reports() const { return m_reports; }
    bool busy() const { return m_job != Job::Idle; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void registerModel(QString id, QString profile, const QUrl &file,
                                   QString name, QString license, QString source);
    Q_INVOKABLE void removeModel(QString id);
    Q_INVOKABLE void run(QStringList ids, const QUrl &audio, QString lyrics, QString language,
                         QString device, bool benchmark, const QUrl &reference);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString readLyrics(const QUrl &url);
    Q_INVOKABLE void save(int reportIndex, QString format, const QUrl &url);
    Q_INVOKABLE QVariantList cues(int reportIndex, int level) const;

signals:
    void modelsChanged();
    void reportsChanged();
    void stateChanged();

private:
    enum class Job { Idle, List, Modify, Run };
    void start(Job job, const QStringList &arguments);
    void nextRun();
    void consume();
    void finished(int code, QProcess::ExitStatus exit);
    void interrupt(int signal);
    void fail(const QString &error);

    QProcess m_worker;
    Job m_job = Job::Idle;
    QVariantList m_models;
    QVariantList m_reports;
    QStringList m_ids;
    QStringList m_runArgs;
    QByteArray m_lyrics;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QString m_lyricsPath;
    QString m_status;
    QString m_error;
    int m_runCount = 0;
    double m_progress = 0.0;
    bool m_cancelled = false;
    bool m_gotReport = false;
};
