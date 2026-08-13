#include "services/loader_install_service.h"

#include "infrastructure/logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>

namespace atlas {
namespace {

bool sha1Matches(const QString &path, const QString &expected)
{
    if (expected.trimmed().isEmpty() || !QFileInfo(path).isFile()) return false;
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

bool rulesAllowOnWindows(const QJsonArray &rules)
{
    if (rules.isEmpty()) return true;
    bool allowed = false;
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        bool matches = true;
        const QJsonObject os = rule.value(QStringLiteral("os")).toObject();
        const QString osName = os.value(QStringLiteral("name")).toString();
        const QString arch = os.value(QStringLiteral("arch")).toString();
        if (!osName.isEmpty() && osName != QStringLiteral("windows")) matches = false;
        if (!arch.isEmpty() && arch != QStringLiteral("x86_64")) matches = false;
        if (!rule.value(QStringLiteral("features")).toObject().isEmpty()) matches = false;
        if (matches) allowed = rule.value(QStringLiteral("action")).toString() == QStringLiteral("allow");
    }
    return allowed;
}

QJsonArray mergedArguments(const QJsonArray &base, const QJsonArray &overlay)
{
    QJsonArray result = base;
    for (const QJsonValue &value : overlay) result.append(value);
    return result;
}

struct MinecraftReleaseVersion {
    int minor = -1;
    int patch = -1;

