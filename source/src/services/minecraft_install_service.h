#pragma once

#include "domain/types.h"
#include "services/download_manager.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkReply;

namespace atlas {

struct MinecraftVersionDescriptor {
    QString id;
    QString type;
    QString metadataUrl;
    QString metadataSha1;
    QDateTime releaseTime;

    bool isValid() const { return !id.isEmpty() && metadataUrl.startsWith(QStringLiteral("https://")); }
};

class MinecraftInstallService final : public QObject
{
    Q_OBJECT
public:
    explicit MinecraftInstallService(const QString &dataDirectory,
                                    DownloadManager *downloadManager,
                                    QObject *parent = nullptr);

    void refreshVersions(bool includeSnapshots = false);
    QVector<MinecraftVersionDescriptor> versions() const;
    void installVanilla(const Instance &instance);
    bool isInstalling() const;
    QString gameDirectory() const;

signals:
    void versionsReady(const QVector<atlas::MinecraftVersionDescriptor> &versions);
    void versionsError(const QString &message);
    void installStarted(const QString &instanceId, const QString &version);
    void installFinished(const QString &instanceId, const QString &version);
    void installError(const QString &instanceId, const QString &message);

private slots:
    void onManifestReply();
    void onTaskChanged(const atlas::DownloadTask &task);

private:
    struct InstallJob {
        Instance instance;
        MinecraftVersionDescriptor descriptor;
        QSet<QString> pendingTaskIds;
        QString metadataTaskId;
        QString assetIndexTaskId;
        QString metadataPath;
        QString assetIndexPath;
        bool awaitingAssetIndex = false;
        bool failed = false;
    };

    void requestManifest(bool includeSnapshots);
    void parseVersionMetadata();
    void scheduleMetadata(const MinecraftVersionDescriptor &descriptor);
    void scheduleVersionFiles(const QJsonObject &metadata);
    void scheduleAssetObjects();
    void finishIfComplete();
    bool libraryAllowedOnWindows(const QJsonObject &library) const;
    void enqueueFile(const QString &taskId, const QString &title, const QUrl &url,
                     const QString &destination, const QString &sha1, qint64 size);
    QString versionDirectory(const QString &version) const;
    QString libraryDestination(const QString &mavenPath) const;
    QString assetDestination(const QString &hash) const;
    QString newTaskId(const QString &part);
    void failInstall(const QString &message);

    QString m_dataDirectory;
    DownloadManager *m_downloadManager = nullptr;
    QNetworkAccessManager m_network;
    QNetworkReply *m_manifestReply = nullptr;
    QVector<MinecraftVersionDescriptor> m_versions;
    InstallJob m_job;
    quint64 m_sequence = 0;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::MinecraftVersionDescriptor)
Q_DECLARE_METATYPE(QVector<atlas::MinecraftVersionDescriptor>)
