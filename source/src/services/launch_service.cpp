#include "services/launch_service.h"

#include "infrastructure/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

namespace atlas {
namespace {

QStringList splitMinecraftArguments(const QString &input)
{
    QStringList result;
    QString current;
    bool quoted = false;
    bool escaped = false;
    for (const QChar character : input) {
        if (escaped) {
            current.append(character);
            escaped = false;
        } else if (character == QLatin1Char('\\')) {
            escaped = true;
        } else if (character == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (character.isSpace() && !quoted) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
        } else {
            current.append(character);
        }
    }
    if (escaped) current.append(QLatin1Char('\\'));
    if (!current.isEmpty()) result.append(current);
    return result;
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
        if (!osName.isEmpty() && osName != QStringLiteral("windows")) matches = false;
        const QString arch = os.value(QStringLiteral("arch")).toString();
        if (!arch.isEmpty() && arch != QStringLiteral("x86_64")) matches = false;
        if (!rule.value(QStringLiteral("features")).toObject().isEmpty()) matches = false;
        if (matches) allowed = rule.value(QStringLiteral("action")).toString() == QStringLiteral("allow");
    }
    return allowed;
}

QStringList metadataArgumentValues(const QJsonArray &entries)
{
    QStringList values;
    for (const QJsonValue &entry : entries) {
        if (entry.isString()) {
            values.append(entry.toString());
            continue;
        }
        const QJsonObject object = entry.toObject();
        if (!rulesAllowOnWindows(object.value(QStringLiteral("rules")).toArray())) continue;
        const QJsonValue value = object.value(QStringLiteral("value"));
        if (value.isString()) values.append(value.toString());
        else for (const QJsonValue &item : value.toArray()) values.append(item.toString());
    }
    return values;
}

QString safePathSegment(const QString &value)
{
    QString safe = value;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    return safe.isEmpty() ? QStringLiteral("default") : safe;
}

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

QString libraryRelativePath(const QJsonObject &library)
{
    const QString artifactPath = library.value(QStringLiteral("downloads")).toObject()
        .value(QStringLiteral("artifact")).toObject().value(QStringLiteral("path")).toString();
    return artifactPath.isEmpty() ? mavenPath(library.value(QStringLiteral("name")).toString()) : artifactPath;
}

QJsonArray mergedLibraries(const QJsonArray &baseLibraries, const QJsonArray &profileLibraries)
{
    QJsonArray result;
    QSet<QString> names;
    for (const QJsonValue &value : profileLibraries) {
        result.append(value);
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) names.insert(name);
    }
    for (const QJsonValue &value : baseLibraries) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && names.contains(name)) continue;
        result.append(value);
    }
    return result;
}

QJsonObject mergedArguments(const QJsonObject &baseArguments, const QJsonObject &profileArguments)
{
    QJsonObject result = baseArguments;
    for (auto it = profileArguments.constBegin(); it != profileArguments.constEnd(); ++it) {
        if (!it.value().isArray()) {
            result.insert(it.key(), it.value());
            continue;
        }
        QJsonArray values = baseArguments.value(it.key()).toArray();
        for (const QJsonValue &value : it.value().toArray()) values.append(value);
        result.insert(it.key(), values);
    }
    return result;
}

QJsonObject resolveInheritedProfile(const QJsonObject &base, const QJsonObject &profile)
{
    QJsonObject resolved = base;
    for (auto it = profile.constBegin(); it != profile.constEnd(); ++it) {
        if (it.key() == QStringLiteral("libraries") || it.key() == QStringLiteral("arguments")) continue;
        resolved.insert(it.key(), it.value());
    }
    resolved.insert(QStringLiteral("libraries"), mergedLibraries(base.value(QStringLiteral("libraries")).toArray(),
                                                                     profile.value(QStringLiteral("libraries")).toArray()));
    resolved.insert(QStringLiteral("arguments"), mergedArguments(base.value(QStringLiteral("arguments")).toObject(),
                                                                     profile.value(QStringLiteral("arguments")).toObject()));
    return resolved;
}

} // namespace