    bool isValid() const { return minor >= 0 && patch >= 0; }
};

MinecraftReleaseVersion parseMinecraftReleaseVersion(const QString &version)
{
    // Только обычные release-пути формата 1.x[.y]. Снапшоты, Beta и Alpha
    // намеренно не проходят: для них официальный API этих loaders не обещает
    // совместимый профиль.
    static const QRegularExpression pattern(QStringLiteral("^1\\.(\\d+)(?:\\.(\\d+))?(?:[-+].*)?$"));
    const QRegularExpressionMatch match = pattern.match(version.trimmed());
    if (!match.hasMatch()) return {};
    return {match.captured(1).toInt(), match.captured(2).isEmpty() ? 0 : match.captured(2).toInt()};
}

bool isAtLeast(const MinecraftReleaseVersion &version, int minor, int patch = 0)
{
    return version.minor > minor || (version.minor == minor && version.patch >= patch);
}

bool isAtMost(const MinecraftReleaseVersion &version, int minor, int patch = 0)
{
    return version.minor < minor || (version.minor == minor && version.patch <= patch);
}

QString versionCacheKey(LoaderKind kind, const QString &minecraftVersion)
{
    return QString::number(static_cast<int>(kind)) + QLatin1Char('|') + minecraftVersion.trimmed();
}

bool loaderSupportsMinecraftVersion(LoaderKind kind, const QString &minecraftVersion)
{
    const MinecraftReleaseVersion version = parseMinecraftReleaseVersion(minecraftVersion);
    if (!version.isValid()) return false;
    switch (kind) {
    case LoaderKind::Forge:
        return isAtLeast(version, 1, 0);
    case LoaderKind::Fabric:
    case LoaderKind::Quilt:
        return isAtLeast(version, 14, 0);
    case LoaderKind::LegacyFabric:
        return isAtLeast(version, 0, 0) && isAtMost(version, 13, 2);
    case LoaderKind::NeoForge:
        return isAtLeast(version, 20, 2);
    default:
        return false;
    }
}

bool ensureLauncherProfilesFile(const QString &gameDirectory, QString *error)
{
    const QString path = QDir(gameDirectory).filePath(QStringLiteral("launcher_profiles.json"));
    if (QFileInfo(path).isFile()) return true;

    QJsonObject document;
    document.insert(QStringLiteral("profiles"), QJsonObject{});
    document.insert(QStringLiteral("selectedProfile"), QStringLiteral("(Default)"));
    document.insert(QStringLiteral("clientToken"), QUuid::createUuid().toString(QUuid::WithoutBraces));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Не удалось создать launcher_profiles.json: %1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить launcher_profiles.json: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace

LoaderInstallService::LoaderInstallService(const QString &dataDirectory,
                                           DownloadManager *downloadManager,
                                           QObject *parent)
    : QObject(parent), m_dataDirectory(QDir::cleanPath(dataDirectory)), m_downloadManager(downloadManager)
{
    Q_ASSERT(m_downloadManager);
    qRegisterMetaType<LoaderVersionDescriptor>();
    qRegisterMetaType<QVector<LoaderVersionDescriptor>>();
    qRegisterMetaType<LoaderKind>();
    connect(m_downloadManager, &DownloadManager::taskChanged,
            this, &LoaderInstallService::onTaskChanged);
    connect(&m_installerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LoaderInstallService::finishInstaller);
    connect(&m_installerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!isInstalling() || error != QProcess::FailedToStart) return;
        failInstall(QStringLiteral("Не удалось запустить официальный installer %1: %2")
                    .arg(loaderKindToString(m_job.instance.loader.kind), m_installerProcess.errorString()));
    });
}

void LoaderInstallService::refreshVersions(LoaderKind kind, const QString &minecraftVersion)
{
    if (!supports(kind)) {
        emit versionsError(kind, minecraftVersion, QStringLiteral("Выбранный загрузчик не поддерживается Atlas."));
        return;
    }
    if (minecraftVersion.trimmed().isEmpty()) {
        emit versionsError(kind, minecraftVersion, QStringLiteral("Не выбрана версия Minecraft."));
        return;
    }
    if (!supportsMinecraftVersion(kind, minecraftVersion)) {
        emit versionsError(kind, minecraftVersion, QStringLiteral("%1 не поддерживает Minecraft %2.")
                           .arg(loaderKindToString(kind), minecraftVersion));
        return;
    }

    const QString cacheKey = versionCacheKey(kind, minecraftVersion);
    const auto cached = m_versionCache.constFind(cacheKey);
    if (cached != m_versionCache.constEnd()) {
        const QVector<LoaderVersionDescriptor> cachedVersions = cached.value();
        m_versions = cachedVersions;
        QTimer::singleShot(0, this, [this, kind, minecraftVersion, cachedVersions]() {
            emit versionsReady(kind, minecraftVersion, cachedVersions);
        });
        return;
    }

    requestJson(QUrl(loaderVersionsUrl(kind, minecraftVersion)), RequestPurpose::VersionList, kind, minecraftVersion);
}

void LoaderInstallService::install(const Instance &instance, const QString &javaExecutable)
{
    if (!m_downloadManager) {
        emit installError(instance.id, QStringLiteral("Менеджер загрузок не инициализирован."));
        return;
    }
    if (isInstalling()) {
        emit installError(instance.id, QStringLiteral("Другая установка загрузчика ещё выполняется. Дождитесь завершения очереди."));
        return;
    }
    if (!supports(instance.loader.kind)) {
        emit installError(instance.id, QStringLiteral("Для %1 пока нет безопасной нативной установки Atlas. Поддерживаются Fabric, Legacy Fabric, Quilt, Forge и NeoForge.")
                          .arg(loaderKindToString(instance.loader.kind)));
        return;
    }
    if (!supportsMinecraftVersion(instance.loader.kind, instance.minecraftVersion)) {
        emit installError(instance.id, QStringLiteral("%1 не поддерживает Minecraft %2. Выберите только показанный совместимый загрузчик.")
                          .arg(loaderKindToString(instance.loader.kind), instance.minecraftVersion));
        return;
    }
    if (instance.id.isEmpty() || instance.minecraftVersion.isEmpty()) {
        emit installError(instance.id, QStringLiteral("У экземпляра не заполнены идентификатор или версия Minecraft."));
        return;
    }
    const QString baseMetadata = QDir(versionDirectory(instance.minecraftVersion))
        .filePath(instance.minecraftVersion + QStringLiteral(".json"));
    if (!QFileInfo(baseMetadata).isFile()) {
        emit installError(instance.id, QStringLiteral("Сначала установите Vanilla Minecraft %1 для этого профиля.")
                          .arg(instance.minecraftVersion));
        return;
    }

    m_job = {};
    m_job.instance = instance;
    m_job.javaExecutable = javaExecutable.trimmed();
    emit installStarted(instance.id, instance.loader.kind, instance.minecraftVersion);
    if (instance.loader.kind == LoaderKind::Forge || instance.loader.kind == LoaderKind::NeoForge) {
        if (m_job.javaExecutable.isEmpty() || !QFileInfo(m_job.javaExecutable).isFile()) {
            failInstall(QStringLiteral("Для %1 нужна установленная локальная Java Atlas. Выберите Java или дождитесь её установки.")
                        .arg(loaderKindToString(instance.loader.kind)));
            return;
        }
        if (instance.loader.version.trimmed().isEmpty()) {
            const QString example = instance.loader.kind == LoaderKind::Forge
                ? QStringLiteral("1.20.1-47.4.22") : QStringLiteral("21.1.142");
            failInstall(QStringLiteral("Для %1 укажите точный номер версии из официальных загрузок, например %2.")
                        .arg(loaderKindToString(instance.loader.kind), example));
            return;
        }
        requestInstallerChecksum(instance.loader.kind, instance.loader.version.trimmed());
        return;
    }
    if (instance.loader.version.trimmed().isEmpty()) {
        requestJson(QUrl(loaderVersionsUrl(instance.loader.kind, instance.minecraftVersion)),
                    RequestPurpose::InstallVersionList, instance.loader.kind, instance.minecraftVersion);
        return;
    }
    beginProfileRequest(instance, instance.loader.version.trimmed());
}

bool LoaderInstallService::isInstalling() const
{
    return !m_job.instance.id.isEmpty() && !m_job.failed;
}

bool LoaderInstallService::supportsMinecraftVersion(LoaderKind kind, const QString &minecraftVersion) const
{
    return supports(kind) && loaderSupportsMinecraftVersion(kind, minecraftVersion);
}

bool LoaderInstallService::supports(LoaderKind kind) const
{
    return kind == LoaderKind::Fabric || kind == LoaderKind::LegacyFabric || kind == LoaderKind::Quilt
        || kind == LoaderKind::Forge || kind == LoaderKind::NeoForge;
}

QString LoaderInstallService::apiBase(LoaderKind kind) const
{
    switch (kind) {
    case LoaderKind::Fabric: return QStringLiteral("https://meta.fabricmc.net/v2");
    case LoaderKind::LegacyFabric: return QStringLiteral("https://meta.legacyfabric.net/v2");
    case LoaderKind::Quilt: return QStringLiteral("https://meta.quiltmc.org/v3");
    default: return {};
    }
}

QString LoaderInstallService::loaderVersionsUrl(LoaderKind kind, const QString &minecraftVersion) const
{
    switch (kind) {
    case LoaderKind::Fabric:
    case LoaderKind::LegacyFabric:
    case LoaderKind::Quilt:
        return apiBase(kind) + QStringLiteral("/versions/loader/")
            + QString::fromLatin1(QUrl::toPercentEncoding(minecraftVersion));
    case LoaderKind::Forge:
        return QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");
    case LoaderKind::NeoForge:
        return QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml");
    default:
        return {};
    }
}

QString LoaderInstallService::profileUrl(LoaderKind kind, const QString &minecraftVersion, const QString &loaderVersion) const
{
    return loaderVersionsUrl(kind, minecraftVersion) + QLatin1Char('/')
        + QString::fromLatin1(QUrl::toPercentEncoding(loaderVersion)) + QStringLiteral("/profile/json");
}

QString LoaderInstallService::forgeInstallerUrl(const QString &loaderVersion) const
{
    static const QRegularExpression safeVersion(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (!safeVersion.match(loaderVersion).hasMatch()) return {};
    return QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar")
        .arg(loaderVersion);
}

QString LoaderInstallService::neoForgeInstallerUrl(const QString &loaderVersion) const
{
    static const QRegularExpression safeVersion(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (!safeVersion.match(loaderVersion).hasMatch()) return {};
    return QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar")
        .arg(loaderVersion);
}

QString LoaderInstallService::installerUrl(LoaderKind kind, const QString &loaderVersion) const
{
    if (kind == LoaderKind::Forge) return forgeInstallerUrl(loaderVersion);
    if (kind == LoaderKind::NeoForge) return neoForgeInstallerUrl(loaderVersion);
    return {};
}

void LoaderInstallService::requestInstallerChecksum(LoaderKind kind, const QString &loaderVersion)
{
    const QString url = installerUrl(kind, loaderVersion);
    if (url.isEmpty()) {
        failInstall(QStringLiteral("Номер версии %1 содержит недопустимые символы.").arg(loaderKindToString(kind)));
        return;
    }
    m_job.resolvedLoaderVersion = loaderVersion;
    const QString extension = kind == LoaderKind::Forge ? QStringLiteral(".sha1") : QStringLiteral(".sha256");
    const QUrl checksumUrl(url + extension);
    if (!checksumUrl.isValid() || checksumUrl.scheme() != QStringLiteral("https")) {
        failInstall(QStringLiteral("Не удалось сформировать HTTPS URL checksum installer %1.").arg(loaderKindToString(kind)));
        return;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_requestPurpose = RequestPurpose::InstallerChecksum;
    m_requestKind = kind;
    m_requestMinecraftVersion = m_job.instance.minecraftVersion;
    m_requestLoaderVersion = loaderVersion;
    QNetworkRequest request(checksumUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.3.3 (installer verification)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &LoaderInstallService::onNetworkReply);
}

void LoaderInstallService::requestJson(const QUrl &url, RequestPurpose purpose, LoaderKind kind,
                                       const QString &minecraftVersion, const QString &loaderVersion)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https")) {
        if (purpose == RequestPurpose::VersionList) {
            emit versionsError(kind, minecraftVersion, QStringLiteral("Сформирован небезопасный URL metadata API."));
        } else {
            failInstall(QStringLiteral("Сформирован небезопасный URL профиля загрузчика."));
        }
        return;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_requestPurpose = purpose;
    m_requestKind = kind;
    m_requestMinecraftVersion = minecraftVersion;
    m_requestLoaderVersion = loaderVersion;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.3.3 (loader installer)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &LoaderInstallService::onNetworkReply);
}

void LoaderInstallService::onNetworkReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) return;
    const RequestPurpose purpose = m_requestPurpose;
    const LoaderKind kind = m_requestKind;
    const QString minecraftVersion = m_requestMinecraftVersion;
    const QString loaderVersion = m_requestLoaderVersion;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        const QString message = purpose == RequestPurpose::InstallerChecksum
            ? QStringLiteral("Не удалось получить checksum installer %1: %2").arg(loaderKindToString(kind), errorText)
            : QStringLiteral("Не удалось получить metadata %1: %2").arg(loaderKindToString(kind), errorText);
        if (purpose == RequestPurpose::VersionList) emit versionsError(kind, minecraftVersion, message);
        else failInstall(message);
        return;
    }
    if (purpose == RequestPurpose::InstallerChecksum) {
        const int length = kind == LoaderKind::Forge ? 40 : 64;
        const QRegularExpression checksumPattern(QStringLiteral("\\b([A-Fa-f0-9]{%1})\\b").arg(length));
        const QRegularExpressionMatch match = checksumPattern.match(QString::fromUtf8(payload));
        if (!match.hasMatch()) {
            failInstall(QStringLiteral("Официальный Maven %1 не вернул допустимый checksum installer JAR.").arg(loaderKindToString(kind)));
            return;
        }
        scheduleInstaller(match.captured(1).toLower(), kind == LoaderKind::Forge ? ChecksumAlgorithm::Sha1 : ChecksumAlgorithm::Sha256);
        return;
    }

    if ((kind == LoaderKind::Forge || kind == LoaderKind::NeoForge)
        && (purpose == RequestPurpose::VersionList || purpose == RequestPurpose::InstallVersionList)) {
        parseMavenVersionList(payload, kind, minecraftVersion, purpose);
        return;
    }

    QJsonParseError parseError;

    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        const QString message = QStringLiteral("Metadata %1 вернул некорректный JSON: %2")
            .arg(loaderKindToString(kind), parseError.errorString());
        if (purpose == RequestPurpose::VersionList) emit versionsError(kind, minecraftVersion, message);
        else failInstall(message);
        return;
    }

    if (purpose == RequestPurpose::VersionList) {
        parseVersionList(document, kind, minecraftVersion);
        return;
    }
    if (purpose == RequestPurpose::InstallVersionList) {
        parseVersionList(document, kind, minecraftVersion);
        LoaderVersionDescriptor selected;
        for (const LoaderVersionDescriptor &candidate : m_versions) {
            if (candidate.stable) {
                selected = candidate;
                break;
            }
        }
        if (!selected.isValid() && !m_versions.isEmpty()) selected = m_versions.first();
        if (!selected.isValid()) {
            failInstall(QStringLiteral("Официальный metadata API не вернул совместимых версий %1 для Minecraft %2.")
                        .arg(loaderKindToString(kind), minecraftVersion));
            return;
        }
        beginProfileRequest(m_job.instance, selected.version);
        return;
    }
    if (!document.isObject()) {
        failInstall(QStringLiteral("Профиль %1 имеет неверный формат JSON.").arg(loaderKindToString(kind)));
        return;
    }
    m_job.resolvedLoaderVersion = loaderVersion;
    parseAndScheduleProfile(document.object());
}

