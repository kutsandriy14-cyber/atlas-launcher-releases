#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QUrl>

#include "services/download_manager.h"

class QNetworkAccessManager;
class QNetworkReply;
namespace atlas {

struct JavaRuntimeInfo {
    int major = 0;
    QString javawPath;
    QString runtimeDirectory;
    QUrl packageUrl;
    QString archiveName;
    QString checksumSha256;
    qint64 archiveSize = -1;
    bool isValid() const;
};

struct JavaExecutableInfo {
    QString executablePath;
    int major = 0;
    QString versionText;

    bool isValid() const { return major > 0 && !executablePath.isEmpty(); }
};

class JavaRuntimeService final : public QObject {
    Q_OBJECT
public:
    JavaRuntimeService(const QString &dataDirectory, DownloadManager *downloads, QObject *parent = nullptr);

    QString runtimeRoot() const;
    JavaRuntimeInfo installedRuntime(int major) const;
    bool isRuntimeReady(int major) const;
    void ensureRuntime(int major);
    void setExtractorPath(const QString &extractorPath);

    static int recommendedJavaMajor(int minecraftJavaMajor);
    static JavaExecutableInfo inspectExecutable(const QString &javaPath, QString *error = nullptr);
    static bool isCompatibleMajor(int selectedMajor, int requiredMajor);

signals:
    void runtimeQueryStarted(int major);
    void runtimeDownloadQueued(const atlas::JavaRuntimeInfo &info);
    void runtimeInstallStarted(int major);
    void runtimeReady(const atlas::JavaRuntimeInfo &info);
    void runtimeError(int major, const QString &message);

private slots:
    void onRuntimeManifestFinished();
    void onDownloadTaskChanged(const atlas::DownloadTask &task);
    void onExtractorFinished(int exitCode, QProcess::ExitStatus status);

private:
    JavaRuntimeInfo parseRuntimeInfo(int major, const QByteArray &payload, QString *error) const;
    QString archivePathFor(int major, const QString &archiveName) const;
    QString stagingDirectoryFor(int major) const;
    QString finalDirectoryFor(int major) const;
    bool activateExtractedRuntime(int major, QString *error);
    void fail(int major, const QString &message);

    QString m_dataDirectory;
    DownloadManager *m_downloads = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_queryReply = nullptr;
    QProcess *m_extractor = nullptr;
    QString m_extractorPath;
    int m_pendingMajor = 0;
    QString m_pendingTaskId;
    JavaRuntimeInfo m_pendingRuntime;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::JavaRuntimeInfo)
Q_DECLARE_METATYPE(atlas::JavaExecutableInfo)
