#pragma once

#include "domain/types.h"
#include "services/settings_service.h"
#include "services/package_service.h"
#include "services/download_manager.h"
#include "services/java_runtime_service.h"
#include "services/minecraft_install_service.h"
#include "services/loader_install_service.h"
#include "services/auth_service.h"
#include "services/launch_service.h"
#include "providers/modrinth_client.h"
#include "providers/curseforge_client.h"

#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QNetworkAccessManager;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QPushButton;
class QCheckBox;
class QComboBox;
class QCloseEvent;
class QWidget;

namespace atlas {

class InstanceService;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(InstanceService *instanceService,
                        SettingsService *settingsService,
                        DownloadManager *downloadManager,
                        JavaRuntimeService *javaRuntimeService,
                        MinecraftInstallService *minecraftInstallService,
                        LoaderInstallService *loaderInstallService,
                        AuthService *authService,
                        LaunchService *launchService,
                        QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showPage(int pageIndex);
    void reloadInstances();
    void selectInstance(QListWidgetItem *item);
    void createInstance();
    void editSelectedInstance();
    void importPackage();
    void launchSelected();
    void saveSettings();
    void applyOfflineNickname();
    void checkForUpdates();
    void chooseJavaPath();
    void installSelectedJavaRuntime();
    void refreshJavaStatus();
    void refreshDownloadQueue();
    void refreshMinecraftVersions();
    void showMinecraftVersions(const QVector<atlas::MinecraftVersionDescriptor> &versions);
    void showMinecraftVersionsError(const QString &message);
    void installSelectedVanilla();
    void installSelectedLoader();
    void showVanillaInstallFinished(const QString &instanceId, const QString &version);
    void showVanillaInstallError(const QString &instanceId, const QString &message);
    void showLoaderInstallFinished(const QString &instanceId, atlas::LoaderKind kind,
                                   const QString &loaderVersion, const QString &profileId);
    void showLoaderInstallError(const QString &instanceId, const QString &message);
    void searchCatalog();
    void installSelectedCatalogProject();
    void installResolvedModrinthFiles(const QString &projectId, const QString &versionId,
                                      const QVector<atlas::ModrinthFile> &files);
    void installResolvedCurseForgeFile(const atlas::CurseForgeFile &file);
    void showCatalogResults(const QVector<atlas::ModrinthProject> &projects, int totalHits);
    void showCatalogError(const QString &message);
    void showRateLimit(int retryAfterSeconds);
    void showCurseForgeResults(const QVector<atlas::CatalogProject> &projects, int totalHits);
    void manageAccount();
    void showDeviceCode(const atlas::DeviceCodePrompt &prompt);
    void applyAuthenticatedSession(const atlas::AccountSession &session);
    void showAuthenticationError(const QString &message);
    void showLaunchStarted(const QString &instanceId, qint64 processId);
    void showLaunchExited(const QString &instanceId, int exitCode, bool crashed);
    void showLaunchError(const QString &instanceId, const QString &message);

private:
    void buildUi();
    QWidget *buildHomePage();
    QWidget *buildLibraryPage();
    QWidget *buildCatalogPage();
    QWidget *buildDownloadsPage();
    QWidget *buildSettingsPage();
    QWidget *makeCard(const QString &objectName = QStringLiteral("card"));
    void setActiveNavigation(int pageIndex);
    Instance selectedInstance() const;
    void updateHome();
    void updateAccountUi();
    int requiredJavaFor(const Instance &instance) const;
    QString catalogDestinationDirectory(const Instance &instance, const QString &projectType) const;
    QString catalogMinecraftVersion() const;
    LoaderKind catalogLoader() const;
    void refreshCatalogCategories();
    void refreshCatalogVersionChoices();
    void updateCatalogPagination();
    void appendCatalogProject(const CatalogProject &project);
    void requestCatalogIcon(const QString &iconUrl);
    void applyCatalogIcon(const QString &iconUrl, const QPixmap &pixmap);
    void scheduleDownloadQueueRefresh(bool immediate = false);
    void setStatus(const QString &message, int timeout = 5000);
    void continuePendingLoaderInstallation();

    InstanceService *m_instanceService = nullptr;
    SettingsService *m_settingsService = nullptr;
    DownloadManager *m_downloadManager = nullptr;
    JavaRuntimeService *m_javaRuntimeService = nullptr;
    MinecraftInstallService *m_minecraftInstallService = nullptr;
    LoaderInstallService *m_loaderInstallService = nullptr;
    AuthService *m_authService = nullptr;
    LaunchService *m_launchService = nullptr;
    LauncherSettings m_settings;
    AccountSession m_activeAccount;
    QString m_pendingLaunchInstanceId;
    QString m_pendingVanillaInstanceId;
    QString m_pendingLoaderInstanceId;
    bool m_pendingLoaderVanillaReady = false;
    bool m_hiddenForGame = false;
    QString m_pendingCatalogInstanceId;
    QString m_pendingCatalogProjectId;
    QString m_pendingCatalogVersionId;
    QString m_pendingCatalogProjectType;
    PackageService m_packageService;
    ModrinthClient *m_modrinthClient = nullptr;
    CurseForgeClient *m_curseForgeClient = nullptr;
    QNetworkAccessManager *m_catalogIconNetwork = nullptr;
    QHash<QString, QPixmap> m_catalogIconCache;
    QVector<Instance> m_instances;
    // Идентификатор текущей сборки хранится отдельно от QListWidget: reloadInstances()
    // пересоздаёт список после установки и не должен возвращать пользователя к первой строке.
    QString m_selectedInstanceId;
    QVector<MinecraftVersionDescriptor> m_minecraftVersions;
    QVector<MinecraftVersionDescriptor> m_allMinecraftVersions;
    QVector<ModrinthCategory> m_modrinthCategories;
    QVector<CurseForgeCategory> m_curseForgeCategories;
    int m_curseForgeCategoryClassId = 0;
    int m_catalogPage = 0;
    int m_catalogTotalHits = 0;
    int m_catalogPageSize = 20;

    QStackedWidget *m_pages = nullptr;
    QListWidget *m_instanceList = nullptr;
    QLabel *m_libraryEmptyState = nullptr;
    QLabel *m_selectedName = nullptr;
    QLabel *m_selectedMeta = nullptr;
    QLabel *m_selectedPath = nullptr;
    QLabel *m_libraryCount = nullptr;
    QLabel *m_versionsStatus = nullptr;
    QPushButton *m_refreshVersionsButton = nullptr;
    QPushButton *m_installVanillaButton = nullptr;
    QLabel *m_javaStatus = nullptr;
    QLabel *m_accountLabel = nullptr;
    QPushButton *m_accountButton = nullptr;
    QLabel *m_catalogStatus = nullptr;
    QLineEdit *m_catalogSearch = nullptr;
    QComboBox *m_catalogGameVersion = nullptr;
    QComboBox *m_catalogLoader = nullptr;
    QComboBox *m_catalogSort = nullptr;
    QComboBox *m_catalogCategory = nullptr;
    QWidget *m_curseForgeKeyPanel = nullptr;
    QLineEdit *m_curseForgeKeyInline = nullptr;
    QLineEdit *m_curseForgeKey = nullptr;
    QComboBox *m_catalogProvider = nullptr;
    QComboBox *m_catalogType = nullptr;
    QListWidget *m_catalogList = nullptr;
    QPushButton *m_catalogInstallButton = nullptr;
    QPushButton *m_catalogPreviousPage = nullptr;
    QPushButton *m_catalogNextPage = nullptr;
    QLabel *m_catalogPageLabel = nullptr;
    QLineEdit *m_javaPath = nullptr;
    QLineEdit *m_microsoftClientId = nullptr;
    QLineEdit *m_offlinePlayerName = nullptr;
    QPushButton *m_applyOfflineNicknameButton = nullptr;
    QLineEdit *m_modrinthUserAgent = nullptr;
    QLineEdit *m_githubRepository = nullptr;
    QCheckBox *m_autoCheckForUpdates = nullptr;
    QPushButton *m_checkForUpdatesButton = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_javaMajor = nullptr;
    QPushButton *m_installJavaButton = nullptr;
    QLabel *m_managedJavaStatus = nullptr;
    QListWidget *m_downloadQueue = nullptr;
    QLabel *m_downloadQueueStatus = nullptr;
    QTimer *m_downloadQueueRefreshTimer = nullptr;
    QLineEdit *m_instancesPath = nullptr;
    QSpinBox *m_minMemory = nullptr;
    QSpinBox *m_maxMemory = nullptr;
    QSpinBox *m_concurrentDownloads = nullptr;
    QSpinBox *m_inactivityTimeout = nullptr;
    QCheckBox *m_verifyHashes = nullptr;
    QCheckBox *m_showSnapshots = nullptr;
    QCheckBox *m_enableAnimations = nullptr;
    QPushButton *m_launchButton = nullptr;
    QVector<QPushButton *> m_navButtons;
    int m_currentPage = 0;
};

} // namespace atlas
