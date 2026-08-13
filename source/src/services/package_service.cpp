#include "services/package_service.h"

#include "infrastructure/json_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>

namespace atlas {

bool PackageService::isSafeRelativePath(const QString &path)
{
    const QString trimmed = path.trimmed();
    const QString portable = QDir::fromNativeSeparators(trimmed);
    static const QRegularExpression windowsDrivePath(QStringLiteral("^[A-Za-z]:[/\\\\]"));
    if (trimmed.isEmpty() || QDir::isAbsolutePath(trimmed) || trimmed.startsWith(QStringLiteral(":/")) ||
        portable.startsWith(QLatin1Char('/')) || portable.startsWith(QChar(0x5C)) ||
        windowsDrivePath.match(trimmed).hasMatch()) {
        return false;
    }
    const QString normalized = QDir::cleanPath(portable);
    if (normalized == QStringLiteral(".") || normalized == QStringLiteral("..") ||
        normalized.startsWith(QStringLiteral("../")) || normalized.contains(QStringLiteral("/../"))) {
        return false;
    }
    return !normalized.startsWith(QLatin1Char('/'));
}

PackageValidation PackageService::inspectDirectory(const QString &packageDirectory) const
{
    return inspectManifest(QDir(packageDirectory).filePath(QStringLiteral("package.json")));
}

PackageValidation PackageService::inspectManifest(const QString &manifestFile) const
{
    PackageValidation validation;
    QJsonObject object;
    QString readError;
    if (!JsonStore::readObject(manifestFile, &object, &readError)) {
        validation.errors.append(QStringLiteral("Не удалось прочитать package.json: %1").arg(readError));
        return validation;
    }

    validation.manifest = PackageManifest::fromJson(object);
    const PackageManifest &manifest = validation.manifest;
    if (manifest.format != QStringLiteral("atlas-launcher-package")) {
        validation.errors.append(QStringLiteral("Неподдерживаемый формат пакета"));
    }
    if (manifest.schemaVersion != 1) {
        validation.errors.append(QStringLiteral("Поддерживается только schemaVersion 1"));
    }
    if (manifest.name.trimmed().isEmpty()) {
        validation.errors.append(QStringLiteral("У пакета отсутствует название"));
    }
    if (manifest.minecraftVersion.trimmed().isEmpty()) {
        validation.errors.append(QStringLiteral("Не указана версия Minecraft"));
    }
    if (manifest.loader.kind == LoaderKind::Unknown) {
        validation.errors.append(QStringLiteral("Указан неизвестный загрузчик"));
    }
    if (manifest.files.size() > 2000) {
        validation.errors.append(QStringLiteral("Пакет содержит слишком много файловых деклараций"));
    }

    for (const PackageFile &file : manifest.files) {
        if (!isSafeRelativePath(file.path)) {
            validation.errors.append(QStringLiteral("Небезопасный путь файла: %1").arg(file.path));
            continue;
        }
        const QString source = file.source.trimmed().toLower();
        if (source != QStringLiteral("local") && source != QStringLiteral("modrinth") &&
            source != QStringLiteral("curseforge")) {
            validation.errors.append(QStringLiteral("Неподдерживаемый источник для %1").arg(file.path));
        }
        if ((source == QStringLiteral("modrinth") || source == QStringLiteral("curseforge")) &&
            (file.projectId.isEmpty() || file.versionId.isEmpty())) {
            validation.errors.append(QStringLiteral("Для удалённого файла %1 нужны projectId и versionId").arg(file.path));
        }
    }
    for (const QString &overridePath : manifest.overrides) {
        if (!isSafeRelativePath(overridePath)) {
            validation.errors.append(QStringLiteral("Небезопасный override-путь: %1").arg(overridePath));
        }
    }

    validation.valid = validation.errors.isEmpty();
    return validation;
}

bool PackageService::copyRecursively(const QString &sourcePath, const QString &targetPath,
                                     QStringList *copiedFiles, QStringList *errors) const
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        errors->append(QStringLiteral("Исходный файл отсутствует: %1").arg(sourcePath));
        return false;
    }

    if (sourceInfo.isDir()) {
        if (!QDir().mkpath(targetPath)) {
            errors->append(QStringLiteral("Не удалось создать папку: %1").arg(targetPath));
            return false;
        }
        QDir sourceDirectory(sourcePath);
        const QFileInfoList entries = sourceDirectory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                                                     QDir::Name | QDir::DirsFirst);
        bool allCopied = true;
        for (const QFileInfo &entry : entries) {
            allCopied = copyRecursively(entry.absoluteFilePath(),
                                        QDir(targetPath).filePath(entry.fileName()),
                                        copiedFiles, errors) && allCopied;
        }
        return allCopied;
    }

    QFileInfo targetInfo(targetPath);
    if (!QDir().mkpath(targetInfo.dir().absolutePath())) {
        errors->append(QStringLiteral("Не удалось создать папку назначения: %1").arg(targetInfo.dir().absolutePath()));
        return false;
    }
    if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
        errors->append(QStringLiteral("Не удалось заменить файл: %1").arg(targetPath));
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        errors->append(QStringLiteral("Не удалось скопировать файл: %1").arg(sourcePath));
        return false;
    }
    copiedFiles->append(targetPath);
    return true;
}

PackageImportResult PackageService::importLocalFiles(const QString &packageDirectory,
                                                      const PackageManifest &manifest,
                                                      const QString &targetDirectory) const
{
    PackageImportResult result;
    const QString root = QDir(packageDirectory).absolutePath();
    const QString targetRoot = QDir(targetDirectory).absolutePath();
    if (!QDir().mkpath(targetRoot)) {
        result.errors.append(QStringLiteral("Не удалось создать папку экземпляра"));
        return result;
    }

    for (const QString &overridePath : manifest.overrides) {
        if (!isSafeRelativePath(overridePath)) {
            result.errors.append(QStringLiteral("Небезопасный override-путь: %1").arg(overridePath));
            continue;
        }
        const QString sourcePath = QDir(root).filePath(overridePath);
        const QString targetPath = QDir(targetRoot).filePath(overridePath);
        copyRecursively(sourcePath, targetPath, &result.copiedFiles, &result.errors);
    }

    for (const PackageFile &file : manifest.files) {
        if (!isSafeRelativePath(file.path)) {
            result.errors.append(QStringLiteral("Небезопасный путь файла: %1").arg(file.path));
            continue;
        }
        const QString source = file.source.trimmed().toLower();
        if (source == QStringLiteral("local")) {
            copyRecursively(QDir(root).filePath(file.path), QDir(targetRoot).filePath(file.path),
                            &result.copiedFiles, &result.errors);
        } else if (source == QStringLiteral("modrinth") || source == QStringLiteral("curseforge")) {
            result.pendingRemoteFiles.append(file.path);
        }
    }

    result.success = result.errors.isEmpty();
    return result;
}

} // namespace atlas
