#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QSaveFile;
class QCryptographicHash;
class QTimer;

namespace atlas {

enum class DownloadState {
    Queued,
    Downloading,
    Verifying,
    Completed,
    Failed,
    Cancelled
};

enum class ChecksumAlgorithm {
    None,
    Sha1,
    Sha256,
    Sha512
};

struct DownloadRequest {
    QString id;
    QString title;
    QUrl url;
    // Допустимый резервный HTTPS-источник того же артефакта. Он используется
    // при первой сетевой ошибке основного URL; хеш остаётся обязательным.
    QUrl fallbackUrl;
    QString destinationPath;
    QString checksum;
    ChecksumAlgorithm checksumAlgorithm = ChecksumAlgorithm::None;
    qint64 expectedSize = -1;
    int retries = 0;
};

struct DownloadTask {
    DownloadRequest request;
    DownloadState state = DownloadState::Queued;
    qint64 bytesReceived = 0;
    qint64 bytesTotal = -1;
    QString error;
};

class DownloadManager final : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);

    QString enqueue(DownloadRequest request);
    void start();
    void cancel(const QString &id);
    void cancelAll();
    void clearFinished();
    void setMaximumConcurrentDownloads(int maximum);
    int maximumConcurrentDownloads() const;
    void setInactivityTimeoutSeconds(int seconds);
    int inactivityTimeoutSeconds() const;
    // Компактный снимок для интерфейса: активные операции всегда попадают в
    // результат, а длинная очередь не превращается в тысячи QListWidgetItem.
    QList<DownloadTask> tasksForDisplay(int maximumItems, int *activeCount = nullptr,
                                        int *totalCount = nullptr) const;
    bool hasActiveDownloads() const;

signals:
    void taskAdded(const atlas::DownloadTask &task);
    void taskChanged(const atlas::DownloadTask &task);
    void queueIdle();

private slots:
    void startNext();

private:
    struct ActiveDownload {
        QNetworkReply *reply = nullptr;
        QSaveFile *output = nullptr;
        QCryptographicHash *hash = nullptr;
        bool cancelRequested = false;
        bool retryableFailure = false;
        QString forcedFailure;
        QString destinationKey;
        QTimer *inactivityTimer = nullptr;
        qint64 lastActivityBytes = 0;
        qint64 lastProgressEmitMs = 0;
    };

    void onReadyRead(const QString &id);
    void onDownloadProgress(const QString &id, qint64 received, qint64 total);
    void onFinished(const QString &id);
    void updateTask(const QString &id);
    void failActive(const QString &id, const QString &message, bool retryable = false);
    void resetInactivityTimer(const QString &id);
    bool verifyAndCommit(const QString &id, ActiveDownload *active, QString *error);
    bool retryableNetworkError(QNetworkReply::NetworkError networkError) const;
    void retryIfAllowed(DownloadTask *task, QNetworkReply::NetworkError networkError);
    void disposeActive(const QString &id);
    DownloadTask *taskById(const QString &id);
    const DownloadTask *taskById(const QString &id) const;

    QNetworkAccessManager m_network;
    QQueue<QString> m_queue;
    QHash<QString, DownloadTask> m_tasks;
    QHash<QString, ActiveDownload *> m_activeDownloads;
    QSet<QString> m_activeDestinations;
    int m_maximumConcurrentDownloads = 8;
    int m_inactivityTimeoutMs = 90000;
    bool m_running = false;
    bool m_cancelAllRemaining = false;
};

QString downloadStateToString(DownloadState state);
QString checksumAlgorithmToString(ChecksumAlgorithm algorithm);

} // namespace atlas

Q_DECLARE_METATYPE(atlas::DownloadTask)
