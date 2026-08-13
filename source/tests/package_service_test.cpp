#include "services/package_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool writeTextFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(content) == content.size();
}

int fail(const char *message)
{
    std::cerr << message << std::endl;
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return fail("Cannot create temporary directory");

    const QString packageRoot = QDir(temporary.path()).filePath(QStringLiteral("source"));
    const QString targetRoot = QDir(temporary.path()).filePath(QStringLiteral("target"));
    QDir().mkpath(QDir(packageRoot).filePath(QStringLiteral("config")));

    const QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("atlas-launcher-package")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("name"), QStringLiteral("Test package")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("minecraft"), QStringLiteral("1.20.1")},
        {QStringLiteral("loader"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("fabric")},
                                                  {QStringLiteral("version"), QStringLiteral("0.15.11")}}},
        {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("mods/example.jar")},
                                                             {QStringLiteral("source"), QStringLiteral("local")}}}},
        {QStringLiteral("overrides"), QJsonArray{QStringLiteral("config")}}
    };
    if (!writeTextFile(QDir(packageRoot).filePath(QStringLiteral("package.json")),
                       QJsonDocument(manifest).toJson(QJsonDocument::Indented)) ||
        !writeTextFile(QDir(packageRoot).filePath(QStringLiteral("mods/example.jar")), "test-mod") ||
        !writeTextFile(QDir(packageRoot).filePath(QStringLiteral("config/options.txt")), "test-config")) {
        return fail("Cannot prepare test package");
    }

    atlas::PackageService service;
    const atlas::PackageValidation validation = service.inspectDirectory(packageRoot);
    if (!validation.valid) return fail("Valid package was rejected");
    if (atlas::PackageService::isSafeRelativePath(QStringLiteral("../outside.txt")) ||
        atlas::PackageService::isSafeRelativePath(QStringLiteral("C:/outside.txt")) ||
        !atlas::PackageService::isSafeRelativePath(QStringLiteral("mods/example.jar"))) {
        return fail("Path protection result is incorrect");
    }

    const atlas::PackageImportResult result = service.importLocalFiles(packageRoot, validation.manifest, targetRoot);
    if (!result.success || !result.pendingRemoteFiles.isEmpty() ||
        !QFile::exists(QDir(targetRoot).filePath(QStringLiteral("mods/example.jar"))) ||
        !QFile::exists(QDir(targetRoot).filePath(QStringLiteral("config/options.txt")))) {
        return fail("Package files were not imported correctly");
    }

    std::cout << "PackageServiceTests passed" << std::endl;
    return 0;
}
