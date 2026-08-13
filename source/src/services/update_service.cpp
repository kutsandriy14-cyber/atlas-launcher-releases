#include "services/update_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

namespace atlas {
namespace {
constexpr auto kGitHubApiVersion = "2026-03-10";
constexpr auto kUpdateUserAgent = "AtlasLauncher-Updater/0.3";
constexpr int kRequestTimeoutMs = 60000;

QString normalizedVersion(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) value.remove(0, 1);
    return value;
}

QString updaterExecutablePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AtlasUpdater.exe"));
}
}

bool UpdateRelease::isValid() const
{
    return !version.isEmpty() && archiveUrl.isValid() && !archiveName.isEmpty() && checksumsUrl.isValid();
}

UpdateService::UpdateService(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    qRegisterMetaType<UpdateRelease>("atlas::UpdateRelease");
}

void UpdateService::checkForUpdate(const QString &repository, const QString &currentVersion)
{
    if (m_checkReply) {
        m_checkReply->abort();
        m_checkReply->deleteLater();
        m_checkReply = nullptr;
    }
    if (!isValidRepository(repository)) {
        emit updateCheckError(QStringLiteral("Адрес GitHub должен быть записан как owner/repository."));
        return;
    }

    m_currentVersion = normalizedVersion(currentVersion);
    QNetworkRequest request(apiUrlForRepository(repository.trimmed()));
    configureRequest(&request);
    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, [this, reply = m_checkReply]() {
        finishCheckReply(reply);
    });
}

void UpdateService::downloadUpdate(const UpdateRelease &release, const QString &updatesDirectory)
{
    cancelDownload();
    if (!release.isValid()) {
        emit updateDownloadError(QStringLiteral("Получены неполные данные об обновлении."));
        return;
    }
    if (updatesDirectory.trimmed().isEmpty() || !QDir().mkpath(updatesDirectory)) {
        emit updateDownloadError(QStringLiteral("Не удалось создать папку обновлений."));
        return;
    }

    m_release = release;
    m_updatesDirectory = QDir::cleanPath(updatesDirectory);
    m_expectedChecksum.clear();
    m_cancelRequested = false;
    QNetworkRequest request(release.checksumsUrl);
    configureRequest(&request);
    m_checksumsReply = m_network->get(request);
    connect(m_checksumsReply, &QNetworkReply::finished, this, [this]() { requestChecksums(); });
}

void UpdateService::cancelDownload()
{
    m_cancelRequested = true;
    if (m_checksumsReply) m_checksumsReply->abort();
    if (m_archiveReply) m_archiveReply->abort();
    if (m_output) m_output->cancelWriting();
}

bool UpdateService::launchCheckProcess(const QString &repository, const QString &currentVersion,
                                       const QString &installDirectory, const QString &settingsDirectory,
                                       QString *error)
{
    const QString executable = updaterExecutablePath();
    if (!QFileInfo::exists(executable)) {
        if (error) *error = QStringLiteral("Не найден AtlasUpdater.exe рядом с лаунчером.");
        return false;
    }
    QStringList arguments;
    arguments << QStringLiteral("--check")
              << QStringLiteral("--repo") << repository
              << QStringLiteral("--current-version") << normalizedVersion(currentVersion)
              << QStringLiteral("--install-dir") << QDir::cleanPath(installDirectory)
              << QStringLiteral("--settings-dir") << QDir::cleanPath(settingsDirectory)
              << QStringLiteral("--parent-pid") << QString::number(QCoreApplication::applicationPid());
    if (!QProcess::startDetached(executable, arguments, QCoreApplication::applicationDirPath())) {
        if (error) *error = QStringLiteral("Не удалось запустить AtlasUpdater.exe.");
        return false;
    }
    return true;
}