LaunchService::LaunchService(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_dataDirectory(QDir::cleanPath(dataDirectory)), m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::started, this, &LaunchService::onProcessStarted);
    connect(m_process, &QProcess::readyRead, this, &LaunchService::onProcessReadyRead);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &LaunchService::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &LaunchService::onProcessError);
}

bool LaunchService::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString LaunchService::runningInstanceId() const
{
    return m_runningInstanceId;
}

void LaunchService::launch(const LaunchOptions &options)
{
    if (isRunning()) {
        emit launchError(options.instance.id, QStringLiteral("Minecraft уже запущен для профиля «%1». Сначала закройте текущую игру.").arg(m_runningInstanceId));
        return;
    }
    QString javaPath;
    QStringList arguments;
    QString error;
    if (!prepareLaunch(options, &javaPath, &arguments, &error)) {
        emit launchError(options.instance.id, error);
        return;
    }
    // Minecraft is intentionally detached from Atlas. This prevents a launcher
    // exit, window close, crash or update from terminating the Java game process.
    qint64 processId = 0;
    if (!QProcess::startDetached(javaPath, arguments, options.instance.rootPath, &processId)) {
        emit launchError(options.instance.id,
                         QStringLiteral("Не удалось создать отдельный процесс Java для Minecraft."));
        return;
    }
    m_runningInstanceId.clear();
    m_outputBuffer.clear();
    Logger::info(QStringLiteral("Launching detached Minecraft %1 for instance %2 (PID %3)")
                     .arg(options.instance.minecraftVersion, options.instance.id, QString::number(processId)));
    emit launchStarted(options.instance.id, processId);
}

void LaunchService::stop()
{
    // Minecraft is launched detached so Atlas never owns or terminates the game.
    // The user closes the game normally from Minecraft itself.
}

bool LaunchService::prepareLaunch(const LaunchOptions &options, QString *javaPath, QStringList *arguments, QString *error)
{
    if (options.instance.id.isEmpty() || options.instance.rootPath.isEmpty() || options.instance.minecraftVersion.isEmpty()) {
        if (error) *error = QStringLiteral("Выбранный экземпляр не содержит обязательных параметров запуска.");
        return false;
    }
    if (options.instance.loader.kind != LoaderKind::Vanilla &&
        options.instance.loader.kind != LoaderKind::Fabric &&
        options.instance.loader.kind != LoaderKind::LegacyFabric &&
        options.instance.loader.kind != LoaderKind::Quilt &&
        options.instance.loader.kind != LoaderKind::Forge &&
        options.instance.loader.kind != LoaderKind::NeoForge) {
        if (error) *error = QStringLiteral("Запуск %1 пока недоступен: этот загрузчик ещё не установлен безопасным модулем Atlas.")
            .arg(loaderKindToString(options.instance.loader.kind));
        return false;
    }
    if (!options.account.isValidForLaunch()) {
        if (error) *error = QStringLiteral("Профиль игрока неполный. Выберите офлайн-профиль или завершите вход Microsoft.");
        return false;
    }
    const QString resolvedJava = resolveJava(options);
    if (resolvedJava.isEmpty() || !QFileInfo::exists(resolvedJava)) {
        if (error) *error = QStringLiteral("Не найдена готовая Java для запуска. Установите локальную Java в настройках Atlas.");
        return false;
    }
    QDir().mkpath(options.instance.rootPath);
    QJsonObject metadata;
    if (!loadVersionMetadata(options.instance, &metadata, error)) return false;
    QString nativesDirectory;
    if (!prepareNatives(options.instance, metadata, &nativesDirectory, error)) return false;
    QString classpathError;
    const QStringList classpath = classpathFor(metadata, &classpathError);
    if (classpath.isEmpty()) {
        if (error) *error = classpathError;
        return false;
    }
    const QStringList launchArguments = argumentsFor(options, metadata, nativesDirectory, classpath, error);
    if (launchArguments.isEmpty()) return false;
    if (javaPath) *javaPath = resolvedJava;
    if (arguments) *arguments = launchArguments;
    return true;
}

