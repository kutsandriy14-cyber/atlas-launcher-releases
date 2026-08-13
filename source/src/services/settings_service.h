#pragma once

#include <QObject>
#include <QString>

namespace atlas {

struct LauncherSettings
{
    QString javaPath;
    QString instancesPath;
    QString downloadsPath;
    QString language = QStringLiteral("ru");
    QString theme = QStringLiteral("obsidian");
    int minMemoryMiB = 1024;
    int maxMemoryMiB = 4096;
    int maxConcurrentDownloads = 8;
    // Максимальное время без поступления байтов для одной HTTPS-загрузки.
    int inactivityTimeoutSeconds = 90;
    int downloadLimitKiB = 0;
    bool verifyHashes = true;
    bool closeToTray = false;
    bool enableAnimations = true;
    bool showSnapshots = false;
    QString modrinthUserAgent = QStringLiteral("AtlasLauncher/0.1.0 (personal launcher)");
    QString githubRepository = QStringLiteral("kutsandriy14-cyber/atlas-launcher-releases");
    bool autoCheckForUpdates = true;
    QString microsoftClientId;
    QString offlinePlayerName = QStringLiteral("Player");

    QJsonObject toJson() const;
    static LauncherSettings fromJson(const QJsonObject &object);
};

class SettingsService final : public QObject
{
    Q_OBJECT

public:
    explicit SettingsService(const QString &dataDirectory, QObject *parent = nullptr);

    LauncherSettings load(QString *error = nullptr) const;
    bool save(const LauncherSettings &settings, QString *error = nullptr) const;
    QString settingsFilePath() const;
    QString dataDirectory() const;

private:
    QString m_dataDirectory;
};

} // namespace atlas