void LoaderInstallService::parseVersionList(const QJsonDocument &document, LoaderKind kind, const QString &minecraftVersion)
{
    if (!document.isArray()) {
        emit versionsError(kind, minecraftVersion, QStringLiteral("Официальный metadata API вернул список версий в неверном формате."));
        return;
    }
    QVector<LoaderVersionDescriptor> versions;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject entry = value.toObject();
        const QJsonObject loader = entry.value(QStringLiteral("loader")).isObject()
            ? entry.value(QStringLiteral("loader")).toObject() : entry;
        LoaderVersionDescriptor descriptor;
        descriptor.version = loader.value(QStringLiteral("version")).toString();
        descriptor.stable = loader.contains(QStringLiteral("stable")) ? loader.value(QStringLiteral("stable")).toBool() : true;
        if (descriptor.isValid()) versions.append(descriptor);
    }
    m_versions = versions;
    if (versions.isEmpty()) {
        emit versionsError(kind, minecraftVersion, QStringLiteral("Официальный metadata API не вернул совместимых версий загрузчика."));
        return;
    }
    m_versionCache.insert(versionCacheKey(kind, minecraftVersion), versions);
    emit versionsReady(kind, minecraftVersion, versions);
}

void LoaderInstallService::parseMavenVersionList(const QByteArray &payload, LoaderKind kind,
                                                 const QString &minecraftVersion, RequestPurpose purpose)
{
    QXmlStreamReader reader(payload);
    QVector<LoaderVersionDescriptor> versions;
    const QString normalizedMinecraft = minecraftVersion.trimmed();
    QString prefix;
    if (kind == LoaderKind::Forge) {
        prefix = normalizedMinecraft + QLatin1Char('-');
    } else {
        QString neoMinecraft = normalizedMinecraft;
        if (neoMinecraft.startsWith(QStringLiteral("1."))) neoMinecraft.remove(0, 2);
        prefix = neoMinecraft + QLatin1Char('.');
    }
    static const QRegularExpression prerelease(QStringLiteral("(?:^|[._-])(alpha|beta|rc|snapshot)(?:[._-]|$)"),
                                                QRegularExpression::CaseInsensitiveOption);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("version")) continue;
        const QString version = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (!version.startsWith(prefix)) continue;
        LoaderVersionDescriptor descriptor;
        descriptor.version = version;
        descriptor.stable = !prerelease.match(version).hasMatch();
        if (descriptor.isValid()) versions.append(descriptor);
    }

    if (reader.hasError()) {
        const QString message = QStringLiteral("Официальный Maven %1 вернул некорректный XML: %2")
            .arg(loaderKindToString(kind), reader.errorString());
        if (purpose == RequestPurpose::VersionList) emit versionsError(kind, minecraftVersion, message);
        else failInstall(message);
        return;
    }

    std::reverse(versions.begin(), versions.end());
    m_versions = versions;
    if (versions.isEmpty()) {
        const QString message = QStringLiteral("Официальный Maven %1 не вернул совместимых версий для Minecraft %2.")
            .arg(loaderKindToString(kind), minecraftVersion);
        if (purpose == RequestPurpose::VersionList) emit versionsError(kind, minecraftVersion, message);
        else failInstall(message);
        return;
    }

    m_versionCache.insert(versionCacheKey(kind, minecraftVersion), versions);

    if (purpose == RequestPurpose::InstallVersionList) {
        LoaderVersionDescriptor selected;
        for (const LoaderVersionDescriptor &candidate : versions) {
            if (candidate.stable) {
                selected = candidate;
                break;
            }
        }
        if (!selected.isValid()) selected = versions.first();
        requestInstallerChecksum(kind, selected.version);
        return;
    }
    emit versionsReady(kind, minecraftVersion, versions);
}