bool LaunchService::loadVersionMetadata(const Instance &instance, QJsonObject *metadata, QString *error) const
{
    QString versionId = instance.minecraftVersion;
    if (instance.loader.kind == LoaderKind::Fabric || instance.loader.kind == LoaderKind::LegacyFabric ||
        instance.loader.kind == LoaderKind::Quilt || instance.loader.kind == LoaderKind::Forge ||
        instance.loader.kind == LoaderKind::NeoForge) {
        const QString markerPath = QDir(instance.rootPath).filePath(QStringLiteral(".atlas/loader-profile.json"));
        QFile markerFile(markerPath);
        if (!markerFile.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("%1 ещё не установлен для этого экземпляра. В библиотеке нажмите «Установить %2».")
                .arg(loaderKindToString(instance.loader.kind), loaderKindToString(instance.loader.kind));
            return false;
        }
        QJsonParseError markerError;
        const QJsonDocument markerDocument = QJsonDocument::fromJson(markerFile.readAll(), &markerError);
        const QJsonObject marker = markerDocument.object();
        if (markerError.error != QJsonParseError::NoError || !markerDocument.isObject() ||
            marker.value(QStringLiteral("minecraft")).toString() != instance.minecraftVersion ||
            marker.value(QStringLiteral("kind")).toString() != loaderKindToString(instance.loader.kind)) {
            if (error) *error = QStringLiteral("Маркер установленного %1 повреждён или относится к другой версии Minecraft. Повторите установку загрузчика.")
                .arg(loaderKindToString(instance.loader.kind));
            return false;
        }
        versionId = marker.value(QStringLiteral("profileId")).toString();
        if (versionId.isEmpty()) {
            if (error) *error = QStringLiteral("Маркер установленного %1 не содержит launcher profile. Повторите установку загрузчика.")
                .arg(loaderKindToString(instance.loader.kind));
            return false;
        }
    }
    const QString path = QDir(versionDirectory(versionId)).filePath(versionId + QStringLiteral(".json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Метаданные версии %1 не установлены. Установите Vanilla или выбранный загрузчик в библиотеке.").arg(versionId);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Метаданные Minecraft повреждены: %1").arg(parseError.errorString());
        return false;
    }
    QJsonObject resolved = document.object();
    const QString inheritedVersion = resolved.value(QStringLiteral("inheritsFrom")).toString();
    if (!inheritedVersion.isEmpty() && inheritedVersion != resolved.value(QStringLiteral("id")).toString()) {
        const QString inheritedPath = QDir(versionDirectory(inheritedVersion))
            .filePath(inheritedVersion + QStringLiteral(".json"));
        QFile inheritedFile(inheritedPath);
        if (!inheritedFile.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Не найдены базовые метаданные Vanilla %1, необходимые для %2. Повторите установку профиля.")
                .arg(inheritedVersion, versionId);
            return false;
        }
        QJsonParseError inheritedError;
        const QJsonDocument inheritedDocument = QJsonDocument::fromJson(inheritedFile.readAll(), &inheritedError);
        if (inheritedError.error != QJsonParseError::NoError || !inheritedDocument.isObject()) {
            if (error) *error = QStringLiteral("Базовые метаданные Vanilla %1 повреждены: %2")
                .arg(inheritedVersion, inheritedError.errorString());
            return false;
        }
        resolved = resolveInheritedProfile(inheritedDocument.object(), resolved);
    }
    if (metadata) *metadata = resolved;
    return true;
}

