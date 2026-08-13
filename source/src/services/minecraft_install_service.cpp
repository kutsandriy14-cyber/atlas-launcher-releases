#include "services/minecraft_install_service.h"

#include "infrastructure/logger.h"
#include "services/download_manager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace atlas {
namespace {

constexpr auto kVersionManifestUrl = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json";

QString mavenPath(const QString &coordinate)
{
    const QStringList parts = coordinate.split(QLatin1Char(':'));
    if (parts.size() < 3) return {};
    QString group = parts.at(0);
    group.replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString artifact = parts.at(1);
    QString version = parts.at(2);
    QString classifier;
    QString extension = QStringLiteral("jar");
    const int at = version.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        extension = version.mid(at + 1);
        version = version.left(at);
    }
    if (parts.size() >= 4) classifier = parts.at(3);
    if (group.isEmpty() || artifact.isEmpty() || version.isEmpty() || extension.isEmpty()) return {};
    QString fileName = artifact + QLatin1Char('-') + version;
    if (!classifier.isEmpty()) fileName += QLatin1Char('-') + classifier;
    fileName += QLatin1Char('.') + extension;
    return group + QLatin1Char('/') + artifact + QLatin1Char('/') + version + QLatin1Char('/') + fileName;
}

QUrl secureMojangUrl(const QString &rawUrl)
{
    QUrl url(rawUrl.trimmed());
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0) return url;

    // Старые official JSON иногда ссылаются на публичный Mojang S3 по HTTP.
    // Те же хосты поддерживают HTTPS, поэтому загрузчик не ослабляет правило
    // «только HTTPS» ради legacy-версий.
    const QString host = url.host();
    if (host.compare(QStringLiteral("s3.amazonaws.com"), Qt::CaseInsensitive) == 0 ||
        host.compare(QStringLiteral("libraries.minecraft.net"), Qt::CaseInsensitive) == 0 ||
        host.compare(QStringLiteral("launcher.mojang.com"), Qt::CaseInsensitive) == 0) {
        url.setScheme(QStringLiteral("https"));
    }
    return url;
}

QUrl legacyMavenUrl(const QString &baseUrl, const QString &path)
{
    QString base = baseUrl.trimmed();
    if (base.isEmpty()) base = QStringLiteral("https://libraries.minecraft.net/");
    if (!base.endsWith(QLatin1Char('/'))) base.append(QLatin1Char('/'));
    return secureMojangUrl(base + path);
}

bool sha1Matches(const QString &path, const QString &expected)
{
    if (expected.isEmpty() || !QFileInfo::exists(path)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) return false;
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex()).compare(expected, Qt::CaseInsensitive) == 0;
}

} // namespace

MinecraftInstallService::MinecraftInstallService(const QString &dataDirectory,
                                                 DownloadManager *downloadManager,
                                                 QObject *parent)
    : QObject(parent), m_dataDirectory(QDir::cleanPath(dataDirectory)), m_downloadManager(downloadManager)
{
    Q_ASSERT(m_downloadManager);
    qRegisterMetaType<MinecraftVersionDescriptor>();
    qRegisterMetaType<QVector<MinecraftVersionDescriptor>>();
    connect(m_downloadManager, &DownloadManager::taskChanged,
            this, &MinecraftInstallService::onTaskChanged);
}

void MinecraftInstallService::refreshVersions(bool includeSnapshots, bool includeOldBeta, bool includeOldAlpha)
{
    requestManifest(includeSnapshots, includeOldBeta, includeOldAlpha);
}

QVector<MinecraftVersionDescriptor> MinecraftInstallService::versions() const
{
    return m_versions;
}

bool MinecraftInstallService::isInstalling() const
{
    return !m_job.instance.id.isEmpty() && !m_job.failed;
}

QString MinecraftInstallService::gameDirectory() const
{
    return QDir(m_dataDirectory).filePath(QStringLiteral("game"));
}

