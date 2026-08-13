#pragma once

#include "domain/types.h"

#include <QString>
#include <QStringList>

namespace atlas {

struct PackageValidation
{
    bool valid = false;
    PackageManifest manifest;
    QStringList errors;
    QStringList warnings;
};

struct PackageImportResult
{
    bool success = false;
    QStringList copiedFiles;
    QStringList pendingRemoteFiles;
    QStringList errors;
};

class PackageService
{
public:
    PackageValidation inspectDirectory(const QString &packageDirectory) const;
    PackageValidation inspectManifest(const QString &manifestFile) const;
    PackageImportResult importLocalFiles(const QString &packageDirectory,
                                         const PackageManifest &manifest,
                                         const QString &targetDirectory) const;

    static bool isSafeRelativePath(const QString &path);

private:
    bool copyRecursively(const QString &sourcePath, const QString &targetPath,
                         QStringList *copiedFiles, QStringList *errors) const;
};

} // namespace atlas