bool LaunchService::prepareNatives(const Instance &instance, const QJsonObject &metadata, QString *nativesDirectory, QString *error) const
{
    const QString nativeRoot = QDir(instance.rootPath).filePath(QStringLiteral(".atlas/natives/%1").arg(safePathSegment(metadata.value(QStringLiteral("id")).toString())));
    QDir nativeDir(nativeRoot);
    if (nativeDir.exists() && !nativeDir.removeRecursively()) {
        if (error) *error = QStringLiteral("Не удалось очистить временную папку natives: %1").arg(nativeRoot);
        return false;
    }
    if (!QDir().mkpath(nativeRoot)) {
        if (error) *error = QStringLiteral("Не удалось создать папку natives: %1").arg(nativeRoot);
        return false;
    }
    const QString extractor = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/7za.exe"));
    bool needsExtractor = false;
    QStringList nativeArchives;
    for (const QJsonValue &value : metadata.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject library = value.toObject();
        if (!libraryAllowedOnWindows(library)) continue;
        const QString archive = nativeArchiveFor(library);
        if (!archive.isEmpty()) {
            needsExtractor = true;
            nativeArchives.append(archive);
        }
    }
    if (needsExtractor && !QFileInfo::exists(extractor)) {
        if (error) *error = QStringLiteral("Не найден встроенный распаковщик natives: %1. Переустановите Atlas Launcher.").arg(extractor);
        return false;
    }
    for (const QString &archive : nativeArchives) {
        if (!QFileInfo::exists(archive)) {
            if (error) *error = QStringLiteral("Не установлена Windows-библиотека Minecraft: %1. Повторите установку Vanilla.").arg(archive);
            return false;
        }
        QProcess extractorProcess;
        extractorProcess.start(extractor, {QStringLiteral("x"), QStringLiteral("-y"), archive, QStringLiteral("-o%1").arg(nativeRoot)});
        if (!extractorProcess.waitForStarted(10000) || !extractorProcess.waitForFinished(60000) || extractorProcess.exitCode() != 0) {
            if (error) *error = QStringLiteral("Не удалось распаковать natives из %1: %2")
                .arg(QFileInfo(archive).fileName(), QString::fromLocal8Bit(extractorProcess.readAllStandardError()));
            return false;
        }
    }
    QDir(QDir(nativeRoot).filePath(QStringLiteral("META-INF"))).removeRecursively();
    if (nativesDirectory) *nativesDirectory = nativeRoot;
    return true;
}

QStringList LaunchService::classpathFor(const QJsonObject &metadata, QString *error) const
{
    QStringList entries;
    QSet<QString> uniquePaths;
    const QString id = metadata.value(QStringLiteral("id")).toString();
    const auto appendUnique = [&entries, &uniquePaths](const QString &path) {
        const QString normalized = QDir::cleanPath(QDir::toNativeSeparators(path));
        if (!uniquePaths.contains(normalized)) {
            uniquePaths.insert(normalized);
            entries.append(normalized);
        }
    };
    for (const QJsonValue &value : metadata.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject library = value.toObject();
        if (!libraryAllowedOnWindows(library)) continue;
        const QString relative = libraryRelativePath(library);
        if (relative.isEmpty()) continue;
        const QString path = QDir(gameDirectory()).filePath(QStringLiteral("libraries/%1").arg(relative));
        if (!QFileInfo(path).isFile()) {
            if (error) *error = QStringLiteral("Отсутствует библиотека Minecraft: %1. Повторите установку Vanilla.").arg(relative);
            return {};
        }
        appendUnique(path);
    }

    // NeoForge 20.2+ generates a remapped Minecraft client in libraries.  Keeping
    // the original inherited Vanilla JAR alongside it makes BootstrapLauncher see
    // two modules exporting net.minecraft packages and abort before the game starts.
    const bool usesRemappedNeoForgeClient = id.startsWith(QStringLiteral("neoforge-")) &&
        metadata.value(QStringLiteral("mainClass")).toString() == QStringLiteral("cpw.mods.bootstraplauncher.BootstrapLauncher");
    if (usesRemappedNeoForgeClient) {
        Logger::info(QStringLiteral("Using NeoForge remapped client instead of inherited Vanilla client JAR for %1").arg(id));
        return entries;
    }

    const QString jarVersion = metadata.value(QStringLiteral("jar")).toString().isEmpty()
        ? metadata.value(QStringLiteral("inheritsFrom")).toString() : metadata.value(QStringLiteral("jar")).toString();
    const QString clientVersion = jarVersion.isEmpty() ? id : jarVersion;
    const QString clientJar = QDir(versionDirectory(clientVersion)).filePath(clientVersion + QStringLiteral(".jar"));
    if (id.isEmpty() || !QFileInfo(clientJar).isFile()) {
        if (error) *error = QStringLiteral("Не найден client JAR версии Minecraft. Повторите установку Vanilla.");
        return {};
    }
    appendUnique(clientJar);
    return entries;
}

