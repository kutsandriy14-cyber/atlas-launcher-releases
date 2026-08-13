#include "services/download_manager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace atlas {
namespace {
constexpr auto kUserAgent = "AtlasLauncher/0.2 (Windows 7+; personal launcher)";
constexpr int kMaximumRetryAttempts = 2;
constexpr int kDefaultConcurrentDownloads = 8;
constexpr int kAbsoluteMaximumConcurrentDownloads = 16;
constexpr int kInactivityTimeoutMs = 90000;
constexpr qint64 kProgressEmitIntervalMs = 300;
constexpr int kMaximumDisplayTasks = 120;

QString normalizeChecksum(const QString &value)
{
    return value.trimmed().toLower();
}

QString destinationKey(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QCryptographicHash::Algorithm cryptoAlgorithm(ChecksumAlgorithm algorithm)
{
    switch (algorithm) {
    case ChecksumAlgorithm::Sha1: return QCryptographicHash::Sha1;
    case ChecksumAlgorithm::Sha256: return QCryptographicHash::Sha256;
    case ChecksumAlgorithm::Sha512: return QCryptographicHash::Sha512;
    case ChecksumAlgorithm::None: break;
    }
    return QCryptographicHash::Sha1;
}
}

QString checksumAlgorithmToString(ChecksumAlgorithm algorithm)
{
    switch (algorithm) {
    case ChecksumAlgorithm::None: return QStringLiteral("без проверки");
    case ChecksumAlgorithm::Sha1: return QStringLiteral("SHA-1");
    case ChecksumAlgorithm::Sha256: return QStringLiteral("SHA-256");
    case ChecksumAlgorithm::Sha512: return QStringLiteral("SHA-512");
    }
    return QStringLiteral("неизвестно");
}

QString downloadStateToString(DownloadState state)
{
    switch (state) {
    case DownloadState::Queued: return QStringLiteral("В очереди");
    case DownloadState::Downloading: return QStringLiteral("Загрузка");
    case DownloadState::Verifying: return QStringLiteral("Проверка");
    case DownloadState::Completed: return QStringLiteral("Готово");
    case DownloadState::Failed: return QStringLiteral("Ошибка");
    case DownloadState::Cancelled: return QStringLiteral("Отменено");
    }
    return QStringLiteral("Неизвестно");
}

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent), m_maximumConcurrentDownloads(kDefaultConcurrentDownloads)
{
    qRegisterMetaType<DownloadTask>("atlas::DownloadTask");
}

QString DownloadManager::enqueue(DownloadRequest request)
{
    if (request.id.trimmed().isEmpty() || m_tasks.contains(request.id)) {
        do {
            request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        } while (m_tasks.contains(request.id));
    }
    request.title = request.title.trimmed();
    if (request.title.isEmpty()) request.title = QFileInfo(request.destinationPath).fileName();

    DownloadTask task;
    task.request = std::move(request);
    task.state = DownloadState::Queued;
    const QString id = task.request.id;
    m_tasks.insert(id, task);
    m_queue.enqueue(id);
    emit taskAdded(m_tasks.value(id));
    if (m_running) startNext();
    return id;
}

void DownloadManager::start()
{
    m_running = true;
    m_cancelAllRemaining = false;
    startNext();
}

void DownloadManager::cancel(const QString &id)
{
    DownloadTask *task = taskById(id);
    if (!task) return;

    if (ActiveDownload *active = m_activeDownloads.value(id, nullptr)) {
        active->cancelRequested = true;
        if (active->reply) active->reply->abort();
        return;
    }
    if (task->state != DownloadState::Queued) return;
    m_queue.removeAll(id);
    task->state = DownloadState::Cancelled;
    task->error.clear();
    updateTask(id);
    if (m_activeDownloads.isEmpty() && m_queue.isEmpty()) emit queueIdle();
}

void DownloadManager::cancelAll()
{
    // updateTask() ниже синхронно испускает taskChanged. Получатель может
    // сообщить об ошибке установки и вызвать cancelAll() повторно; повторная
    // очистка во время обхода m_tasks инвалидировала итератор QHash.
    if (m_cancelAllRemaining || (m_activeDownloads.isEmpty() && m_queue.isEmpty())) return;
    m_cancelAllRemaining = true;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->state == DownloadState::Queued) {
            it->state = DownloadState::Cancelled;
            it->error.clear();
            updateTask(it->request.id);
        }
    }
    m_queue.clear();
    // abort() может синхронно вызвать finished и удалить ActiveDownload из
    // m_activeDownloads. Поэтому не обходим QHash итератором или снапшотом
    // указателей: работаем по стабильному списку идентификаторов и каждый раз
    // заново проверяем, существует ли запись.
    const QStringList activeIds = m_activeDownloads.keys();
    for (const QString &id : activeIds) {
        ActiveDownload *active = m_activeDownloads.value(id, nullptr);
        if (!active) continue;
        active->cancelRequested = true;
        if (active->reply) active->reply->abort();
    }
    if (m_activeDownloads.isEmpty()) emit queueIdle();
}

