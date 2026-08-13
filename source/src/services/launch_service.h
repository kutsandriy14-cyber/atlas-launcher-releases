#pragma once

#include "domain/types.h"
#include "services/auth_service.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace atlas {

struct LaunchOptions {
    Instance instance;
    AccountSession account;
    QString javaExecutable;
    int minMemoryMiB = 1024;
    int maxMemoryMiB = 4096;
    QStringList extraJvmArguments;
    QStringList extraGameArguments;
};

class LaunchService final : public QObject
{
    Q_OBJECT
public:
    explicit LaunchService(const QString &dataDirectory, QObject *parent = nullptr);

    bool isRunning() const;
    QString runningInstanceId() const;
    void launch(const atlas::LaunchOptions &options);
    void stop();

signals:
    void launchStarted(const QString &instanceId, qint64 processId);
    void launchExited(const QString &instanceId, int exitCode, bool crashed);
    void launchError(const QString &instanceId, const QString &message);
    void logLine(const QString &instanceId, const QString &line);

private slots:
    void onProcessStarted();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    bool prepareLaunch(const atlas::LaunchOptions &options, QString *javaPath, QStringList *arguments, QString *error);
    bool loadVersionMetadata(const atlas::Instance &instance, QJsonObject *metadata, QString *error) const;
    bool prepareNatives(const atlas::Instance &instance, const QJsonObject &metadata, QString *nativesDirectory, QString *error) const;
    QStringList classpathFor(const QJsonObject &metadata, QString *error) const;
    QStringList argumentsFor(const atlas::LaunchOptions &options, const QJsonObject &metadata,
                             const QString &nativesDirectory, const QStringList &classpath, QString *error) const;
    bool libraryAllowedOnWindows(const QJsonObject &library) const;
    QString nativeArchiveFor(const QJsonObject &library) const;
    QString resolveJava(const atlas::LaunchOptions &options) const;
    QString versionDirectory(const QString &version) const;
    QString gameDirectory() const;
    QString replaceVariables(const QString &value, const QHash<QString, QString> &variables) const;
    void emitProcessOutput(const QByteArray &data);

    QString m_dataDirectory;
    QProcess *m_process = nullptr;
    QString m_runningInstanceId;
    QByteArray m_outputBuffer;
};

} // namespace atlas
