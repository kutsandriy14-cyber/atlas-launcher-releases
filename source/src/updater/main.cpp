#include "services/update_service.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString argumentValue(const QStringList &arguments, const QString &name)
{
    const int index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

qint64 argumentPid(const QStringList &arguments, const QString &name)
{
    bool ok = false;
    const qint64 value = argumentValue(arguments, name).toLongLong(&ok);
    return ok && value > 0 ? value : -1;
}

bool waitForProcessExit(qint64 pid, int timeoutMilliseconds)
{
    if (pid <= 0) return true;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) return true;
    const DWORD result = WaitForSingleObject(process, static_cast<DWORD>(timeoutMilliseconds));
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
#else
    Q_UNUSED(timeoutMilliseconds)
    return true;
#endif
}

#ifdef Q_OS_WIN
struct CloseWindowRequest
{
    DWORD processId = 0;
};

BOOL CALLBACK closeProcessWindow(HWND window, LPARAM parameter)
{
    auto *request = reinterpret_cast<CloseWindowRequest *>(parameter);
    DWORD ownerProcessId = 0;
    GetWindowThreadProcessId(window, &ownerProcessId);
    if (request && ownerProcessId == request->processId && IsWindowVisible(window)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}
#endif

void requestLauncherClose(qint64 pid)
{
#ifdef Q_OS_WIN
    if (pid <= 0) return;
    CloseWindowRequest request;
    request.processId = static_cast<DWORD>(pid);
    EnumWindows(closeProcessWindow, reinterpret_cast<LPARAM>(&request));
#else
    Q_UNUSED(pid)
#endif
}

bool copyDirectoryContents(const QString &sourceDirectory, const QString &targetDirectory, QString *error)
{
    const QDir source(sourceDirectory);
    if (!source.exists()) {
        if (error) *error = QStringLiteral("Не найдена распакованная папка обновления.");
        return false;
    }
    if (!QDir().mkpath(targetDirectory)) {
        if (error) *error = QStringLiteral("Не удалось создать папку установки.");
        return false;
    }
    QDirIterator iterator(sourceDirectory, QDir::NoDotAndDotDot | QDir::AllEntries,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo sourceInfo(sourcePath);
        const QString relativePath = source.relativeFilePath(sourcePath);
        const QString targetPath = QDir(targetDirectory).filePath(relativePath);
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                if (error) *error = QStringLiteral("Не удалось создать папку: %1").arg(relativePath);
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            if (error) *error = QStringLiteral("Не удалось подготовить папку: %1").arg(relativePath);
            return false;
        }
        QFile::remove(targetPath);
        if (!QFile::copy(sourcePath, targetPath)) {
            if (error) *error = QStringLiteral("Не удалось заменить файл: %1").arg(relativePath);
            return false;
        }
    }
    return true;
}