void MinecraftInstallService::requestManifest(bool includeSnapshots, bool includeOldBeta, bool includeOldAlpha)
{
    if (m_manifestReply) {
        QNetworkReply *previousReply = m_manifestReply;
        m_manifestReply = nullptr;
        previousReply->abort();
        previousReply->deleteLater();
    }
    QNetworkRequest request{QUrl(QString::fromLatin1(kVersionManifestUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.2 (Minecraft installer)"));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    m_manifestReply = m_network.get(request);
    m_manifestReply->setProperty("includeSnapshots", includeSnapshots);
    m_manifestReply->setProperty("includeOldBeta", includeOldBeta);
    m_manifestReply->setProperty("includeOldAlpha", includeOldAlpha);
    connect(m_manifestReply, &QNetworkReply::finished, this, &MinecraftInstallService::onManifestReply);
}

void MinecraftInstallService::onManifestReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    // refreshVersions() can replace an in-flight request. Ignore the cancelled
    // reply rather than letting it consume the newer active reply.
    if (reply != m_manifestReply) {
        reply->deleteLater();
        return;
    }
    m_manifestReply = nullptr;
    const bool includeSnapshots = reply->property("includeSnapshots").toBool();
    const bool includeOldBeta = reply->property("includeOldBeta").toBool();
    const bool includeOldAlpha = reply->property("includeOldAlpha").toBool();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError) {
        emit versionsError(QStringLiteral("Не удалось получить список версий Mojang: %1").arg(networkErrorText));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit versionsError(QStringLiteral("Mojang manifest вернул некорректный JSON: %1").arg(parseError.errorString()));
        return;
    }
    QVector<MinecraftVersionDescriptor> found;
    for (const QJsonValue &value : document.object().value(QStringLiteral("versions")).toArray()) {
        const QJsonObject object = value.toObject();
        MinecraftVersionDescriptor descriptor;
        descriptor.id = object.value(QStringLiteral("id")).toString();
        descriptor.type = object.value(QStringLiteral("type")).toString();
        descriptor.metadataUrl = object.value(QStringLiteral("url")).toString();
        descriptor.metadataSha1 = object.value(QStringLiteral("sha1")).toString();
        descriptor.releaseTime = QDateTime::fromString(object.value(QStringLiteral("releaseTime")).toString(), Qt::ISODate);
        const bool selected = descriptor.type == QStringLiteral("release")
            || (descriptor.type == QStringLiteral("snapshot") && includeSnapshots)
            || (descriptor.type == QStringLiteral("old_beta") && includeOldBeta)
            || (descriptor.type == QStringLiteral("old_alpha") && includeOldAlpha);
        if (selected && descriptor.isValid()) found.append(descriptor);
    }
    if (found.isEmpty()) {
        emit versionsError(QStringLiteral("Официальный Mojang manifest не содержит доступных версий."));
        return;
    }
    m_versions = found;
    emit versionsReady(m_versions);
}

void MinecraftInstallService::installVanilla(const Instance &instance)
{
    if (!m_downloadManager) {
        emit installError(instance.id, QStringLiteral("Менеджер загрузок не инициализирован."));
        return;
    }
    if (isInstalling()) {
        emit installError(instance.id, QStringLiteral("Другая установка уже выполняется. Дождитесь завершения очереди."));
        return;
    }
    if (instance.id.isEmpty() || instance.minecraftVersion.isEmpty()) {
        emit installError(instance.id, QStringLiteral("У экземпляра не выбрана версия Minecraft."));
        return;
    }
    MinecraftVersionDescriptor descriptor;
    for (const MinecraftVersionDescriptor &candidate : m_versions) {
        if (candidate.id == instance.minecraftVersion) {
            descriptor = candidate;
            break;
        }
    }
    if (!descriptor.isValid()) {
        emit installError(instance.id, QStringLiteral("Версия %1 не найдена. Нажмите «Обновить версии» и попробуйте снова.").arg(instance.minecraftVersion));
        return;
    }

    m_job = {};
    m_job.instance = instance;
    m_job.descriptor = descriptor;
    m_job.metadataPath = QDir(versionDirectory(descriptor.id)).filePath(descriptor.id + QStringLiteral(".json"));
    QDir().mkpath(versionDirectory(descriptor.id));
    QDir().mkpath(QDir(gameDirectory()).filePath(QStringLiteral("libraries")));
    QDir().mkpath(QDir(gameDirectory()).filePath(QStringLiteral("assets/indexes")));
    QDir().mkpath(QDir(gameDirectory()).filePath(QStringLiteral("assets/objects")));
    emit installStarted(instance.id, descriptor.id);
    scheduleMetadata(descriptor);
    m_downloadManager->start();
}

void MinecraftInstallService::scheduleMetadata(const MinecraftVersionDescriptor &descriptor)
{
    if (sha1Matches(m_job.metadataPath, descriptor.metadataSha1)) {
        QTimer::singleShot(0, this, &MinecraftInstallService::parseVersionMetadata);
        return;
    }
    const QString id = newTaskId(QStringLiteral("version-metadata"));
    m_job.metadataTaskId = id;
    enqueueFile(id, QStringLiteral("Minecraft %1: метаданные версии").arg(descriptor.id),
                QUrl(descriptor.metadataUrl), m_job.metadataPath, descriptor.metadataSha1, -1);
}

void MinecraftInstallService::onTaskChanged(const DownloadTask &task)
{
    if (!isInstalling() || !m_job.pendingTaskIds.contains(task.request.id)) return;
    if (task.state == DownloadState::Failed || task.state == DownloadState::Cancelled) {
        failInstall(QStringLiteral("Не удалось скачать «%1»: %2").arg(task.request.title, task.error));
        return;
    }
    if (task.state != DownloadState::Completed) return;

    const QString completedId = task.request.id;
    m_job.pendingTaskIds.remove(completedId);
    if (completedId == m_job.metadataTaskId) {
        parseVersionMetadata();
        return;
    }
    if (completedId == m_job.assetIndexTaskId) {
        m_job.awaitingAssetIndex = false;
        scheduleAssetObjects();
        return;
    }
    finishIfComplete();
}

void MinecraftInstallService::parseVersionMetadata()
{
    QFile file(m_job.metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        failInstall(QStringLiteral("Не удалось открыть метаданные версии: %1").arg(file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failInstall(QStringLiteral("Метаданные версии имеют некорректный JSON: %1").arg(parseError.errorString()));
        return;
    }
    scheduleVersionFiles(document.object());
}

void MinecraftInstallService::scheduleVersionFiles(const QJsonObject &metadata)
{
    const QJsonObject client = metadata.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("client")).toObject();
    const QUrl clientUrl = secureMojangUrl(client.value(QStringLiteral("url")).toString());
    const QString clientSha1 = client.value(QStringLiteral("sha1")).toString();
    if (clientUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || clientSha1.isEmpty()) {
        failInstall(QStringLiteral("Mojang metadata не содержит проверяемый client JAR."));
        return;
    }
    const QString clientPath = QDir(versionDirectory(m_job.descriptor.id)).filePath(m_job.descriptor.id + QStringLiteral(".jar"));
    enqueueFile(newTaskId(QStringLiteral("client")), QStringLiteral("Minecraft %1: client JAR").arg(m_job.descriptor.id),
                clientUrl, clientPath, clientSha1, client.value(QStringLiteral("size")).toVariant().toLongLong());

    const QJsonObject assetIndex = metadata.value(QStringLiteral("assetIndex")).toObject();
    const QUrl assetUrl = secureMojangUrl(assetIndex.value(QStringLiteral("url")).toString());
    const QString assetSha1 = assetIndex.value(QStringLiteral("sha1")).toString();
    const QString assetId = assetIndex.value(QStringLiteral("id")).toString();
    if (assetUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || assetSha1.isEmpty() || assetId.isEmpty()) {
        failInstall(QStringLiteral("Mojang metadata не содержит проверяемый asset index."));
        return;
    }
    m_job.assetIndexPath = QDir(gameDirectory()).filePath(QStringLiteral("assets/indexes/%1.json").arg(assetId));
    if (sha1Matches(m_job.assetIndexPath, assetSha1)) {
        m_job.awaitingAssetIndex = false;
        QTimer::singleShot(0, this, &MinecraftInstallService::scheduleAssetObjects);
    } else {
        m_job.assetIndexTaskId = newTaskId(QStringLiteral("asset-index"));
        m_job.awaitingAssetIndex = true;
        enqueueFile(m_job.assetIndexTaskId, QStringLiteral("Minecraft %1: asset index").arg(m_job.descriptor.id),
                    assetUrl, m_job.assetIndexPath, assetSha1,
                    assetIndex.value(QStringLiteral("size")).toVariant().toLongLong());
    }

    const QJsonObject loggingFile = metadata.value(QStringLiteral("logging")).toObject()
        .value(QStringLiteral("client")).toObject().value(QStringLiteral("file")).toObject();
    const QString loggingId = loggingFile.value(QStringLiteral("id")).toString();
    const QUrl loggingUrl = secureMojangUrl(loggingFile.value(QStringLiteral("url")).toString());
    const QString loggingSha1 = loggingFile.value(QStringLiteral("sha1")).toString();
    if (!loggingId.isEmpty() && QFileInfo(loggingId).fileName() == loggingId &&
        loggingUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 && !loggingSha1.isEmpty()) {
        const QString loggingPath = QDir(gameDirectory()).filePath(QStringLiteral("assets/log_configs/%1").arg(loggingId));
        enqueueFile(newTaskId(QStringLiteral("log-config")), QStringLiteral("Minecraft %1: конфигурация журнала").arg(m_job.descriptor.id),
                    loggingUrl, loggingPath, loggingSha1, loggingFile.value(QStringLiteral("size")).toVariant().toLongLong());
    }

    for (const QJsonValue &value : metadata.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject library = value.toObject();
        if (!libraryAllowedOnWindows(library)) continue;
        const QJsonObject downloads = library.value(QStringLiteral("downloads")).toObject();
        const QJsonObject artifact = downloads.value(QStringLiteral("artifact")).toObject();
        const QString name = library.value(QStringLiteral("name")).toString();
        const QString artifactPath = artifact.value(QStringLiteral("path")).toString();
        const QUrl artifactUrl = secureMojangUrl(artifact.value(QStringLiteral("url")).toString());
        const QString artifactSha1 = artifact.value(QStringLiteral("sha1")).toString();
        if (!artifactPath.isEmpty() && artifactUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 && !artifactSha1.isEmpty()) {
            enqueueFile(newTaskId(QStringLiteral("library")), QStringLiteral("Библиотека %1").arg(name),
                        artifactUrl, libraryDestination(artifactPath), artifactSha1,
                        artifact.value(QStringLiteral("size")).toVariant().toLongLong());
        } else {
            // Старые JSON и некоторые legacy loader profiles задают только Maven
            // coordinate и базовый URL. Без SHA-1 задача выполняется без ложной
            // проверки, но по-прежнему только через допустимый HTTPS-источник.
            const QString legacyPath = mavenPath(name);
            const QUrl legacyUrl = legacyMavenUrl(library.value(QStringLiteral("url")).toString(), legacyPath);
            if (!legacyPath.isEmpty() && legacyUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
                enqueueFile(newTaskId(QStringLiteral("library")), QStringLiteral("Библиотека %1").arg(name),
                            legacyUrl, libraryDestination(legacyPath), QString(), -1);
            }
        }

        const QJsonObject natives = library.value(QStringLiteral("natives")).toObject();
        QString classifier = natives.value(QStringLiteral("windows")).toString();
        classifier.replace(QStringLiteral("${arch}"), QStringLiteral("64"));
        const QJsonObject nativeFile = downloads.value(QStringLiteral("classifiers")).toObject().value(classifier).toObject();
        const QString nativePath = nativeFile.value(QStringLiteral("path")).toString().isEmpty()
            ? mavenPath(name + QLatin1Char(':') + classifier)
            : nativeFile.value(QStringLiteral("path")).toString();
        const QUrl nativeUrl = secureMojangUrl(nativeFile.value(QStringLiteral("url")).toString());
        const QString nativeSha1 = nativeFile.value(QStringLiteral("sha1")).toString();
        if (!classifier.isEmpty() && !nativePath.isEmpty() &&
            nativeUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 && !nativeSha1.isEmpty()) {
            enqueueFile(newTaskId(QStringLiteral("native")), QStringLiteral("Windows-библиотека %1").arg(name),
                        nativeUrl, libraryDestination(nativePath), nativeSha1,
                        nativeFile.value(QStringLiteral("size")).toVariant().toLongLong());
        } else if (!classifier.isEmpty() && !nativePath.isEmpty()) {
            const QUrl legacyNativeUrl = legacyMavenUrl(library.value(QStringLiteral("url")).toString(), nativePath);
            if (legacyNativeUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
                enqueueFile(newTaskId(QStringLiteral("native")), QStringLiteral("Windows-библиотека %1").arg(name),
                            legacyNativeUrl, libraryDestination(nativePath), QString(), -1);
            }
        }
    }
    m_downloadManager->start();
}

void MinecraftInstallService::scheduleAssetObjects()
{
    QFile file(m_job.assetIndexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        failInstall(QStringLiteral("Не удалось открыть asset index: %1").arg(file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject objects = document.object().value(QStringLiteral("objects")).toObject();
    if (parseError.error != QJsonParseError::NoError || objects.isEmpty()) {
        failInstall(QStringLiteral("Asset index имеет некорректный JSON или не содержит объектов."));
        return;
    }
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QJsonObject asset = it.value().toObject();
        const QString hash = asset.value(QStringLiteral("hash")).toString();
        if (hash.length() < 3) continue;
        const QString url = QStringLiteral("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash);
        enqueueFile(newTaskId(QStringLiteral("asset")), QStringLiteral("Asset: %1").arg(it.key()),
                    QUrl(url), assetDestination(hash), hash,
                    asset.value(QStringLiteral("size")).toVariant().toLongLong());
    }
    m_downloadManager->start();
    finishIfComplete();
}

void MinecraftInstallService::finishIfComplete()
{
    if (!isInstalling() || m_job.awaitingAssetIndex || !m_job.pendingTaskIds.isEmpty()) return;
    const QString instanceId = m_job.instance.id;
    const QString version = m_job.descriptor.id;
    Logger::info(QStringLiteral("Installed vanilla %1 for instance %2").arg(version, instanceId));
    m_job = {};
    emit installFinished(instanceId, version);
}

bool MinecraftInstallService::libraryAllowedOnWindows(const QJsonObject &library) const
{
    const QJsonArray rules = library.value(QStringLiteral("rules")).toArray();
    if (rules.isEmpty()) return true;
    bool allowed = false;
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        bool matches = true;
        const QJsonObject os = rule.value(QStringLiteral("os")).toObject();
        const QString osName = os.value(QStringLiteral("name")).toString();
        if (!osName.isEmpty() && osName != QStringLiteral("windows")) matches = false;
        if (!os.value(QStringLiteral("arch")).toString().isEmpty() && os.value(QStringLiteral("arch")).toString() != QStringLiteral("x86_64")) matches = false;
        if (matches) allowed = rule.value(QStringLiteral("action")).toString() == QStringLiteral("allow");
    }
    return allowed;
}

void MinecraftInstallService::enqueueFile(const QString &taskId, const QString &title, const QUrl &url,
                                          const QString &destination, const QString &sha1, qint64 size)
{
    if (!url.isValid() || url.scheme().toLower() != QStringLiteral("https")) {
        failInstall(QStringLiteral("Небезопасный URL в metadata: %1").arg(url.toString()));
        return;
    }
    if (sha1Matches(destination, sha1)) return;
    DownloadRequest request;
    request.id = taskId;
    request.title = title;
    request.url = url;
    // Piston metadata already gives a Maven-relative URL. При недоступности
    // libraries.minecraft.net можно безопасно запросить тот же путь из Maven
    // Central: размер и SHA-1 ниже всё равно должны полностью совпасть.
    if (url.host().compare(QStringLiteral("libraries.minecraft.net"), Qt::CaseInsensitive) == 0
        && url.path().startsWith(QLatin1Char('/'))) {
        request.fallbackUrl = QUrl(QStringLiteral("https://repo.maven.apache.org/maven2") + url.path());
    }
    request.destinationPath = destination;
    request.checksum = sha1;
    request.checksumAlgorithm = sha1.trimmed().isEmpty() ? ChecksumAlgorithm::None : ChecksumAlgorithm::Sha1;
    request.expectedSize = size > 0 ? size : -1;
    m_job.pendingTaskIds.insert(taskId);
    m_downloadManager->enqueue(request);
}

QString MinecraftInstallService::versionDirectory(const QString &version) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("versions/%1").arg(version));
}

QString MinecraftInstallService::libraryDestination(const QString &mavenFilePath) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("libraries/%1").arg(mavenFilePath));
}

QString MinecraftInstallService::assetDestination(const QString &hash) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("assets/objects/%1/%2").arg(hash.left(2), hash));
}

QString MinecraftInstallService::newTaskId(const QString &part)
{
    return QStringLiteral("minecraft:%1:%2:%3:%4")
        .arg(m_job.instance.id, m_job.descriptor.id, part, QString::number(++m_sequence));
}

void MinecraftInstallService::failInstall(const QString &message)
{
    if (m_job.instance.id.isEmpty() || m_job.failed) return;
    const QString instanceId = m_job.instance.id;
    m_job.failed = true;
    m_downloadManager->cancelAll();
    m_job.pendingTaskIds.clear();
    Logger::error(QStringLiteral("Minecraft installation failed: %1").arg(message));
    m_job = {};
    emit installError(instanceId, message);
}

} // namespace atlas