bool UpdateService::launchApplyProcess(const QString &archivePath, const QString &installDirectory,
                                       const QString &restartExecutable, QString *error)
{
    const QString executable = updaterExecutablePath();
    if (!QFileInfo::exists(executable)) {
        if (error) *error = QStringLiteral("Не найден AtlasUpdater.exe рядом с лаунчером.");
        return false;
    }
    QStringList arguments;
    arguments << QStringLiteral("--apply")
              << QStringLiteral("--archive") << QDir::cleanPath(archivePath)
              << QStringLiteral("--install-dir") << QDir::cleanPath(installDirectory)
              << QStringLiteral("--restart") << QDir::cleanPath(restartExecutable);
    if (!QProcess::startDetached(executable, arguments, QCoreApplication::applicationDirPath())) {
        if (error) *error = QStringLiteral("Не удалось запустить процесс установки обновления.");
        return false;
    }
    return true;
}

void UpdateService::finishCheckReply(QNetworkReply *reply)
{
    if (reply != m_checkReply) {
        if (reply) reply->deleteLater();
        return;
    }
    m_checkReply = nullptr;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray response = reply->readAll();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError) {
        emit updateCheckError(QStringLiteral("GitHub Releases недоступен: %1").arg(networkErrorText));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit updateCheckError(QStringLiteral("GitHub вернул некорректные данные о релизе."));
        return;
    }
    const QJsonObject root = document.object();
    const QString releaseVersion = normalizedVersion(root.value(QStringLiteral("tag_name")).toString());
    if (releaseVersion.isEmpty()) {
        emit updateCheckError(QStringLiteral("У последнего GitHub Release нет корректного тега версии."));
        return;
    }
    if (compareVersions(releaseVersion, m_currentVersion) <= 0) {
        emit noUpdateAvailable();
        return;
    }

    UpdateRelease release;
    release.version = releaseVersion;
    release.notes = root.value(QStringLiteral("body")).toString().trimmed();
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &item : assets) {
        const QJsonObject asset = item.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (name == QStringLiteral("SHA256SUMS.txt")) {
            release.checksumsUrl = url;
        } else if (name.endsWith(QStringLiteral("-win64-portable.zip"), Qt::CaseInsensitive)) {
            release.archiveName = name;
            release.archiveUrl = url;
            release.archiveSize = qint64(asset.value(QStringLiteral("size")).toDouble(-1));
        }
    }
    if (!release.isValid()) {
        emit updateCheckError(QStringLiteral("В GitHub Release отсутствует portable ZIP или SHA256SUMS.txt."));
        return;
    }
    emit updateAvailable(release);
}

void UpdateService::requestChecksums()
{
    QNetworkReply *reply = m_checksumsReply;
    m_checksumsReply = nullptr;
    if (!reply) return;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray content = reply->readAll();
    reply->deleteLater();
    if (m_cancelRequested) return;
    if (networkError != QNetworkReply::NoError) {
        emit updateDownloadError(QStringLiteral("Не удалось получить SHA256SUMS.txt: %1").arg(networkErrorText));
        return;
    }
    m_expectedChecksum = expectedChecksumForArchive(content, m_release.archiveName);
    if (m_expectedChecksum.isEmpty()) {
        emit updateDownloadError(QStringLiteral("В SHA256SUMS.txt нет контрольной суммы архива %1.").arg(m_release.archiveName));
        return;
    }
    beginArchiveDownload();
}