bool extractArchive(const QString &archivePath, const QString &installDirectory,
                    const QString &temporaryDirectory, QString *extractedRoot, QString *error)
{
    const QString sevenZip = QDir(installDirectory).filePath(QStringLiteral("tools/7za.exe"));
    if (!QFileInfo::exists(sevenZip)) {
        if (error) *error = QStringLiteral("Не найден встроенный tools\\7za.exe.");
        return false;
    }
    QProcess process;
    process.setWorkingDirectory(temporaryDirectory);
    process.start(sevenZip, {QStringLiteral("x"), QStringLiteral("-y"), archivePath,
                             QStringLiteral("-o%1").arg(temporaryDirectory)});
    if (!process.waitForStarted(15000) || !process.waitForFinished(180000)) {
        if (error) *error = QStringLiteral("Не удалось распаковать ZIP обновления.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("7-Zip завершился с ошибкой: %1")
            .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }
    const QFileInfoList entries = QDir(temporaryDirectory).entryInfoList(
        QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    if (entries.size() == 1 && entries.first().isDir()) {
        *extractedRoot = entries.first().absoluteFilePath();
    } else {
        *extractedRoot = temporaryDirectory;
    }
    return true;
}

bool launchTemporaryUpdater(const QString &archivePath, const QString &installDirectory,
                            const QString &restartPath, qint64 targetPid, QString *error)
{
    const QString sourceUpdater = QDir(installDirectory).filePath(QStringLiteral("AtlasUpdater.exe"));
    if (!QFileInfo::exists(sourceUpdater)) {
        if (error) *error = QStringLiteral("AtlasUpdater.exe не найден в папке установки.");
        return false;
    }
    const QString stagingDirectory = QDir::temp().filePath(
        QStringLiteral("AtlasUpdater-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!QDir().mkpath(stagingDirectory)) {
        if (error) *error = QStringLiteral("Не удалось создать временную папку updater.");
        return false;
    }
    const QString temporaryUpdater = QDir(stagingDirectory).filePath(QStringLiteral("AtlasUpdater.exe"));
    if (!QFile::copy(sourceUpdater, temporaryUpdater)) {
        if (error) *error = QStringLiteral("Не удалось подготовить отдельный updater.exe.");
        return false;
    }
    const QDir sourceDirectory(installDirectory);
    const QFileInfoList dlls = sourceDirectory.entryInfoList({QStringLiteral("*.dll")}, QDir::Files);
    for (const QFileInfo &dll : dlls) {
        if (!QFile::copy(dll.absoluteFilePath(), QDir(stagingDirectory).filePath(dll.fileName()))) {
            if (error) *error = QStringLiteral("Не удалось подготовить библиотеку updater: %1").arg(dll.fileName());
            return false;
        }
    }
    const QString sourcePlatforms = sourceDirectory.filePath(QStringLiteral("platforms"));
    if (QDir(sourcePlatforms).exists() && !copyDirectoryContents(sourcePlatforms, QDir(stagingDirectory).filePath(QStringLiteral("platforms")), error)) {
        return false;
    }
    QStringList arguments;
    arguments << QStringLiteral("--apply")
              << QStringLiteral("--archive") << archivePath
              << QStringLiteral("--install-dir") << installDirectory
              << QStringLiteral("--restart") << restartPath
              << QStringLiteral("--target-pid") << QString::number(targetPid);
    if (!QProcess::startDetached(temporaryUpdater, arguments, stagingDirectory)) {
        if (error) *error = QStringLiteral("Не удалось запустить отдельный процесс установки.");
        return false;
    }

    // Отдельный updater уже запущен из временной папки и ждёт PID Atlas Launcher.
    // Закрываем основное приложение через его цикл событий: WM_CLOSE может быть
    // перехвачен оконным менеджером Windows или не дойти до окна при модальном диалоге.
    requestLauncherClose(targetPid);
    QTimer::singleShot(0, []() { QCoreApplication::quit(); });
    return true;
}

int applyUpdate(const QStringList &arguments)
{
    const QString archivePath = argumentValue(arguments, QStringLiteral("--archive"));
    const QString installDirectory = argumentValue(arguments, QStringLiteral("--install-dir"));
    const QString restartPath = argumentValue(arguments, QStringLiteral("--restart"));
    const qint64 targetPid = argumentPid(arguments, QStringLiteral("--target-pid"));
    if (archivePath.isEmpty() || installDirectory.isEmpty() || !QFileInfo::exists(archivePath)) {
        QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"),
                              QStringLiteral("Не найден проверенный архив обновления."));
        return 2;
    }
    if (!waitForProcessExit(targetPid, 10 * 60 * 1000)) {
        QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"),
                              QStringLiteral("Лаунчер не закрылся за 10 минут. Обновление не применено."));
        return 3;
    }

    QTemporaryDir temporaryDirectory(QDir::temp().filePath(QStringLiteral("AtlasUpdateExtract-XXXXXX")));
    if (!temporaryDirectory.isValid()) {
        QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"),
                              QStringLiteral("Не удалось создать временную папку распаковки."));
        return 4;
    }
    QString extractedRoot;
    QString error;
    if (!extractArchive(archivePath, installDirectory, temporaryDirectory.path(), &extractedRoot, &error) ||
        !copyDirectoryContents(extractedRoot, installDirectory, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"),
                              QStringLiteral("Обновление не применено: %1").arg(error));
        return 5;
    }
    if (!restartPath.isEmpty() && QFileInfo::exists(restartPath)) {
        QProcess::startDetached(restartPath, {}, installDirectory);
    }
    return 0;
}