QStringList LaunchService::argumentsFor(const LaunchOptions &options, const QJsonObject &metadata,
                                        const QString &nativesDirectory, const QStringList &classpath, QString *error) const
{
    const QString assetIndex = metadata.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
    const QString mainClass = metadata.value(QStringLiteral("mainClass")).toString();
    if (assetIndex.isEmpty() || mainClass.isEmpty()) {
        if (error) *error = QStringLiteral("Метаданные версии не содержат asset index или главный класс запуска.");
        return {};
    }
    const QString classpathText = classpath.join(QStringLiteral(";"));
    QString compactUuid = options.account.uuid;
    compactUuid.remove(QLatin1Char('-'));
    QHash<QString, QString> variables{
        {QStringLiteral("${auth_player_name}"), options.account.playerName},
        {QStringLiteral("${version_name}"), metadata.value(QStringLiteral("id")).toString()},
        {QStringLiteral("${game_directory}"), QDir::toNativeSeparators(options.instance.rootPath)},
        {QStringLiteral("${assets_root}"), QDir::toNativeSeparators(QDir(gameDirectory()).filePath(QStringLiteral("assets")))},
        {QStringLiteral("${assets_index_name}"), assetIndex},
        {QStringLiteral("${path}"), QDir::toNativeSeparators(QDir(gameDirectory()).filePath(
            QStringLiteral("assets/log_configs/%1").arg(metadata.value(QStringLiteral("logging")).toObject()
                .value(QStringLiteral("client")).toObject().value(QStringLiteral("file")).toObject()
                .value(QStringLiteral("id")).toString())))},
        {QStringLiteral("${auth_uuid}"), compactUuid},
        {QStringLiteral("${auth_access_token}"), options.account.accessToken},
        {QStringLiteral("${clientid}"), options.account.clientId},
        {QStringLiteral("${auth_xuid}"), options.account.xuid},
        {QStringLiteral("${user_type}"), options.account.isMicrosoft() ? QStringLiteral("msa") : QStringLiteral("legacy")},
        {QStringLiteral("${version_type}"), metadata.value(QStringLiteral("type")).toString()},
        {QStringLiteral("${natives_directory}"), QDir::toNativeSeparators(nativesDirectory)},
        {QStringLiteral("${launcher_name}"), QStringLiteral("Atlas Launcher")},
        {QStringLiteral("${launcher_version}"), QCoreApplication::applicationVersion()},
        {QStringLiteral("${classpath}"), classpathText},
        {QStringLiteral("${classpath_separator}"), QStringLiteral(";")},
        {QStringLiteral("${library_directory}"), QDir::toNativeSeparators(QDir(gameDirectory()).filePath(QStringLiteral("libraries")))},
        {QStringLiteral("${game_assets}"), QDir::toNativeSeparators(QDir(gameDirectory()).filePath(QStringLiteral("assets/virtual/%1").arg(assetIndex)))},
        {QStringLiteral("${resolution_width}"), QString::number(qBound(320, options.instance.resolutionWidth, 7680))},
        {QStringLiteral("${resolution_height}"), QString::number(qBound(240, options.instance.resolutionHeight, 4320))}
    };

    QStringList jvmArguments;
    const QJsonObject arguments = metadata.value(QStringLiteral("arguments")).toObject();
    if (!arguments.isEmpty()) {
        jvmArguments = metadataArgumentValues(arguments.value(QStringLiteral("jvm")).toArray());
    }
    if (jvmArguments.isEmpty()) {
        jvmArguments << QStringLiteral("-Djava.library.path=${natives_directory}")
                     << QStringLiteral("-cp") << QStringLiteral("${classpath}");
    }
    jvmArguments.prepend(QStringLiteral("-Xmx%1M").arg(qMax(256, options.maxMemoryMiB)));
    jvmArguments.prepend(QStringLiteral("-Xms%1M").arg(qMax(256, qMin(options.minMemoryMiB, options.maxMemoryMiB))));
    if (!options.instance.safeMode) {
        for (const QString &argument : options.extraJvmArguments) {
            if (!argument.trimmed().isEmpty()) jvmArguments.append(argument.trimmed());
        }
    }
    for (QString &argument : jvmArguments) argument = replaceVariables(argument, variables);

    QStringList gameArguments;
    if (!arguments.isEmpty()) gameArguments = metadataArgumentValues(arguments.value(QStringLiteral("game")).toArray());
    if (gameArguments.isEmpty()) gameArguments = splitMinecraftArguments(metadata.value(QStringLiteral("minecraftArguments")).toString());
    if (gameArguments.isEmpty()) {
        if (error) *error = QStringLiteral("Метаданные версии не содержат игровые аргументы запуска.");
        return {};
    }
    if (!options.instance.safeMode) {
        for (const QString &argument : options.extraGameArguments) {
            if (!argument.trimmed().isEmpty()) gameArguments.append(argument.trimmed());
        }
    }
    if (options.instance.fullscreen && !gameArguments.contains(QStringLiteral("--fullscreen"))) {
        gameArguments.append(QStringLiteral("--fullscreen"));
    }
    for (QString &argument : gameArguments) argument = replaceVariables(argument, variables);
    jvmArguments << mainClass;
    jvmArguments.append(gameArguments);
    return jvmArguments;
}

