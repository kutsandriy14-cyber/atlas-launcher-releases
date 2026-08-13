#include "infrastructure/logger.h"
#include "services/instance_service.h"
#include "services/settings_service.h"
#include "services/download_manager.h"
#include "services/java_runtime_service.h"
#include "services/minecraft_install_service.h"
#include "services/loader_install_service.h"
#include "services/auth_service.h"
#include "services/launch_service.h"
#include "services/update_service.h"
#include "ui/atlas_theme.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    // The main window can be hidden while Minecraft is running; keep the launcher process alive
    // so it can restore the interface and receive the game's exit signal.
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("Atlas"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("atlas-launcher.local"));
    QCoreApplication::setApplicationName(QStringLiteral("Atlas Launcher"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.1"));

    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    const QString dataDirectory = roamingAppData.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("atlaslauncher"))
        : QDir(roamingAppData).filePath(QStringLiteral("atlaslauncher"));
    QDir().mkpath(dataDirectory);
    atlas::Logger::install(dataDirectory);
    atlas::AtlasTheme::apply(application);

    atlas::SettingsService settingsService(dataDirectory);
    const atlas::LauncherSettings startupSettings = settingsService.load();
    atlas::InstanceService instanceService(dataDirectory);
    atlas::DownloadManager downloadManager;
    atlas::JavaRuntimeService javaRuntimeService(dataDirectory, &downloadManager);
    atlas::MinecraftInstallService minecraftInstallService(dataDirectory, &downloadManager);
    atlas::LoaderInstallService loaderInstallService(dataDirectory, &downloadManager);
    atlas::AuthService authService(dataDirectory);
    atlas::LaunchService launchService(dataDirectory);
    const QString extractorPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("tools/7za.exe"));
    javaRuntimeService.setExtractorPath(extractorPath);
    atlas::MainWindow window(&instanceService, &settingsService, &downloadManager,
                             &javaRuntimeService, &minecraftInstallService, &loaderInstallService,
                             &authService, &launchService);
    window.show();
    if (startupSettings.autoCheckForUpdates && !startupSettings.githubRepository.isEmpty()) {
        QTimer::singleShot(1500, [&application, startupSettings, dataDirectory]() {
            QString error;
            if (!atlas::UpdateService::launchCheckProcess(startupSettings.githubRepository,
                                                          application.applicationVersion(),
                                                          application.applicationDirPath(),
                                                          dataDirectory, &error)) {
                atlas::Logger::warning(QStringLiteral("Автопроверка обновлений не запущена: %1").arg(error));
            }
        });
    }
    return application.exec();
}