void UpdateService::beginArchiveDownload()
{
    const QString outputPath = QDir(m_updatesDirectory).filePath(m_release.archiveName);
    if (outputPath.isEmpty()) {
        emit updateDownloadError(QStringLiteral("Внутренняя ошибка пути архива обновления."));
        return;
    }
    m_output = new QSaveFile(outputPath);
    if (!m_output->open(QIODevice::WriteOnly)) {
        emit updateDownloadError(QStringLiteral("Не удалось открыть временный архив: %1").arg(m_output->errorString()));
        delete m_output;
        m_output = nullptr;
        return;
    }
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    QNetworkRequest request(m_release.archiveUrl);
    configureRequest(&request);
    m_archiveReply = m_network->get(request);
    connect(m_archiveReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_archiveReply || !m_output) return;
        const QByteArray data = m_archiveReply->readAll();
        if (data.isEmpty()) return;
        if (m_output->write(data) != data.size()) {
            m_cancelRequested = true;
            m_archiveReply->abort();
            emit updateDownloadError(QStringLiteral("Не удалось записать архив обновления: %1").arg(m_output->errorString()));
            return;
        }
        m_hash->addData(data);
    });
    connect(m_archiveReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit updateDownloadProgress(received, total);
    });
    connect(m_archiveReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_archiveReply;
        m_archiveReply = nullptr;
        if (!reply) return;
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorText = reply->errorString();
        if (reply->bytesAvailable() > 0 && m_output) {
            const QByteArray data = reply->readAll();
            if (m_output->write(data) == data.size() && m_hash) m_hash->addData(data);
        }
        reply->deleteLater();
        if (m_cancelRequested || networkError != QNetworkReply::NoError) {
            if (m_output) m_output->cancelWriting();
            if (!m_cancelRequested) emit updateDownloadError(QStringLiteral("Не удалось скачать обновление: %1").arg(errorText));
        } else {
            const QString actual = m_hash ? QString::fromLatin1(m_hash->result().toHex()) : QString();
            if (actual != m_expectedChecksum) {
                if (m_output) m_output->cancelWriting();
                emit updateDownloadError(QStringLiteral("SHA-256 архива не совпал. Обновление отменено."));
            } else if (!m_output || !m_output->commit()) {
                emit updateDownloadError(QStringLiteral("Не удалось атомарно сохранить ZIP обновления."));
            } else {
                const QString archivePath = m_output->fileName();
                delete m_output;
                m_output = nullptr;
                delete m_hash;
                m_hash = nullptr;
                emit updateReadyToInstall(archivePath, m_release);
                return;
            }
        }
        delete m_output;
        m_output = nullptr;
        delete m_hash;
        m_hash = nullptr;
    });
}

QString UpdateService::expectedChecksumForArchive(const QByteArray &checksumFile, const QString &archiveName) const
{
    const QList<QByteArray> lines = checksumFile.split('\n');
    const QRegularExpression pattern(QStringLiteral("^\\s*([A-Fa-f0-9]{64})\\s+\\*?(.+?)\\s*$"));
    for (const QByteArray &line : lines) {
        const QRegularExpressionMatch match = pattern.match(QString::fromUtf8(line));
        if (match.hasMatch() && match.captured(2).trimmed() == archiveName) {
            return match.captured(1).toLower();
        }
    }
    return QString();
}

bool UpdateService::isValidRepository(const QString &repository)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"));
    return pattern.match(repository.trimmed()).hasMatch();
}

int UpdateService::compareVersions(const QString &left, const QString &right)
{
    const QStringList leftParts = normalizedVersion(left).split(QLatin1Char('.'));
    const QStringList rightParts = normalizedVersion(right).split(QLatin1Char('.'));
    const int count = qMax(leftParts.size(), rightParts.size());
    for (int index = 0; index < count; ++index) {
        bool leftOk = false;
        bool rightOk = false;
        const int l = index < leftParts.size() ? leftParts.at(index).toInt(&leftOk) : 0;
        const int r = index < rightParts.size() ? rightParts.at(index).toInt(&rightOk) : 0;
        if (!leftOk || !rightOk) return QString::compare(normalizedVersion(left), normalizedVersion(right), Qt::CaseInsensitive);
        if (l != r) return l < r ? -1 : 1;
    }
    return 0;
}

QUrl UpdateService::apiUrlForRepository(const QString &repository)
{
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository));
}

void UpdateService::configureRequest(QNetworkRequest *request)
{
    if (!request) return;
    request->setRawHeader("Accept", "application/vnd.github+json");
    request->setRawHeader("User-Agent", kUpdateUserAgent);
    request->setRawHeader("X-GitHub-Api-Version", kGitHubApiVersion);
    request->setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request->setTransferTimeout(kRequestTimeoutMs);
}

} // namespace atlas