bool LaunchService::libraryAllowedOnWindows(const QJsonObject &library) const
{
    return rulesAllowOnWindows(library.value(QStringLiteral("rules")).toArray());
}

QString LaunchService::nativeArchiveFor(const QJsonObject &library) const
{
    const QJsonObject downloads = library.value(QStringLiteral("downloads")).toObject();
    QString classifier = library.value(QStringLiteral("natives")).toObject().value(QStringLiteral("windows")).toString();
    classifier.replace(QStringLiteral("${arch}"), QStringLiteral("64"));
    if (classifier.isEmpty()) return {};
    const QJsonObject file = downloads.value(QStringLiteral("classifiers")).toObject().value(classifier).toObject();
    const QString relative = file.value(QStringLiteral("path")).toString().isEmpty()
        ? mavenPath(library.value(QStringLiteral("name")).toString() + QLatin1Char(':') + classifier)
        : file.value(QStringLiteral("path")).toString();
    return relative.isEmpty() ? QString() : QDir(gameDirectory()).filePath(QStringLiteral("libraries/%1").arg(relative));
}

QString LaunchService::resolveJava(const LaunchOptions &options) const
{
    const QString configured = !options.javaExecutable.trimmed().isEmpty()
        ? options.javaExecutable.trimmed() : options.instance.java.path.trimmed();
    return QDir::toNativeSeparators(configured);
}

QString LaunchService::versionDirectory(const QString &version) const
{
    return QDir(gameDirectory()).filePath(QStringLiteral("versions/%1").arg(version));
}

QString LaunchService::gameDirectory() const
{
    return QDir(m_dataDirectory).filePath(QStringLiteral("game"));
}

QString LaunchService::replaceVariables(const QString &value, const QHash<QString, QString> &variables) const
{
    QString result = value;
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) result.replace(it.key(), it.value());
    return result;
}

void LaunchService::onProcessStarted()
{
    emit launchStarted(m_runningInstanceId, m_process->processId());
}

void LaunchService::onProcessReadyRead()
{
    emitProcessOutput(m_process->readAll());
}

void LaunchService::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    emitProcessOutput(m_process->readAll());
    if (!m_outputBuffer.isEmpty()) {
        emit logLine(m_runningInstanceId, QString::fromLocal8Bit(m_outputBuffer));
        m_outputBuffer.clear();
    }
    const QString id = m_runningInstanceId;
    m_runningInstanceId.clear();
    emit launchExited(id, exitCode, exitStatus == QProcess::CrashExit);
}

void LaunchService::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart) return;
    const QString id = m_runningInstanceId;
    m_runningInstanceId.clear();
    emit launchError(id, QStringLiteral("Не удалось запустить Java: %1").arg(m_process->errorString()));
}

void LaunchService::emitProcessOutput(const QByteArray &data)
{
    m_outputBuffer.append(data);
    int newline = -1;
    while ((newline = m_outputBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_outputBuffer.left(newline);
        m_outputBuffer.remove(0, newline + 1);
        if (!line.trimmed().isEmpty()) emit logLine(m_runningInstanceId, QString::fromLocal8Bit(line).trimmed());
    }
}

} // namespace atlas
