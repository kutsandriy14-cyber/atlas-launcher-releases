#pragma once

#include "domain/types.h"
#include "services/download_manager.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QNetworkReply;

namespace atlas {

struct LoaderVersionDescriptor {
    QString version;
    bool stable = false;

    bool isValid() const { return !version.trimmed().isEmpty(); }
};

class LoaderInstallService final : public QObject
{
    Q_OBJECT
public:
    explicit LoaderInstallService(const QString &dataDirectory,
                                  DownloadManager *downloadManager,
                                  QObject *parent = nullptr);

    void refreshVersions(LoaderKind kind, const QString &minecraftVersion);
    void install(const Instance &instance, const QString &javaExecutable = QString());
    bool isInstalling() const;
    bool supportsMinecraftVersion(LoaderKind kind, const QString &minecraftVersion) const;

signals:
    void versionsReady(atlas::LoaderKind kind, const QString &minecraftVersion,
                       const QVector<atlas::LoaderVersionDescriptor> &versions);
    void versionsError(atlas::LoaderKind kind, const QString &minecraftVersion, const QString &message);
    void installStarted(const QString &instanceId, atlas::LoaderKind kind, const QString &minecraftVersion);
    void installFinished(const QString &instanceId, atlas::LoaderKind kind,
                         const QString &loaderVersion, const QString &profileId);
    void installError(const QString &instanceId, const QString &message);

private slots:
    void onNetworkReply();
    void onTaskChanged(const atlas::DownloadTask &task);

private:
    enum class RequestPurpose {
        VersionList,
        InstallVersionList,
        Profile,
        InstallerChecksum
    };

    struct InstallJob {
        Instance instance;
        QJsonObject mergedProfile;
        QString resolvedLoaderVersion;
        QString profileId;
        QString javaExecutable;
        QString installerPath;
        QString installerTaskId;
        QSet<QString> pendingTaskIds;
        bool failed = false;
    };

    bool supports(LoaderKind kind) const;
    QString apiBase(LoaderKind kind) const;
    QString loaderVersionsUrl(LoaderKind kind, const QString &minecraftVersion) const;
    QString profileUrl(LoaderKind kind, const QString &minecraftVersion, const QString &loaderVersion) const;
    QString forgeInstallerUrl(const QString &loaderVersion) const;
    QString neoForgeInstallerUrl(const QString &loaderVersion) const;
    QString installerUrl(LoaderKind kind, const QString &loaderVersion) const;
    void requestJson(const QUrl &url, RequestPurpose purpose, LoaderKind kind,
                     const QString &minecraftVersion, const QString &loaderVersion = QString());
    void parseVersionList(const QJsonDocument &document, LoaderKind kind, const QString &minecraftVersion);
    void parseMavenVersionList(const QByteArray &payload, LoaderKind kind, const QString &minecraftVersion,
                               RequestPurpose purpose);
    void beginProfileRequest(const Instance &instance, const QString &loaderVersion);
    void requestInstallerChecksum(LoaderKind kind, const QString &loaderVersion);
    void scheduleInstaller(const QString &checksum, ChecksumAlgorithm algorithm);
    void launchInstaller();
    void finishInstaller(int exitCode, QProcess::ExitStatus exitStatus);
    bool finalizeInstalledProfile(const QString &profileId, QString *error);
    void parseAndScheduleProfile(const QJsonObject &profile);
    QJsonObject mergedProfile(const QJsonObject &base, const QJsonObject &profile, QString *error) const;
    QJsonObject normalizedLibrary(const QJsonObject &library, LoaderKind kind, QString *error) const;
    void scheduleProfileLibraries();
    void finishIfComplete();
    void failInstall(const QString &message);
    bool libraryAllowedOnWindows(const QJsonObject &library) const;
    QString mavenPath(const QString &coordinate) const;
    QString gameDirectory() const;
    QString versionDirectory(const QString &version) const;
    QString libraryDestination(const QString &mavenPath) const;
    QString newTaskId(const QString &part);

    QString m_dataDirectory;
    DownloadManager *m_downloadManager = nullptr;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QProcess m_installerProcess;
    RequestPurpose m_requestPurpose = RequestPurpose::VersionList;
    LoaderKind m_requestKind = LoaderKind::Unknown;
    QString m_requestMinecraftVersion;
    QString m_requestLoaderVersion;
    QVector<LoaderVersionDescriptor> m_versions;
    InstallJob m_job;
    quint64 m_sequence = 0;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::LoaderVersionDescriptor)
Q_DECLARE_METATYPE(QVector<atlas::LoaderVersionDescriptor>)
Q_DECLARE_METATYPE(atlas::LoaderKind)