void LoaderInstallService::beginProfileRequest(const Instance &instance, const QString &loaderVersion)
{
    if (!isInstalling()) return;
    if (loaderVersion.trimmed().isEmpty()) {
        failInstall(QStringLiteral("Не выбрана версия загрузчика."));
        return;
    }
    m_job.resolvedLoaderVersion = loaderVersion.trimmed();
    requestJson(QUrl(profileUrl(instance.loader.kind, instance.minecraftVersion, m_job.resolvedLoaderVersion)),
                RequestPurpose::Profile, instance.loader.kind, instance.minecraftVersion, m_job.resolvedLoaderVersion);
}

void LoaderInstallService::scheduleInstaller(const QString &checksum, ChecksumAlgorithm algorithm)
{
    const LoaderKind kind = m_job.instance.loader.kind;
    const QString url = installerUrl(kind, m_job.resolvedLoaderVersion);
    const int expectedLength = algorithm == ChecksumAlgorithm::Sha1 ? 40 : 64;
    if (url.isEmpty() || checksum.size() != expectedLength) {
        failInstall(QStringLiteral("Не удалось подготовить проверяемую загрузку installer %1.").arg(loaderKindToString(kind)));
        return;
    }
    const QString kindName = loaderKindToString(kind);
    const QString stagingDirectory = QDir(m_dataDirectory).filePath(
        QStringLiteral("staging/%1/%2").arg(kindName, m_job.instance.id));
    if (!QDir().mkpath(stagingDirectory)) {
        failInstall(QStringLiteral("Не удалось создать staging-папку installer %1.").arg(kindName));
        return;
    }
    m_job.installerPath = QDir(stagingDirectory).filePath(
        QStringLiteral("%1-%2-installer.jar").arg(kindName, m_job.resolvedLoaderVersion));
    DownloadRequest request;
    request.id = newTaskId(kindName + QStringLiteral("-installer"));
    request.title = QStringLiteral("%1 %2: официальный installer").arg(kindName, m_job.resolvedLoaderVersion);
    request.url = QUrl(url);
    request.destinationPath = m_job.installerPath;
    request.checksum = checksum;
    request.checksumAlgorithm = algorithm;
    m_job.installerTaskId = request.id;
    m_downloadManager->enqueue(request);
    m_downloadManager->start();
}