void DownloadManager::clearFinished()
{
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (it->state == DownloadState::Completed || it->state == DownloadState::Cancelled || it->state == DownloadState::Failed) {
            it = m_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

void DownloadManager::setMaximumConcurrentDownloads(int maximum)
{
    m_maximumConcurrentDownloads = qBound(1, maximum, kAbsoluteMaximumConcurrentDownloads);
    if (m_running) startNext();
}

int DownloadManager::maximumConcurrentDownloads() const
{
    return m_maximumConcurrentDownloads;
}

void DownloadManager::setInactivityTimeoutSeconds(int seconds)
{
    m_inactivityTimeoutMs = qBound(15, seconds, 600) * 1000;
    for (ActiveDownload *active : std::as_const(m_activeDownloads)) {
        if (active && active->inactivityTimer) {
            active->inactivityTimer->setInterval(m_inactivityTimeoutMs);
            if (!active->cancelRequested) active->inactivityTimer->start();
        }
    }
}

int DownloadManager::inactivityTimeoutSeconds() const
{
    return m_inactivityTimeoutMs / 1000;
}

QList<DownloadTask> DownloadManager::tasksForDisplay(int maximumItems, int *activeCount,
                                                       int *totalCount) const
{
    const int maximum = qBound(1, maximumItems, kMaximumDisplayTasks);
    if (activeCount) *activeCount = 0;
    if (totalCount) *totalCount = m_tasks.size();

    QList<DownloadTask> result;
    result.reserve(qMin(maximum, m_tasks.size()));
    // Сначала идут действительно работающие задачи: они остаются видимыми даже
    // если очередь Minecraft содержит несколько тысяч ожидающих файлов.
    for (const DownloadTask &task : m_tasks) {
        const bool active = task.state == DownloadState::Downloading || task.state == DownloadState::Verifying;
        if (active && activeCount) ++*activeCount;
        if (active && result.size() < maximum) result.append(task);
    }
    // Затем — ошибки и отменённые задачи, чтобы пользователь видел проблему.
    for (const DownloadTask &task : m_tasks) {
        if (result.size() >= maximum) break;
        if (task.state == DownloadState::Failed || task.state == DownloadState::Cancelled) result.append(task);
    }
    // Оставшиеся строки показывают начало очереди, но не создают тысячи виджетов.
    for (const DownloadTask &task : m_tasks) {
        if (result.size() >= maximum) break;
        if (task.state == DownloadState::Queued) result.append(task);
    }
    // Завершённые строки полезны только когда установки уже нет; они не должны
    // вытеснять активные файлы в процессе скачивания.
    if (result.isEmpty()) {
        for (const DownloadTask &task : m_tasks) {
            if (result.size() >= maximum) break;
            if (task.state == DownloadState::Completed) result.append(task);
        }
    }
    return result;
}

bool DownloadManager::hasActiveDownloads() const
{
    return !m_activeDownloads.isEmpty() || !m_queue.isEmpty();
}

void DownloadManager::startNext()
{
    if (!m_running) return;

    while (m_activeDownloads.size() < m_maximumConcurrentDownloads && !m_queue.isEmpty()) {
        const int candidates = m_queue.size();
        bool started = false;
        for (int candidate = 0; candidate < candidates; ++candidate) {
            const QString id = m_queue.dequeue();
            DownloadTask *task = taskById(id);
            if (!task || task->state != DownloadState::Queued) continue;
            if (!task->request.url.isValid() || task->request.url.scheme().toLower() != QStringLiteral("https")) {
                task->state = DownloadState::Failed;
                task->error = QStringLiteral("Допускаются только корректные HTTPS-адреса.");
                updateTask(id);
                continue;
            }
            if (task->request.destinationPath.trimmed().isEmpty()) {
                task->state = DownloadState::Failed;
                task->error = QStringLiteral("Не указан целевой путь загрузки.");
                updateTask(id);
                continue;
            }

            const QString outputKey = destinationKey(task->request.destinationPath);
            if (m_activeDestinations.contains(outputKey)) {
                m_queue.enqueue(id);
                continue;
            }
            const QFileInfo targetInfo(task->request.destinationPath);
            if (!QDir().mkpath(targetInfo.absolutePath())) {
                task->state = DownloadState::Failed;
                task->error = QStringLiteral("Не удалось создать папку назначения.");
                updateTask(id);
                continue;
            }

            auto *active = new ActiveDownload;
            active->destinationKey = outputKey;
            active->output = new QSaveFile(task->request.destinationPath, this);
            if (!active->output->open(QIODevice::WriteOnly)) {
                task->state = DownloadState::Failed;
                task->error = QStringLiteral("Не удалось открыть временный файл: %1").arg(active->output->errorString());
                delete active->output;
                delete active;
                updateTask(id);
                continue;
            }
            if (task->request.checksumAlgorithm != ChecksumAlgorithm::None) {
                active->hash = new QCryptographicHash(cryptoAlgorithm(task->request.checksumAlgorithm));
            }

            task->state = DownloadState::Downloading;
            task->bytesReceived = 0;
            task->bytesTotal = task->request.expectedSize;
            task->error.clear();
            m_activeDestinations.insert(outputKey);
            m_activeDownloads.insert(id, active);
            updateTask(id);

            QNetworkRequest request(task->request.url);
            request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setTransferTimeout(m_inactivityTimeoutMs);
            active->reply = m_network.get(request);
            active->inactivityTimer = new QTimer;
            active->inactivityTimer->setSingleShot(true);
            active->inactivityTimer->setInterval(m_inactivityTimeoutMs);
            connect(active->inactivityTimer, &QTimer::timeout, this, [this, id]() {
                failActive(id, QStringLiteral("Нет передачи данных более %1 секунд; запрос будет повторён.")
                                   .arg(m_inactivityTimeoutMs / 1000), true);
            });
            connect(active->reply, &QNetworkReply::readyRead, this, [this, id]() { onReadyRead(id); });
            connect(active->reply, &QNetworkReply::downloadProgress, this,
                    [this, id](qint64 received, qint64 total) { onDownloadProgress(id, received, total); });
            connect(active->reply, &QNetworkReply::finished, this, [this, id]() { onFinished(id); });
            resetInactivityTimer(id);
            started = true;
            break;
        }
        if (!started) break;
    }

    if (m_activeDownloads.isEmpty() && m_queue.isEmpty()) emit queueIdle();
}

void DownloadManager::onReadyRead(const QString &id)
{
    ActiveDownload *active = m_activeDownloads.value(id, nullptr);
    if (!active || !active->reply || !active->output || !active->reply->isOpen()) return;
    const QByteArray data = active->reply->readAll();
    if (data.isEmpty()) return;
    if (active->output->write(data) != data.size()) {
        failActive(id, QStringLiteral("Ошибка записи временного файла: %1").arg(active->output->errorString()));
        return;
    }
    if (active->hash) active->hash->addData(data);
    resetInactivityTimer(id);
}

void DownloadManager::onDownloadProgress(const QString &id, qint64 received, qint64 total)
{
    DownloadTask *task = taskById(id);
    if (!task) return;
    task->bytesReceived = received;
    task->bytesTotal = total >= 0 ? total : task->request.expectedSize;
    ActiveDownload *active = m_activeDownloads.value(id, nullptr);
    if (active && received > active->lastActivityBytes) {
        active->lastActivityBytes = received;
        resetInactivityTimer(id);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool isComplete = total > 0 && received >= total;
    // downloadProgress приходит очень часто. Без дросселирования 1 файл способен
    // запустить сотни пересборок интерфейса в секунду.
    if (!active || isComplete || now - active->lastProgressEmitMs >= kProgressEmitIntervalMs) {
        if (active) active->lastProgressEmitMs = now;
        updateTask(id);
    }
}

void DownloadManager::onFinished(const QString &id)
{
    ActiveDownload *active = m_activeDownloads.value(id, nullptr);
    DownloadTask *task = taskById(id);
    if (!active || !task) {
        disposeActive(id);
        startNext();
        return;
    }

    const QNetworkReply::NetworkError networkError = active->reply ? active->reply->error() : QNetworkReply::UnknownNetworkError;
    const QString networkErrorText = active->reply ? active->reply->errorString() : QStringLiteral("Потерян объект сетевого запроса.");
    onReadyRead(id);
    task->bytesReceived = active->output ? active->output->pos() : task->bytesReceived;

    if (active->cancelRequested) {
        task->state = DownloadState::Cancelled;
        task->error.clear();
        if (active->output) active->output->cancelWriting();
    } else if (!active->forcedFailure.isEmpty()) {
        task->state = DownloadState::Failed;
        task->error = active->forcedFailure;
        if (active->output) active->output->cancelWriting();
        if (active->retryableFailure) retryIfAllowed(task, QNetworkReply::TimeoutError);
    } else if (networkError != QNetworkReply::NoError) {
        task->state = DownloadState::Failed;
        task->error = QStringLiteral("Ошибка сети: %1").arg(networkErrorText);
        if (active->output) active->output->cancelWriting();
        retryIfAllowed(task, networkError);
    } else {
        task->state = DownloadState::Verifying;
        updateTask(id);
        QString error;
        if (verifyAndCommit(id, active, &error)) {
            task->state = DownloadState::Completed;
            task->error.clear();
        } else {
            task->state = DownloadState::Failed;
            task->error = error;
        }
    }

    updateTask(id);
    disposeActive(id);
    startNext();
}

void DownloadManager::updateTask(const QString &id)
{
    const DownloadTask *task = taskById(id);
    if (task) emit taskChanged(*task);
}

void DownloadManager::failActive(const QString &id, const QString &message, bool retryable)
{
    ActiveDownload *active = m_activeDownloads.value(id, nullptr);
    if (!active || !active->forcedFailure.isEmpty()) return;
    active->forcedFailure = message;
    active->retryableFailure = retryable;
    if (active->reply) active->reply->abort();
}

void DownloadManager::resetInactivityTimer(const QString &id)
{
    ActiveDownload *active = m_activeDownloads.value(id, nullptr);
    if (active && active->inactivityTimer && !active->cancelRequested) {
        active->inactivityTimer->start();
    }
}

bool DownloadManager::retryableNetworkError(QNetworkReply::NetworkError networkError) const
{
    switch (networkError) {
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::ProxyTimeoutError:
        return true;
    default:
        break;
    }
    return false;
}

void DownloadManager::retryIfAllowed(DownloadTask *task, QNetworkReply::NetworkError networkError)
{
    if (!task || m_cancelAllRemaining || !retryableNetworkError(networkError)) return;

    const QUrl fallback = task->request.fallbackUrl;
    if (fallback.isValid() && fallback.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
        task->request.url = fallback;
        task->request.fallbackUrl = QUrl();
        task->request.retries = 0;
        task->state = DownloadState::Queued;
        task->error = QStringLiteral("Основной источник недоступен; используется резервный проверенный источник...");
        m_queue.enqueue(task->request.id);
        return;
    }

    if (task->request.retries >= kMaximumRetryAttempts) return;
    task->request.retries += 1;
    task->state = DownloadState::Queued;
    task->error = QStringLiteral("Повторная попытка %1 из %2...")
                      .arg(task->request.retries).arg(kMaximumRetryAttempts);
    m_queue.enqueue(task->request.id);
}

bool DownloadManager::verifyAndCommit(const QString &id, ActiveDownload *active, QString *error)
{
    DownloadTask *task = taskById(id);
    if (!task || !active || !active->output) {
        if (error) *error = QStringLiteral("Внутренняя ошибка задачи загрузки.");
        return false;
    }
    if (task->request.expectedSize >= 0 && task->bytesReceived != task->request.expectedSize) {
        active->output->cancelWriting();
        if (error) *error = QStringLiteral("Размер файла не совпал: ожидалось %1 байт, получено %2.")
            .arg(task->request.expectedSize).arg(task->bytesReceived);
        return false;
    }
    const QString expected = normalizeChecksum(task->request.checksum);
    if (task->request.checksumAlgorithm != ChecksumAlgorithm::None && expected.isEmpty()) {
        active->output->cancelWriting();
        if (error) *error = QStringLiteral("Не указана контрольная сумма для %1.")
            .arg(checksumAlgorithmToString(task->request.checksumAlgorithm));
        return false;
    }
    if (!expected.isEmpty()) {
        if (!active->hash) {
            active->output->cancelWriting();
            if (error) *error = QStringLiteral("Не создан хеш для проверки %1.")
                .arg(checksumAlgorithmToString(task->request.checksumAlgorithm));
            return false;
        }
        const QString actual = QString::fromLatin1(active->hash->result().toHex());
        if (actual != expected) {
            active->output->cancelWriting();
            if (error) *error = QStringLiteral("%1 не совпал: ожидалось %2, получено %3.")
                .arg(checksumAlgorithmToString(task->request.checksumAlgorithm), expected, actual);
            return false;
        }
    }
    if (!active->output->commit()) {
        if (error) *error = QStringLiteral("Не удалось атомарно сохранить файл: %1").arg(active->output->errorString());
        return false;
    }
    return true;
}

void DownloadManager::disposeActive(const QString &id)
{
    ActiveDownload *active = m_activeDownloads.take(id);
    if (!active) return;
    m_activeDestinations.remove(active->destinationKey);
    if (active->inactivityTimer) {
        active->inactivityTimer->stop();
        delete active->inactivityTimer;
    }
    if (active->reply) active->reply->deleteLater();
    delete active->output;
    delete active->hash;
    delete active;
}

DownloadTask *DownloadManager::taskById(const QString &id)
{
    auto it = m_tasks.find(id);
    return it == m_tasks.end() ? nullptr : &it.value();
}

const DownloadTask *DownloadManager::taskById(const QString &id) const
{
    const auto it = m_tasks.constFind(id);
    return it == m_tasks.cend() ? nullptr : &it.value();
}

} // namespace atlas
