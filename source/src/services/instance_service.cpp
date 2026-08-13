#include "services/instance_service.h"

#include "infrastructure/json_store.h"
#include "infrastructure/logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUuid>

namespace atlas {

InstanceService::InstanceService(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_dataDirectory(QDir::cleanPath(QDir::fromNativeSeparators(dataDirectory)))
{
    m_instancesDirectory = QDir(m_dataDirectory).filePath(QStringLiteral("instances"));
    QDir().mkpath(instancesDirectory());
}

QString InstanceService::dataDirectory() const
{
    return m_dataDirectory;
}

QString InstanceService::instancesDirectory() const
{
    return m_instancesDirectory;
}

bool InstanceService::setInstancesDirectory(const QString &path, QString *error)
{
    QString requested = QDir::fromNativeSeparators(path.trimmed());
    if (requested.isEmpty()) requested = QStringLiteral("instances");
    const QString normalized = QDir::cleanPath(QDir::isAbsolutePath(requested)
        ? requested
        : QDir(m_dataDirectory).filePath(requested));
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("Папка экземпляров не указана.");
        return false;
    }
    if (!QDir().mkpath(normalized)) {
        if (error) *error = QStringLiteral("Не удалось создать папку экземпляров: %1").arg(normalized);
        return false;
    }
    m_instancesDirectory = normalized;
    return true;
}

QString InstanceService::instanceDirectory(const QString &id) const
{
    return QDir(instancesDirectory()).filePath(id);
}

QString InstanceService::filePath(const QString &id) const
{
    return QDir(instanceDirectory(id)).filePath(QStringLiteral("instance.json"));
}

bool InstanceService::isSafeId(const QString &id) const
{
    if (id.isEmpty() || id.size() > 64 || id == QStringLiteral(".") || id == QStringLiteral("..")) {
        return false;
    }
    for (const QChar character : id) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

QVector<Instance> InstanceService::loadAll(QString *error) const
{
    QVector<Instance> result;
    QDir directory(instancesDirectory());
    const QFileInfoList entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        QJsonObject object;
        QString readError;
        if (!JsonStore::readObject(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("instance.json")), &object, &readError)) {
            Logger::warning(QStringLiteral("Skipping instance %1: %2").arg(entry.fileName(), readError));
            continue;
        }
        Instance instance = Instance::fromJson(object);
        if (instance.id.isEmpty()) instance.id = entry.fileName();
        // Legacy profiles stored the game directly next to instance.json. New profiles
        // may safely point to a user-selected directory, while metadata stays managed.
        if (instance.rootPath.trimmed().isEmpty()) instance.rootPath = entry.absoluteFilePath();
        instance.rootPath = QDir::cleanPath(QDir::fromNativeSeparators(instance.rootPath));
        result.append(instance);
    }
    if (result.isEmpty() && error) *error = QString();
    return result;
}

bool InstanceService::save(const Instance &instance, QString *error) const
{
    if (!isSafeId(instance.id)) {
        if (error) *error = QStringLiteral("Instance ID contains unsafe characters");
        return false;
    }

    const QString metadataDirectory = instanceDirectory(instance.id);
    if (!QDir().mkpath(metadataDirectory)) {
        if (error) *error = QStringLiteral("Cannot create instance metadata directory");
        return false;
    }

    Instance copy = instance;
    const QString requestedRoot = QDir::fromNativeSeparators(copy.rootPath.trimmed());
    copy.rootPath = QDir::cleanPath(requestedRoot.isEmpty() ? metadataDirectory : requestedRoot);
    if (!QDir().mkpath(copy.rootPath)) {
        if (error) *error = QStringLiteral("Cannot create game directory: %1").arg(copy.rootPath);
        return false;
    }

    QDir gameDirectoryObject(copy.rootPath);
    const QStringList directories{
        QStringLiteral("mods"), QStringLiteral("resourcepacks"), QStringLiteral("saves"),
        QStringLiteral("shaderpacks"), QStringLiteral("config"), QStringLiteral("screenshots")
    };
    for (const QString &directory : directories) {
        if (!gameDirectoryObject.mkpath(directory)) {
            if (error) *error = QStringLiteral("Cannot create game subdirectory: %1").arg(directory);
            return false;
        }
    }

    if (!copy.createdAt.isValid()) copy.createdAt = QDateTime::currentDateTimeUtc();
    return JsonStore::writeObject(filePath(copy.id), copy.toJson(), error);
}

bool InstanceService::remove(const QString &id, QString *error) const
{
    if (!isSafeId(id)) {
        if (error) *error = QStringLiteral("Instance ID contains unsafe characters");
        return false;
    }
    QDir directory(instanceDirectory(id));
    if (!directory.exists()) {
        if (error) *error = QStringLiteral("Instance does not exist");
        return false;
    }
    if (!directory.removeRecursively()) {
        if (error) *error = QStringLiteral("Cannot remove instance directory");
        return false;
    }
    return true;
}

Instance InstanceService::create(const QString &name, const QString &minecraftVersion,
                                LoaderKind loader, const QString &loaderVersion) const
{
    Instance instance;
    instance.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    instance.name = name.trimmed().isEmpty() ? QStringLiteral("New Instance") : name.trimmed();
    instance.minecraftVersion = minecraftVersion.trimmed().isEmpty()
        ? QStringLiteral("1.20.1") : minecraftVersion.trimmed();
    instance.loader.kind = loader;
    instance.loader.version = loaderVersion.trimmed();
    instance.createdAt = QDateTime::currentDateTimeUtc();
    instance.rootPath = instanceDirectory(instance.id);
    return instance;
}

} // namespace atlas