void LoaderInstallService::launchInstaller()
{
    if (!isInstalling()) return;
    const QString kindName = loaderKindToString(m_job.instance.loader.kind);
    if (!QFileInfo(m_job.installerPath).isFile()) {
        failInstall(QStringLiteral("Проверенный installer %1 не найден после загрузки.").arg(kindName));
        return;
    }
    const QString installDirectory = gameDirectory();
    QString profileError;
    if (!ensureLauncherProfilesFile(installDirectory, &profileError)) {
        failInstall(profileError);
        return;
    }
    m_installerProcess.setWorkingDirectory(installDirectory);
    m_installerProcess.setProgram(m_job.javaExecutable);
    // Forge/NeoForge по умолчанию ищут %APPDATA%/.minecraft. Atlas хранит
    // общую игру в собственной папке, поэтому путь передаётся явно.
    m_installerProcess.setArguments({QStringLiteral("-jar"), m_job.installerPath,
                                    QStringLiteral("--installClient"), installDirectory});
    Logger::info(QStringLiteral("Starting official %1 installer %2 for %3")
                 .arg(kindName, m_job.resolvedLoaderVersion, m_job.instance.minecraftVersion));
    m_installerProcess.start();
}

void LoaderInstallService::finishInstaller(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!isInstalling()) return;
    const LoaderKind kind = m_job.instance.loader.kind;
    if (kind != LoaderKind::Forge && kind != LoaderKind::NeoForge) return;
    const QString kindName = loaderKindToString(kind);
    const bool installerSucceeded = exitStatus == QProcess::NormalExit && exitCode == 0;
    const QString standardError = QString::fromLocal8Bit(m_installerProcess.readAllStandardError()).trimmed();
    const QString standardOutput = QString::fromLocal8Bit(m_installerProcess.readAllStandardOutput()).trimmed();
    const QString details = !standardError.isEmpty() ? standardError : standardOutput;

    const QString prefix = kind == LoaderKind::Forge ? QStringLiteral("forge-") : QStringLiteral("neoforge-");
    QStringList candidates{prefix + m_job.resolvedLoaderVersion, m_job.resolvedLoaderVersion};
    if (kind == LoaderKind::Forge) {
        const QString minecraftPrefix = m_job.instance.minecraftVersion + QLatin1Char('-');
        if (m_job.resolvedLoaderVersion.startsWith(minecraftPrefix)) {
            // Forge 1.12 installer создаёт ID вида 1.12.2-forge-14.23.5.2864,
            // а API возвращает 1.12.2-14.23.5.2864.
            candidates.append(m_job.instance.minecraftVersion + QStringLiteral("-forge-")
                              + m_job.resolvedLoaderVersion.mid(minecraftPrefix.size()));
        }
    }
    const QDir versions(QDir(gameDirectory()).filePath(QStringLiteral("versions")));
    const QFileInfoList directories = versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &directory : directories) {
        const QString name = directory.fileName();
        if (!name.contains(prefix.left(prefix.size() - 1), Qt::CaseInsensitive)) continue;
        if (name.contains(m_job.resolvedLoaderVersion)
            || name.endsWith(QLatin1Char('-') + m_job.resolvedLoaderVersion.section(QLatin1Char('-'), -1))) {
            candidates.append(name);
        }
    }
    candidates.removeDuplicates();

    // Старые Forge-installer сначала создают version JSON, а затем сами скачивают
    // библиотеки. Если их собственное HTTPS-соединение отвалилось, Atlas может
    // безопасно продолжить из этого официального JSON через свою очередь с SHA-1.
    for (const QString &candidate : candidates) {
        const QString metadataPath = QDir(versionDirectory(candidate)).filePath(candidate + QStringLiteral(".json"));
        QFile metadata(metadataPath);
        if (!metadata.open(QIODevice::ReadOnly)) continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject profile = document.object();
        if (profile.value(QStringLiteral("id")).toString().isEmpty()
            || profile.value(QStringLiteral("mainClass")).toString().isEmpty()) continue;
        if (!installerSucceeded) {
            Logger::warning(QStringLiteral("Official %1 installer ended with code %2; continuing libraries through Atlas queue: %3")
                            .arg(kindName).arg(exitCode, 0, 10).arg(details.left(240)));
        }
        // При полностью готовом profile finishIfComplete() вызывается синхронно.
        // Закрываем исходный JSON заранее: в Windows QSaveFile не может атомарно
        // заменить файл, пока он остаётся открытым этим же процессом.
        metadata.close();
        parseAndScheduleProfile(profile);
        return;
    }

    if (!installerSucceeded) {
        failInstall(QStringLiteral("Официальный installer %1 завершился с кодом %2%3")
                    .arg(kindName).arg(exitCode)
                    .arg(details.isEmpty() ? QString() : QStringLiteral(": %1").arg(details)));
        return;
    }
    failInstall(QStringLiteral("Installer %1 завершился, но launcher profile не найден в папке versions.").arg(kindName));
}