int checkForUpdate(QApplication &application, const QStringList &arguments)
{
    const QString repository = argumentValue(arguments, QStringLiteral("--repo"));
    const QString currentVersion = argumentValue(arguments, QStringLiteral("--current-version"));
    const QString installDirectory = argumentValue(arguments, QStringLiteral("--install-dir"));
    const QString settingsDirectory = argumentValue(arguments, QStringLiteral("--settings-dir"));
    const qint64 parentPid = argumentPid(arguments, QStringLiteral("--parent-pid"));
    if (repository.isEmpty() || currentVersion.isEmpty() || installDirectory.isEmpty() || settingsDirectory.isEmpty()) {
        return 2;
    }

    QSettings preference(QDir(settingsDirectory).filePath(QStringLiteral("updater.ini")), QSettings::IniFormat);
    atlas::UpdateService service;
    QObject::connect(&service, &atlas::UpdateService::noUpdateAvailable, &application, [&application]() {
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateCheckError, &application, [&application](const QString &) {
        // A background check must remain unobtrusive if the network or GitHub is unavailable.
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateAvailable, &application,
                     [&application, &service, &preference, installDirectory, settingsDirectory, parentPid](const atlas::UpdateRelease &release) {
        if (preference.value(QStringLiteral("ignoredVersion")).toString() == release.version) {
            application.quit();
            return;
        }
        QMessageBox prompt;
        prompt.setIcon(QMessageBox::Information);
        prompt.setWindowTitle(QStringLiteral("Доступно обновление Atlas Launcher"));
        prompt.setText(QStringLiteral("Доступна версия %1.").arg(release.version));
        prompt.setInformativeText(QStringLiteral("Обновление будет скачано с GitHub Releases, проверено по SHA-256. После согласия Atlas Launcher закроется, файлы заменятся, затем лаунчер запустится снова."));
        QAbstractButton *update = prompt.addButton(QStringLiteral("Обновить"), QMessageBox::AcceptRole);
        QAbstractButton *later = prompt.addButton(QStringLiteral("Позже"), QMessageBox::RejectRole);
        QAbstractButton *ignore = prompt.addButton(QStringLiteral("Игнорировать эту версию"), QMessageBox::DestructiveRole);
        prompt.exec();
        if (prompt.clickedButton() == update) {
            auto *progress = new QProgressDialog(nullptr);
            progress->setWindowTitle(QStringLiteral("Atlas Updater"));
            progress->setLabelText(QStringLiteral("Скачивается обновление %1…").arg(release.version));
            progress->setCancelButton(nullptr);
            progress->setWindowModality(Qt::ApplicationModal);
            progress->setMinimumDuration(0);
            progress->setRange(0, 0);
            progress->show();

            QObject::connect(&service, &atlas::UpdateService::updateDownloadProgress, &application,
                             [progress](qint64 received, qint64 total) {
                if (!progress) return;
                if (total > 0) {
                    progress->setRange(0, 1000);
                    progress->setValue(int((received * 1000) / total));
                    progress->setLabelText(QStringLiteral("Скачивается обновление: %1 из %2 МБ")
                                           .arg(QString::number(received / (1024.0 * 1024.0), 'f', 1))
                                           .arg(QString::number(total / (1024.0 * 1024.0), 'f', 1)));
                }
            });
            QObject::connect(&service, &atlas::UpdateService::updateDownloadError, &application,
                             [progress](const QString &) {
                if (progress) progress->close();
            });
            QObject::connect(&service, &atlas::UpdateService::updateReadyToInstall, &application,
                             [progress](const QString &, const atlas::UpdateRelease &) {
                if (!progress) return;
                progress->setRange(0, 0);
                progress->setLabelText(QStringLiteral("Проверка завершена. Запускается установка…"));
            });

            const QString updatesDirectory = QDir(settingsDirectory).filePath(QStringLiteral("updates"));
            service.downloadUpdate(release, updatesDirectory);
        } else if (prompt.clickedButton() == ignore) {
            preference.setValue(QStringLiteral("ignoredVersion"), release.version);
            preference.sync();
            application.quit();
        } else if (prompt.clickedButton() == later) {
            application.quit();
        }
    });
    QObject::connect(&service, &atlas::UpdateService::updateDownloadError, &application,
                     [&application](const QString &message) {
        QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"), message);
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateReadyToInstall, &application,
                     [&application, installDirectory, parentPid](const QString &archivePath, const atlas::UpdateRelease &) {
        QString error;
        const QString restartPath = QDir(installDirectory).filePath(QStringLiteral("AtlasLauncher.exe"));
        if (!launchTemporaryUpdater(archivePath, installDirectory, restartPath, parentPid, &error)) {
            QMessageBox::critical(nullptr, QStringLiteral("Atlas Updater"), error);
        }
        application.quit();
    });
    service.checkForUpdate(repository, currentVersion);
    return application.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Atlas Updater"));
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--apply"))) return applyUpdate(arguments);
    if (arguments.contains(QStringLiteral("--check"))) return checkForUpdate(application, arguments);
    return 1;
}
