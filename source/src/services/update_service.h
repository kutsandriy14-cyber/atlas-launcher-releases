#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSaveFile;

namespace atlas {

struct UpdateRelease
{
    QString version;
    QString notes;
    QUrl archiveUrl;
    QString archiveName;
    QUrl checksumsUrl;
    qint64 archiveSize = -1;

    bool isValid() const;
};

class UpdateService final : public QObject
{
    Q_OBJECT
public:
    explicit UpdateService(QObject *parent = nullptr);

    void checkForUpdate(const QString &repository, const QString &currentVersion);
    void downloadUpdate(const UpdateRelease &release, const QString &updatesDirectory);
    void cancelDownload();

    static bool launchCheckProcess(const QString &repository, const QString &currentVersion,
                                   const QString &installDirectory, const QString &settingsDirectory,
                                   QString *error = nullptr);
    static bool launchApplyProcess(const QString &archivePath, const QString &installDirectory,
                                   const QString &restartExecutable, QString *error = nullptr);

signals:
    void updateAvailable(const atlas::UpdateRelease &release);
    void noUpdateAvailable();
    void updateCheckError(const QString &message);
    void updateDownloadProgress(qint64 received, qint64 total);
    void updateReadyToInstall(const QString &archivePath, const atlas::UpdateRelease &release);
    void updateDownloadError(const QString &message);

private:
    void finishCheckReply(QNetworkReply *reply);
    void requestChecksums();
    void beginArchiveDownload();
    QString expectedChecksumForArchive(const QByteArray &checksumFile, const QString &archiveName) const;
    static bool isValidRepository(const QString &repository);
    static int compareVersions(const QString &left, const QString &right);
    static QUrl apiUrlForRepository(const QString &repository);
    static void configureRequest(QNetworkRequest *request);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_checksumsReply = nullptr;
    QNetworkReply *m_archiveReply = nullptr;
    QSaveFile *m_output = nullptr;
    QString m_currentVersion;
    QString m_updatesDirectory;
    UpdateRelease m_release;
    QString m_expectedChecksum;
    QCryptographicHash *m_hash = nullptr;
    bool m_cancelRequested = false;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::UpdateRelease)