void LoaderInstallService::parseAndScheduleProfile(const QJsonObject &profile)
{
    const QString basePath = QDir(versionDirectory(m_job.instance.minecraftVersion))
        .filePath(m_job.instance.minecraftVersion + QStringLiteral(".json"));
    QFile baseFile(basePath);
    if (!baseFile.open(QIODevice::ReadOnly)) {
        failInstall(QStringLiteral("Не удалось открыть metadata установленной Vanilla-версии: %1").arg(baseFile.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument baseDocument = QJsonDocument::fromJson(baseFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !baseDocument.isObject()) {
        failInstall(QStringLiteral("Metadata Vanilla повреждены: %1").arg(parseError.errorString()));
        return;
    }
    QString mergeError;
    const QJsonObject result = mergedProfile(baseDocument.object(), profile, &mergeError);
    if (result.isEmpty()) {
        failInstall(mergeError.isEmpty() ? QStringLiteral("Не удалось объединить launcher profile.") : mergeError);
        return;
    }
    m_job.mergedProfile = result;
    m_job.profileId = result.value(QStringLiteral("id")).toString();
    if (m_job.profileId.isEmpty()) {
        failInstall(QStringLiteral("Launcher profile не содержит идентификатор версии."));
        return;
    }
    scheduleProfileLibraries();
}

QJsonObject LoaderInstallService::mergedProfile(const QJsonObject &base, const QJsonObject &profile, QString *error) const
{
    const QString inherited = profile.value(QStringLiteral("inheritsFrom")).toString();
    if (!inherited.isEmpty() && inherited != m_job.instance.minecraftVersion) {
        if (error) *error = QStringLiteral("Launcher profile рассчитан на Minecraft %1, а выбран экземпляр %2.")
            .arg(inherited, m_job.instance.minecraftVersion);
        return {};
    }
    const QString profileId = profile.value(QStringLiteral("id")).toString();
    const QString mainClass = profile.value(QStringLiteral("mainClass")).toString();
    if (profileId.isEmpty() || mainClass.isEmpty()) {
        if (error) *error = QStringLiteral("Launcher profile не содержит id или mainClass.");
        return {};
    }

    QJsonObject result = base;
    result.insert(QStringLiteral("id"), profileId);
    result.insert(QStringLiteral("inheritsFrom"), m_job.instance.minecraftVersion);
    result.insert(QStringLiteral("jar"), m_job.instance.minecraftVersion);
    result.insert(QStringLiteral("mainClass"), mainClass);

    QJsonArray libraries = base.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &value : profile.value(QStringLiteral("libraries")).toArray()) {
        QString normalizeError;
        const QJsonObject library = normalizedLibrary(value.toObject(), m_job.instance.loader.kind, &normalizeError);
        if (library.isEmpty()) {
            if (error) *error = normalizeError;
            return {};
        }
        libraries.append(library);
    }
    result.insert(QStringLiteral("libraries"), libraries);

    const QJsonObject baseArguments = base.value(QStringLiteral("arguments")).toObject();
    const QJsonObject profileArguments = profile.value(QStringLiteral("arguments")).toObject();
    if (!profileArguments.isEmpty()) {
        QJsonObject arguments = baseArguments;
        arguments.insert(QStringLiteral("jvm"), mergedArguments(baseArguments.value(QStringLiteral("jvm")).toArray(),
                                                                  profileArguments.value(QStringLiteral("jvm")).toArray()));
        arguments.insert(QStringLiteral("game"), mergedArguments(baseArguments.value(QStringLiteral("game")).toArray(),
                                                                   profileArguments.value(QStringLiteral("game")).toArray()));
        result.insert(QStringLiteral("arguments"), arguments);
    }

    // Старые Forge profiles используют legacy minecraftArguments вместо блока
    // arguments.game. При запуске через LaunchWrapper FMLTweaker обязателен:
    // иначе LaunchWrapper выбирает VanillaTweaker и не применяет ремаппинг классов.
    if (m_job.instance.loader.kind == LoaderKind::Forge
        && mainClass == QStringLiteral("net.minecraft.launchwrapper.Launch")) {
        QString legacyArguments = profile.value(QStringLiteral("minecraftArguments")).toString();
        if (legacyArguments.isEmpty()) {
            legacyArguments = base.value(QStringLiteral("minecraftArguments")).toString();
        }
        if (!legacyArguments.contains(QStringLiteral("--tweakClass"))) {
            legacyArguments += QStringLiteral(" --tweakClass net.minecraftforge.fml.common.launcher.FMLTweaker");
        }
        if (legacyArguments.contains(QStringLiteral("--versionType"))) {
            legacyArguments.replace(QRegularExpression(QStringLiteral("--versionType\\s+\\S+")),
                                    QStringLiteral("--versionType Forge"));
        } else {
            legacyArguments += QStringLiteral(" --versionType Forge");
        }
        result.insert(QStringLiteral("minecraftArguments"), legacyArguments.trimmed());
    }
    return result;
}

QJsonObject LoaderInstallService::normalizedLibrary(const QJsonObject &library, LoaderKind kind, QString *error) const
{
    if (!libraryAllowedOnWindows(library)) return library;
    const QString coordinate = library.value(QStringLiteral("name")).toString();
    if (coordinate.isEmpty()) {
        if (error) *error = QStringLiteral("Launcher profile содержит библиотеку без Maven coordinate.");
        return {};
    }

    QJsonObject result = library;
    QJsonObject downloads = result.value(QStringLiteral("downloads")).toObject();
    const QJsonObject natives = result.value(QStringLiteral("natives")).toObject();
    QString windowsClassifier = natives.value(QStringLiteral("windows")).toString();
    windowsClassifier.replace(QStringLiteral("${arch}"), QStringLiteral("64"));

    auto normalizeFile = [&](QJsonObject file, const QString &classifier, bool inheritLibrarySha1) -> QJsonObject {
        QString relativePath = file.value(QStringLiteral("path")).toString();
        if (relativePath.isEmpty()) {
            relativePath = mavenPath(coordinate + (classifier.isEmpty() ? QString() : QLatin1Char(':') + classifier));
        }
        if (relativePath.isEmpty() || relativePath.startsWith(QStringLiteral("../")) || relativePath.contains(QStringLiteral("/../"))) {
            if (error) *error = QStringLiteral("Launcher profile содержит небезопасный Maven path для %1.").arg(coordinate);
            return {};
        }
        QUrl artifactUrl(file.value(QStringLiteral("url")).toString());
        if (!artifactUrl.isValid() || artifactUrl.scheme().isEmpty()) {
            // Старые Forge profile (в частности 1.12.2) записывают локальный
            // путь и SHA-1 universal JAR, но оставляют URL пустым. Это ровно
            // официальный Maven-артефакт с classifier universal; локальный
            // путь остаётся тем, который ожидает launcher profile.
            if (kind == LoaderKind::Forge
                && classifier.isEmpty()
                && coordinate.startsWith(QStringLiteral("net.minecraftforge:forge:"))) {
                artifactUrl = QUrl(QStringLiteral("https://maven.minecraftforge.net/")
                                   + mavenPath(coordinate + QStringLiteral(":universal")));
            } else {
                QUrl base(library.value(QStringLiteral("url")).toString());
                if (!base.isValid() || base.scheme() != QStringLiteral("https")) {
                    if (error) *error = QStringLiteral("Launcher profile не содержит HTTPS Maven URL для %1.").arg(coordinate);
                    return {};
                }
                QString root = base.toString();
                if (!root.endsWith(QLatin1Char('/'))) root.append(QLatin1Char('/'));
                artifactUrl = QUrl(root + relativePath);
            }
        }
        if (!artifactUrl.isValid() || artifactUrl.scheme() != QStringLiteral("https")) {
            if (error) *error = QStringLiteral("Launcher profile содержит небезопасный URL библиотеки %1.").arg(coordinate);
            return {};
        }
        file.insert(QStringLiteral("path"), relativePath);
        file.insert(QStringLiteral("url"), artifactUrl.toString());
        if (inheritLibrarySha1 && file.value(QStringLiteral("sha1")).toString().isEmpty()
            && !library.value(QStringLiteral("sha1")).toString().isEmpty()) {
            file.insert(QStringLiteral("sha1"), library.value(QStringLiteral("sha1")).toString());
        }
        return file;
    };

    const QJsonObject sourceArtifact = downloads.value(QStringLiteral("artifact")).toObject();
    const bool nativeOnly = !windowsClassifier.isEmpty() && sourceArtifact.isEmpty();
    if (!nativeOnly) {
        const QJsonObject artifact = normalizeFile(sourceArtifact, {}, true);
        if (artifact.isEmpty()) return {};
        downloads.insert(QStringLiteral("artifact"), artifact);
    } else {
        downloads.remove(QStringLiteral("artifact"));
    }

    if (!windowsClassifier.isEmpty()) {
        QJsonObject classifiers = downloads.value(QStringLiteral("classifiers")).toObject();
        const QJsonObject nativeFile = normalizeFile(classifiers.value(windowsClassifier).toObject(), windowsClassifier, false);
        if (nativeFile.isEmpty()) return {};
        classifiers.insert(windowsClassifier, nativeFile);
        downloads.insert(QStringLiteral("classifiers"), classifiers);
    }

    result.insert(QStringLiteral("downloads"), downloads);
    return result;
}

void LoaderInstallService::scheduleProfileLibraries()
{
    const auto enqueueProfileFile = [this](const QJsonObject &library, const QJsonObject &file,
                                           const QString &taskPart, const QString &titlePrefix) {
        const QString relativePath = file.value(QStringLiteral("path")).toString();
        const QUrl url(file.value(QStringLiteral("url")).toString());
        if (relativePath.isEmpty() || !url.isValid() || url.scheme() != QStringLiteral("https")) return;
        const QString destination = libraryDestination(relativePath);
        const QString sha1 = file.value(QStringLiteral("sha1")).toString();
        if ((!sha1.isEmpty() && sha1Matches(destination, sha1)) || (sha1.isEmpty() && QFileInfo(destination).isFile())) return;

        DownloadRequest request;
        request.id = newTaskId(taskPart);
        request.title = QStringLiteral("%1: %2%3")
            .arg(loaderKindToString(m_job.instance.loader.kind), titlePrefix, library.value(QStringLiteral("name")).toString());
        request.url = url;
        request.destinationPath = destination;
        request.checksum = sha1;
        request.checksumAlgorithm = sha1.isEmpty() ? ChecksumAlgorithm::None : ChecksumAlgorithm::Sha1;
        request.expectedSize = file.contains(QStringLiteral("size"))
            ? file.value(QStringLiteral("size")).toVariant().toLongLong() : -1;
        m_job.pendingTaskIds.insert(request.id);
        m_downloadManager->enqueue(request);
    };

    for (const QJsonValue &value : m_job.mergedProfile.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject library = value.toObject();
        if (!libraryAllowedOnWindows(library)) continue;
        const QJsonObject downloads = library.value(QStringLiteral("downloads")).toObject();
        enqueueProfileFile(library, downloads.value(QStringLiteral("artifact")).toObject(),
                           QStringLiteral("loader-library"), QString());

        QString classifier = library.value(QStringLiteral("natives")).toObject().value(QStringLiteral("windows")).toString();
        classifier.replace(QStringLiteral("${arch}"), QStringLiteral("64"));
        if (!classifier.isEmpty()) {
            const QJsonObject nativeFile = downloads.value(QStringLiteral("classifiers")).toObject().value(classifier).toObject();
            enqueueProfileFile(library, nativeFile, QStringLiteral("loader-native"), QStringLiteral("Windows-библиотека "));
        }
    }
    if (m_job.pendingTaskIds.isEmpty()) {
        finishIfComplete();
        return;
    }
    m_downloadManager->start();
}

void LoaderInstallService::finishIfComplete()
{
    if (!isInstalling() || !m_job.pendingTaskIds.isEmpty()) return;
    const QString directory = versionDirectory(m_job.profileId);
    if (!QDir().mkpath(directory)) {
        failInstall(QStringLiteral("Не удалось создать папку launcher profile: %1").arg(directory));
        return;
    }
    const QString path = QDir(directory).filePath(m_job.profileId + QStringLiteral(".json"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        failInstall(QStringLiteral("Не удалось сохранить launcher profile: %1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(m_job.mergedProfile).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        failInstall(QStringLiteral("Не удалось завершить сохранение launcher profile: %1").arg(file.errorString()));
        return;
    }
    QString markerError;
    if (!finalizeInstalledProfile(m_job.profileId, &markerError)) {
        failInstall(markerError);
        return;
    }
    const QString instanceId = m_job.instance.id;
    const LoaderKind kind = m_job.instance.loader.kind;
    const QString loaderVersion = m_job.resolvedLoaderVersion;
    const QString profileId = m_job.profileId;
    Logger::info(QStringLiteral("Installed %1 %2 for Minecraft %3 (profile %4)")
                 .arg(loaderKindToString(kind), loaderVersion, m_job.instance.minecraftVersion, profileId));
    m_job = {};
    emit installFinished(instanceId, kind, loaderVersion, profileId);
}

bool LoaderInstallService::finalizeInstalledProfile(const QString &profileId, QString *error)
{
    const QString metadataPath = QDir(versionDirectory(profileId)).filePath(profileId + QStringLiteral(".json"));
    QFile metadata(metadataPath);
    if (!metadata.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Launcher profile %1 не найден после установки.").arg(profileId);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll(), &parseError);
    const QJsonObject profile = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || profile.value(QStringLiteral("id")).toString().isEmpty()
        || profile.value(QStringLiteral("mainClass")).toString().isEmpty()) {
        if (error) *error = QStringLiteral("Созданный launcher profile %1 повреждён или неполон.").arg(profileId);
        return false;
    }
    const QString markerDirectory = QDir(m_job.instance.rootPath).filePath(QStringLiteral(".atlas"));
    if (!QDir().mkpath(markerDirectory)) {
        if (error) *error = QStringLiteral("Не удалось создать папку marker-файла экземпляра: %1").arg(markerDirectory);
        return false;
    }
    QSaveFile marker(QDir(markerDirectory).filePath(QStringLiteral("loader-profile.json")));
    if (!marker.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Не удалось сохранить marker установленного загрузчика: %1").arg(marker.errorString());
        return false;
    }
    const QJsonObject markerObject{
        {QStringLiteral("kind"), loaderKindToString(m_job.instance.loader.kind)},
        {QStringLiteral("minecraft"), m_job.instance.minecraftVersion},
        {QStringLiteral("loaderVersion"), m_job.resolvedLoaderVersion},
        {QStringLiteral("profileId"), profileId}
    };
    marker.write(QJsonDocument(markerObject).toJson(QJsonDocument::Compact));
    if (!marker.commit()) {
        if (error) *error = QStringLiteral("Не удалось завершить marker установленного загрузчика: %1").arg(marker.errorString());
        return false;
    }
    return true;
}

void LoaderInstallService::failInstall(const QString &message)
{
    if (!isInstalling()) return;
    const QString instanceId = m_job.instance.id;
    const QSet<QString> pending = m_job.pendingTaskIds;
    m_job = {};
    for (const QString &taskId : pending) m_downloadManager->cancel(taskId);
    emit installError(instanceId, message);
}

void LoaderInstallService::onTaskChanged(const DownloadTask &task)
{
    if (!isInstalling()) return;
    if (task.request.id == m_job.installerTaskId) {
        if (task.state == DownloadState::Failed || task.state == DownloadState::Cancelled) {
            failInstall(QStringLiteral("Не удалось скачать официальный installer %1: %2")
                        .arg(loaderKindToString(m_job.instance.loader.kind), task.error));
            return;
        }
        if (task.state == DownloadState::Completed) launchInstaller();
        return;
    }
    if (!m_job.pendingTaskIds.contains(task.request.id)) return;
    if (task.state == DownloadState::Failed || task.state == DownloadState::Cancelled) {
        failInstall(QStringLiteral("Не удалось скачать библиотеку загрузчика «%1»: %2").arg(task.request.title, task.error));
        return;
    }
    if (task.state != DownloadState::Completed) return;
    m_job.pendingTaskIds.remove(task.request.id);
    finishIfComplete();
}

bool LoaderInstallService::libraryAllowedOnWindows(const QJsonObject &library) const
{
    return rulesAllowOnWindows(library.value(QStringLiteral("rules")).toArray());
}

QString LoaderInstallService::mavenPath(const QString &coordinate) const
{
    const QStringList parts = coordinate.split(QLatin1Char(':'));
    if (parts.size() < 3) return {};
    QString group = parts.at(0);
    group.replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString artifact = parts.at(1);
    QString version = parts.at(2);
    QString extension = QStringLiteral("jar");
    const int at = version.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        extension = version.mid(at + 1);
        version = version.left(at);
    }
    const QString classifier = parts.size() >= 4 ? parts.at(3) : QString();
    QString fileName = artifact + QLatin1Char('-') + version;
    if (!classifier.isEmpty()) fileName += QLatin1Char('-') + classifier;
    fileName += QLatin1Char('.') + extension;
    return group + QLatin1Char('/') + artifact + QLatin1Char('/') + version + QLatin1Char('/') + fileName;
}

QString LoaderInstallService::gameDirectory() const
{
    return QDir(m_dataDirectory).filePath(QStringLiteral("game"));
}

QString LoaderInstallService::versionDirectory(const QString &version) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("versions/%1").arg(version));
}

QString LoaderInstallService::libraryDestination(const QString &path) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("libraries/%1").arg(path));
}

QString LoaderInstallService::newTaskId(const QString &part)
{
    return QStringLiteral("loader-%1-%2-%3").arg(part, QUuid::createUuid().toString(QUuid::WithoutBraces)).arg(++m_sequence);
}

} // namespace atlas
