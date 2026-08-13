#include "services/settings_service.h"

#include "infrastructure/json_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

namespace atlas {

QJsonObject LauncherSettings::toJson() const
{
    return QJsonObject{
        {QStringLiteral("javaPath"), javaPath},
        {QStringLiteral("instancesPath"), instancesPath},
        {QStringLiteral("downloadsPath"), downloadsPath},
        {QStringLiteral("language"), language},
        {QStringLiteral("theme"), theme},
        {QStringLiteral("minMemoryMiB"), minMemoryMiB},
        {QStringLiteral("maxMemoryMiB"), maxMemoryMiB},
        {QStringLiteral("maxConcurrentDownloads"), maxConcurrentDownloads},
        {QStringLiteral("inactivityTimeoutSeconds"), inactivityTimeoutSeconds},
        {QStringLiteral("downloadLimitKiB"), downloadLimitKiB},
        {QStringLiteral("verifyHashes"), verifyHashes},
        {QStringLiteral("closeToTray"), closeToTray},
        {QStringLiteral("enableAnimations"), enableAnimations},
        {QStringLiteral("showSnapshots"), showSnapshots},
        {QStringLiteral("modrinthUserAgent"), modrinthUserAgent},
        {QStringLiteral("githubRepository"), githubRepository},
        {QStringLiteral("autoCheckForUpdates"), autoCheckForUpdates},
        {QStringLiteral("microsoftClientId"), microsoftClientId},
        {QStringLiteral("offlinePlayerName"), offlinePlayerName}
    };
}

LauncherSettings LauncherSettings::fromJson(const QJsonObject &object)
{
    LauncherSettings settings;
    settings.javaPath = object.value(QStringLiteral("javaPath")).toString();
    settings.instancesPath = object.value(QStringLiteral("instancesPath")).toString();
    settings.downloadsPath = object.value(QStringLiteral("downloadsPath")).toString();
    settings.language = object.value(QStringLiteral("language")).toString(settings.language);
    settings.theme = object.value(QStringLiteral("theme")).toString(settings.theme);
    settings.minMemoryMiB = qBound(256, object.value(QStringLiteral("minMemoryMiB")).toInt(settings.minMemoryMiB), 65536);
    settings.maxMemoryMiB = qBound(settings.minMemoryMiB, object.value(QStringLiteral("maxMemoryMiB")).toInt(settings.maxMemoryMiB), 65536);
    settings.maxConcurrentDownloads = qBound(1, object.value(QStringLiteral("maxConcurrentDownloads")).toInt(settings.maxConcurrentDownloads), 16);
    settings.inactivityTimeoutSeconds = qBound(15, object.value(QStringLiteral("inactivityTimeoutSeconds")).toInt(settings.inactivityTimeoutSeconds), 600);
    settings.downloadLimitKiB = qMax(0, object.value(QStringLiteral("downloadLimitKiB")).toInt(settings.downloadLimitKiB));
    settings.verifyHashes = object.value(QStringLiteral("verifyHashes")).toBool(settings.verifyHashes);
    settings.closeToTray = object.value(QStringLiteral("closeToTray")).toBool(settings.closeToTray);
    settings.enableAnimations = object.value(QStringLiteral("enableAnimations")).toBool(settings.enableAnimations);
    settings.showSnapshots = object.value(QStringLiteral("showSnapshots")).toBool(settings.showSnapshots);
    settings.modrinthUserAgent = object.value(QStringLiteral("modrinthUserAgent")).toString(settings.modrinthUserAgent);
    settings.githubRepository = object.value(QStringLiteral("githubRepository")).toString(settings.githubRepository).trimmed();
    settings.autoCheckForUpdates = object.value(QStringLiteral("autoCheckForUpdates")).toBool(settings.autoCheckForUpdates);
    settings.microsoftClientId = object.value(QStringLiteral("microsoftClientId")).toString();
    settings.offlinePlayerName = object.value(QStringLiteral("offlinePlayerName")).toString(settings.offlinePlayerName).trimmed();
    if (settings.offlinePlayerName.isEmpty()) settings.offlinePlayerName = QStringLiteral("Player");
    return settings;
}

SettingsService::SettingsService(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_dataDirectory(dataDirectory)
{
    QDir().mkpath(m_dataDirectory);
}

LauncherSettings SettingsService::load(QString *error) const
{
    QJsonObject object;
    if (!JsonStore::readObject(settingsFilePath(), &object, error)) {
        return LauncherSettings();
    }
    return LauncherSettings::fromJson(object);
}

bool SettingsService::save(const LauncherSettings &settings, QString *error) const
{
    return JsonStore::writeObject(settingsFilePath(), settings.toJson(), error);
}

QString SettingsService::settingsFilePath() const
{
    return QDir(m_dataDirectory).filePath(QStringLiteral("settings.json"));
}

QString SettingsService::dataDirectory() const
{
    return m_dataDirectory;
}

} // namespace atlas
