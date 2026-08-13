#include "services/java_runtime_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QUrlQuery>

namespace atlas {
namespace {
constexpr auto kAdoptiumApiBase = "https://api.adoptium.net/v3/assets/latest/";
constexpr auto kUserAgent = "AtlasLauncher/0.2 (Windows 7+; Java runtime installer)";

QString runtimeMetadataPath(const QString &runtimeDirectory)
{
    return QDir(runtimeDirectory).filePath(QStringLiteral("runtime.json"));
}
}

bool JavaRuntimeInfo::isValid() const
{
    return major > 0 && !javawPath.isEmpty() && QFileInfo::exists(javawPath);
}

JavaRuntimeService::JavaRuntimeService(const QString &dataDirectory, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_dataDirectory(dataDirectory)
    , m_downloads(downloads)
    , m_network(new QNetworkAccessManager(this))
{
    qRegisterMetaType<JavaRuntimeInfo>("atlas::JavaRuntimeInfo");
    qRegisterMetaType<JavaExecutableInfo>("atlas::JavaExecutableInfo");
    if (m_downloads) connect(m_downloads, &DownloadManager::taskChanged, this, &JavaRuntimeService::onDownloadTaskChanged);
#ifdef Q_OS_WIN
    m_extractorPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/7za.exe"));
#endif
}

QString JavaRuntimeService::runtimeRoot() const
{
    return QDir(m_dataDirectory).filePath(QStringLiteral("runtime"));
}

JavaRuntimeInfo JavaRuntimeService::installedRuntime(int major) const
{
    JavaRuntimeInfo result;
    result.major = major;
    result.runtimeDirectory = finalDirectoryFor(major);
    const QString metadataPath = runtimeMetadataPath(result.runtimeDirectory);
    QFile metadata(metadataPath);
    if (metadata.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
        const QJsonObject object = document.object();
        result.javawPath = object.value(QStringLiteral("javawPath")).toString();
        if (result.javawPath.isEmpty()) result.javawPath = object.value(QStringLiteral("javaPath")).toString();
        result.archiveName = object.value(QStringLiteral("archiveName")).toString();
        result.checksumSha256 = object.value(QStringLiteral("checksumSha256")).toString();
    }
    if (!result.isValid()) {
        QDirIterator iterator(result.runtimeDirectory, {QStringLiteral("javaw.exe")}, QDir::Files, QDirIterator::Subdirectories);
        if (iterator.hasNext()) result.javawPath = iterator.next();
    }
    return result;
}

bool JavaRuntimeService::isRuntimeReady(int major) const
{
    return installedRuntime(major).isValid();
}

void JavaRuntimeService::ensureRuntime(int major)
{
    if (major <= 0) {
        fail(major, QStringLiteral("Указана недопустимая версия Java."));
        return;
    }
    if (isRuntimeReady(major)) {
        emit runtimeReady(installedRuntime(major));
        return;
    }
    if (m_queryReply || !m_pendingTaskId.isEmpty() || m_extractor) {
        fail(major, QStringLiteral("Установка другой Java Runtime уже выполняется."));
        return;
    }

    QUrl url(QString::fromLatin1(kAdoptiumApiBase) + QString::number(major) + QStringLiteral("/hotspot"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("architecture"), QStringLiteral("x64"));
    query.addQueryItem(QStringLiteral("image_type"), QStringLiteral("jre"));
    query.addQueryItem(QStringLiteral("os"), QStringLiteral("windows"));
    query.addQueryItem(QStringLiteral("vendor"), QStringLiteral("eclipse"));
    url.setQuery(query);

    m_pendingMajor = major;
    emit runtimeQueryStarted(major);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setTransferTimeout(60000);
    m_queryReply = m_network->get(request);
    connect(m_queryReply, &QNetworkReply::finished, this, &JavaRuntimeService::onRuntimeManifestFinished);
}

void JavaRuntimeService::setExtractorPath(const QString &extractorPath)
{
    m_extractorPath = extractorPath;
}

int JavaRuntimeService::recommendedJavaMajor(int minecraftJavaMajor)
{
    if (minecraftJavaMajor <= 8) return 8;
    if (minecraftJavaMajor <= 11) return 11;
    if (minecraftJavaMajor <= 17) return 17;
    return 21;
}

JavaExecutableInfo JavaRuntimeService::inspectExecutable(const QString &javaPath, QString *error)
{
    const QFileInfo selected(javaPath.trimmed());
    if (!selected.exists() || !selected.isFile()) {
        if (error) *error = QStringLiteral("Не найден исполняемый файл Java: %1").arg(javaPath);
        return {};
    }

    // javaw.exe is right for launching Minecraft, but java.exe reliably writes -version output.
    QString probePath = selected.absoluteFilePath();
    if (selected.fileName().compare(QStringLiteral("javaw.exe"), Qt::CaseInsensitive) == 0) {
        const QString consoleJava = QDir(selected.absolutePath()).filePath(QStringLiteral("java.exe"));
        if (QFileInfo::exists(consoleJava)) probePath = consoleJava;
    }

    QProcess process;
    process.setProgram(probePath);
    process.setArguments({QStringLiteral("-version")});
    process.start();
    if (!process.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Не удалось запустить Java для проверки: %1").arg(process.errorString());
        return {};
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) *error = QStringLiteral("Проверка Java превысила лимит времени.");
        return {};
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput() + process.readAllStandardError());
    const QRegularExpression versionExpression(
        QStringLiteral("(?:java|openjdk)[^\\r\\n\\\"]*?(?:version\\s+)?\\\"?([0-9]+(?:\\.[0-9]+)*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = versionExpression.match(output);
    if (!match.hasMatch()) {
        if (error) *error = QStringLiteral("Не удалось определить версию Java из ответа: %1")
            .arg(output.trimmed().isEmpty() ? QStringLiteral("нет вывода") : output.trimmed());
        return {};
    }

    const QString version = match.captured(1);
    const QStringList parts = version.split(QLatin1Char('.'));
    int major = parts.value(0).toInt();
    if (major == 1 && parts.size() > 1) major = parts.at(1).toInt();
    if (major <= 0) {
        if (error) *error = QStringLiteral("Версия Java имеет недопустимый формат: %1").arg(version);
        return {};
    }

    JavaExecutableInfo result;
    result.executablePath = selected.absoluteFilePath();
    result.major = major;
    result.versionText = version;
    return result;
}

bool JavaRuntimeService::isCompatibleMajor(int selectedMajor, int requiredMajor)
{
    // Minecraft metadata describes an exact Java feature release. Keeping this strict avoids
    // starting older game versions with an incompatible newer runtime and vice versa.
    return selectedMajor > 0 && requiredMajor > 0 && selectedMajor == requiredMajor;
}

void JavaRuntimeService::onRuntimeManifestFinished()
{
    if (!m_queryReply) return;
    const int major = m_pendingMajor;
    const auto networkError = m_queryReply->error();
    const QString errorText = m_queryReply->errorString();
    const QByteArray payload = m_queryReply->readAll();
    m_queryReply->deleteLater();
    m_queryReply = nullptr;

    if (networkError != QNetworkReply::NoError) {
        fail(major, QStringLiteral("Не удалось получить Java Runtime: %1").arg(errorText));
        return;
    }
    QString error;
    const JavaRuntimeInfo info = parseRuntimeInfo(major, payload, &error);
    if (info.archiveName.isEmpty() || info.checksumSha256.isEmpty()) {
        fail(major, error.isEmpty() ? QStringLiteral("API Java Runtime вернул неполные данные.") : error);
        return;
    }
    m_pendingRuntime = info;
    const QString archivePath = archivePathFor(major, info.archiveName);
    m_pendingRuntime.runtimeDirectory = finalDirectoryFor(major);
    m_pendingTaskId = m_downloads->enqueue({
        QStringLiteral("java-%1").arg(major),
        QStringLiteral("Java %1 Runtime").arg(major),
        info.packageUrl,
        QUrl(),
        archivePath,
        info.checksumSha256,
        ChecksumAlgorithm::Sha256,
        info.archiveSize
    });
    emit runtimeDownloadQueued(m_pendingRuntime);
    m_downloads->start();
}

JavaRuntimeInfo JavaRuntimeService::parseRuntimeInfo(int major, const QByteArray &payload, QString *error) const
{
    JavaRuntimeInfo result;
    result.major = major;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isArray() || document.array().isEmpty()) {
        if (error) *error = QStringLiteral("Для Java %1 не найден подходящий x64 JRE ZIP.").arg(major);
        return result;
    }
    const QJsonObject release = document.array().at(0).toObject();
    const QJsonObject binary = release.value(QStringLiteral("binary")).toObject();
    const QJsonObject package = binary.value(QStringLiteral("package")).toObject();
    const QString link = package.value(QStringLiteral("link")).toString();
    result.archiveName = package.value(QStringLiteral("name")).toString();
    result.checksumSha256 = package.value(QStringLiteral("checksum")).toString();
    result.archiveSize = static_cast<qint64>(package.value(QStringLiteral("size")).toDouble(-1));
    if (link.isEmpty() || result.archiveName.isEmpty() || result.checksumSha256.isEmpty()) {
        if (error) *error = QStringLiteral("Ответ Java Runtime не содержит ссылки, имени или SHA-256.");
        return {};
    }
    result.packageUrl = QUrl(link);
    if (!result.packageUrl.isValid() || result.packageUrl.scheme() != QStringLiteral("https")) {
        if (error) *error = QStringLiteral("Ответ Java Runtime содержит недопустимый адрес архива.");
        return {};
    }
    return result;
}

QString JavaRuntimeService::archivePathFor(int major, const QString &archiveName) const
{
    return QDir(runtimeRoot()).filePath(QStringLiteral("downloads/java-%1/%2").arg(major).arg(archiveName));
}

QString JavaRuntimeService::stagingDirectoryFor(int major) const
{
    return QDir(runtimeRoot()).filePath(QStringLiteral(".staging-%1").arg(major));
}

QString JavaRuntimeService::finalDirectoryFor(int major) const
{
    return QDir(runtimeRoot()).filePath(QString::number(major));
}

bool JavaRuntimeService::activateExtractedRuntime(int major, QString *error)
{
    const QString staging = stagingDirectoryFor(major);
    QDirIterator iterator(staging, {QStringLiteral("javaw.exe")}, QDir::Files, QDirIterator::Subdirectories);
    if (!iterator.hasNext()) {
        if (error) *error = QStringLiteral("В распакованной Java Runtime не найден bin\\javaw.exe.");
        return false;
    }
    const QString javawInStaging = iterator.next();
    const QString finalDirectory = finalDirectoryFor(major);
    QDir finalDir(finalDirectory);
    if (finalDir.exists() && !finalDir.removeRecursively()) {
        if (error) *error = QStringLiteral("Не удалось заменить старую Java Runtime: %1").arg(finalDirectory);
        return false;
    }
    if (!QDir().rename(staging, finalDirectory)) {
        if (error) *error = QStringLiteral("Не удалось активировать распакованную Java Runtime.");
        return false;
    }

    const QString javawPath = finalDirectory + javawInStaging.mid(staging.length());
    QJsonObject metadata;
    metadata.insert(QStringLiteral("major"), major);
    metadata.insert(QStringLiteral("javawPath"), QDir::toNativeSeparators(javawPath));
    metadata.insert(QStringLiteral("archiveName"), m_pendingRuntime.archiveName);
    metadata.insert(QStringLiteral("checksumSha256"), m_pendingRuntime.checksumSha256);
    QFile metadataFile(runtimeMetadataPath(finalDirectory));
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Java распакована, но не удалось записать metadata: %1").arg(metadataFile.errorString());
        return false;
    }
    metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    return true;
}

void JavaRuntimeService::onDownloadTaskChanged(const DownloadTask &task)
{
    if (task.request.id != m_pendingTaskId) return;
    if (task.state == DownloadState::Failed || task.state == DownloadState::Cancelled) {
        fail(m_pendingMajor, task.error.isEmpty() ? QStringLiteral("Загрузка Java Runtime отменена.") : task.error);
        return;
    }
    if (task.state != DownloadState::Completed) return;
#ifdef Q_OS_WIN
    if (!QFileInfo::exists(m_extractorPath)) {
        fail(m_pendingMajor, QStringLiteral("Не найден встроенный распаковщик Java: %1").arg(m_extractorPath));
        return;
    }
    const QString staging = stagingDirectoryFor(m_pendingMajor);
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        fail(m_pendingMajor, QStringLiteral("Не удалось создать временную папку Java Runtime."));
        return;
    }
    emit runtimeInstallStarted(m_pendingMajor);
    m_extractor = new QProcess(this);
    m_extractor->setProgram(m_extractorPath);
    m_extractor->setArguments({QStringLiteral("x"), task.request.destinationPath, QStringLiteral("-y"), QStringLiteral("-o%1").arg(staging)});
    connect(m_extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &JavaRuntimeService::onExtractorFinished);
    m_extractor->start();
#else
    fail(m_pendingMajor, QStringLiteral("Локальная установка Windows Java Runtime доступна только в Windows-сборке лаунчера."));
#endif
}

void JavaRuntimeService::onExtractorFinished(int exitCode, QProcess::ExitStatus status)
{
    const int major = m_pendingMajor;
    const QString errorOutput = m_extractor ? QString::fromLocal8Bit(m_extractor->readAllStandardError()).trimmed() : QString();
    if (status != QProcess::NormalExit || exitCode != 0) {
        fail(major, QStringLiteral("Не удалось распаковать Java Runtime%1").arg(errorOutput.isEmpty() ? QString() : QStringLiteral(": %1").arg(errorOutput)));
        return;
    }
    QString error;
    if (!activateExtractedRuntime(major, &error)) {
        fail(major, error);
        return;
    }
    const JavaRuntimeInfo info = installedRuntime(major);
    m_pendingTaskId.clear();
    m_pendingMajor = 0;
    m_pendingRuntime = {};
    m_extractor->deleteLater();
    m_extractor = nullptr;
    emit runtimeReady(info);
}

void JavaRuntimeService::fail(int major, const QString &message)
{
    m_pendingTaskId.clear();
    m_pendingMajor = 0;
    m_pendingRuntime = {};
    if (m_extractor) {
        m_extractor->deleteLater();
        m_extractor = nullptr;
    }
    emit runtimeError(major, message);
}

} // namespace atlas
