#include "services/auth_service.h"
#include "services/settings_service.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool fail(const QString &message)
{
    QTextStream(stderr) << "FAIL: " << message << '\n';
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail(QStringLiteral("Cannot create temporary settings directory")) ? 0 : 1;

    atlas::SettingsService settingsService(directory.path());
    atlas::LauncherSettings settings;
    settings.offlinePlayerName = QStringLiteral("Atlas_1122");

    QString error;
    if (!settingsService.save(settings, &error)) {
        return fail(QStringLiteral("Cannot save offline nickname: %1").arg(error)) ? 0 : 1;
    }

    const atlas::LauncherSettings loadedSettings = settingsService.load(&error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("Cannot load offline nickname: %1").arg(error)) ? 0 : 1;
    }
    if (loadedSettings.offlinePlayerName != settings.offlinePlayerName) {
        return fail(QStringLiteral("Stored offline nickname differs from saved value")) ? 0 : 1;
    }

    atlas::AuthService authService(directory.path());
    const atlas::AccountSession session = authService.offlineSession(loadedSettings.offlinePlayerName);
    if (session.isMicrosoft()) return fail(QStringLiteral("Offline profile incorrectly became Microsoft account")) ? 0 : 1;
    if (session.playerName != QStringLiteral("Atlas_1122")) {
        return fail(QStringLiteral("Launch session did not receive stored offline nickname")) ? 0 : 1;
    }
    if (!session.isValidForLaunch()) return fail(QStringLiteral("Offline nickname session is not launchable")) ? 0 : 1;

    QTextStream(stdout) << "PASS: saved and applied offline nickname " << session.playerName << '\n';
    return 0;
}
