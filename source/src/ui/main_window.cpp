#include "ui/main_window.h"

#include "infrastructure/logger.h"
#include "services/instance_service.h"
#include "services/update_service.h"

#include <algorithm>
#include <QCheckBox>
#include <QColor>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QIcon>
#include <QPixmap>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariant>

namespace atlas {
namespace {

QLabel *label(const QString &text, const QString &objectName = QString())
{
    auto *result = new QLabel(text);
    if (!objectName.isEmpty()) result->setObjectName(objectName);
    result->setWordWrap(true);
    return result;
}

QPushButton *button(const QString &text, const QString &objectName = QString())
{
    auto *result = new QPushButton(text);
    if (!objectName.isEmpty()) result->setObjectName(objectName);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

QFrame *lineSeparator()
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QStringLiteral("color: #25303d; background: #25303d; max-height: 1px;"));
    return line;
}

int javaMajorForMinecraftVersion(const QString &minecraftVersion)
{
    const QStringList parts = minecraftVersion.split(QLatin1Char('.'));
    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = false;
    const int major = parts.value(0).toInt(&majorOk);
    const int minor = parts.value(1).toInt(&minorOk);
    const int patch = parts.value(2).toInt(&patchOk);

    // Mojang runtime requirements: Java 8 through 1.16.5, Java 16 for 1.17,
    // Java 17 from 1.18 through 1.20.4, and Java 21 starting with 1.20.5.
    // Unknown/non-release identifiers deliberately use the newest managed runtime.
    if (!majorOk || !minorOk || major != 1) return 21;
    if (minor <= 16) return 8;
    if (minor == 17) return 16;
    if (minor < 20) return 17;
    if (minor == 20 && (!patchOk || patch <= 4)) return 17;
    return 21;
}

int curseForgeClassId(ContentType type)
{
    // Идентификаторы классов Minecraft из публичного каталога CurseForge.
    // 0 означает «весь контент» и намеренно не добавляется к запросу.
    switch (type) {
    case ContentType::Mod: return 6;
    case ContentType::Modpack: return 4471;
    case ContentType::ResourcePack: return 12;
    case ContentType::Shader: return 6552; // Shader Packs
    case ContentType::World: return 17;
    case ContentType::All: return 0;
    }
    return 0;
}

class BuildEditorDialog final : public QDialog
{
public:
    BuildEditorDialog(const QVector<MinecraftVersionDescriptor> &versions, const Instance &initial,
                      int defaultMinMemory, int defaultMaxMemory, JavaRuntimeService *javaRuntimeService,
                      MinecraftInstallService *minecraftInstallService,
                      LoaderInstallService *loaderInstallService, bool showSnapshots, bool showOldBeta,
                      bool showOldAlpha, QWidget *parent)
        : QDialog(parent), m_result(initial), m_editing(!initial.id.isEmpty()),
          m_javaRuntimeService(javaRuntimeService), m_minecraftInstallService(minecraftInstallService),
          m_loaderInstallService(loaderInstallService), m_showSnapshots(showSnapshots),
          m_showOldBeta(showOldBeta), m_showOldAlpha(showOldAlpha)
    {
        setWindowTitle(m_editing ? QStringLiteral("Изменить сборку") : QStringLiteral("Новая сборка"));
        setModal(true);
        // Окно должно нормально открываться даже на 800×600 и не требовать
        // горизонтальной прокрутки. Длинные подписи ниже переносятся над полями.
        resize(680, 650);
        setMinimumSize(440, 420);
        setSizeGripEnabled(true);

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget;
        content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        content->setMinimumWidth(0);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(28, 24, 28, 20);
        layout->setSpacing(14);

        layout->addWidget(label(m_editing ? QStringLiteral("Изменить сборку") : QStringLiteral("Новая сборка"), QStringLiteral("pageTitle")));
        layout->addWidget(label(m_editing
            ? QStringLiteral("Измените только нужные параметры. Остальные настройки скрыты ниже и сохраняются в этом профиле.")
            : QStringLiteral("Выберите версию и тип. После сохранения Atlas сам скачает локальную Java и официальные файлы Minecraft."), QStringLiteral("muted")));

        auto *basicCard = new QFrame;
        basicCard->setObjectName(QStringLiteral("card"));
        auto *basicLayout = new QVBoxLayout(basicCard);
        basicLayout->setContentsMargins(18, 16, 18, 16);
        basicLayout->setSpacing(10);
        basicLayout->addWidget(label(QStringLiteral("Основное"), QStringLiteral("cardTitle")));
        auto *basicForm = new QFormLayout;
        basicForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        basicForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
        m_name = new QLineEdit(m_result.name.isEmpty() ? QStringLiteral("Моя сборка") : m_result.name);
        m_name->setPlaceholderText(QStringLiteral("Например: Vanilla 1.21.1"));
        basicForm->addRow(QStringLiteral("Название сборки"), m_name);
        m_version = new QComboBox;
        m_version->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_version->setMinimumContentsLength(10);
        m_version->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_versionRefreshButton = button(QStringLiteral("Обновить"));
        m_versionRefreshButton->setToolTip(QStringLiteral("Заново получить официальный список версий Minecraft от Mojang"));
        auto *versionRow = new QWidget;
        auto *versionLayout = new QHBoxLayout(versionRow);
        versionLayout->setContentsMargins(0, 0, 0, 0);
        versionLayout->setSpacing(8);
        versionLayout->addWidget(m_version, 1);
        versionLayout->addWidget(m_versionRefreshButton);
        basicForm->addRow(QStringLiteral("Версия Minecraft"), versionRow);
        m_versionHint = label(QStringLiteral("Официальный список версий готовится…"), QStringLiteral("muted"));
        basicForm->addRow(QString(), m_versionHint);
        m_loader = new QComboBox;
        m_loader->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_loader->setMinimumContentsLength(14);
        m_loader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        basicForm->addRow(QStringLiteral("Загрузчик"), m_loader);
        m_loaderVersionCombo = new QComboBox;
        m_loaderVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_loaderVersionCombo->setMinimumContentsLength(12);
        m_loaderVersionCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_loaderVersionLabel = label(QStringLiteral("Версия загрузчика"));
        basicForm->addRow(m_loaderVersionLabel, m_loaderVersionCombo);
        m_loaderHint = label(QString(), QStringLiteral("muted"));
        basicForm->addRow(QString(), m_loaderHint);
        basicLayout->addLayout(basicForm);
        layout->addWidget(basicCard);

        auto *folderCard = new QFrame;
        folderCard->setObjectName(QStringLiteral("card"));
        auto *folderLayout = new QVBoxLayout(folderCard);
        folderLayout->setContentsMargins(18, 16, 18, 16);
        folderLayout->setSpacing(9);
        folderLayout->addWidget(label(QStringLiteral("Папка игры"), QStringLiteral("cardTitle")));
        m_useAtlasFolder = new QCheckBox(QStringLiteral("Использовать отдельную папку Atlas для этой сборки"));
        m_useAtlasFolder->setChecked(!m_editing || m_result.rootPath.trimmed().isEmpty());
        folderLayout->addWidget(m_useAtlasFolder);
        folderLayout->addWidget(label(QStringLiteral("Рекомендуется: файлы, моды, миры и настройки будут изолированы от других сборок."), QStringLiteral("muted")));
        auto *folderRow = new QHBoxLayout;
        m_gameFolder = new QLineEdit(m_result.rootPath);
        m_gameFolder->setPlaceholderText(QStringLiteral("Выберите отдельную папку игры"));
        m_folderBrowse = button(QStringLiteral("Обзор"));
        folderRow->addWidget(m_gameFolder, 1);
        folderRow->addWidget(m_folderBrowse);
        folderLayout->addLayout(folderRow);
        layout->addWidget(folderCard);

        auto *advancedToggle = button(QStringLiteral("▾  Больше настроек"));
        advancedToggle->setCheckable(true);
        layout->addWidget(advancedToggle, 0, Qt::AlignLeft);
        m_advanced = new QFrame;
        m_advanced->setObjectName(QStringLiteral("card"));
        auto *advancedLayout = new QVBoxLayout(m_advanced);
        advancedLayout->setContentsMargins(18, 16, 18, 16);
        advancedLayout->setSpacing(10);
        advancedLayout->addWidget(label(QStringLiteral("Параметры запуска профиля"), QStringLiteral("cardTitle")));
        advancedLayout->addWidget(label(QStringLiteral("Изменяйте эти значения только если они действительно нужны. Они применяются именно к этой сборке."), QStringLiteral("muted")));
        auto *advancedForm = new QFormLayout;
        advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        advancedForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
        auto *resolutionRow = new QHBoxLayout;
        m_width = new QSpinBox;
        m_width->setRange(320, 7680);
        m_width->setValue(m_result.resolutionWidth);
        m_height = new QSpinBox;
        m_height->setRange(240, 4320);
        m_height->setValue(m_result.resolutionHeight);
        resolutionRow->addWidget(m_width);
        resolutionRow->addWidget(label(QStringLiteral("×")));
        resolutionRow->addWidget(m_height);
        resolutionRow->addStretch();
        advancedForm->addRow(QStringLiteral("Разрешение окна"), resolutionRow);
        m_fullscreen = new QCheckBox(QStringLiteral("Полноэкранный режим"));
        m_fullscreen->setChecked(m_result.fullscreen);
        advancedForm->addRow(QString(), m_fullscreen);
        m_memory = new QSpinBox;
        m_memory->setRange(256, 65536);
        m_memory->setSuffix(QStringLiteral(" MiB"));
        m_memory->setValue(m_result.java.maxMemoryMiB > 0 ? m_result.java.maxMemoryMiB : defaultMaxMemory);
        advancedForm->addRow(QStringLiteral("Максимум памяти"), m_memory);
        m_launcherBehavior = new QComboBox;
        m_launcherBehavior->addItem(QStringLiteral("Оставить Atlas открытым"),
                                    static_cast<int>(LauncherWindowBehavior::KeepOpen));
        m_launcherBehavior->addItem(QStringLiteral("Свернуть Atlas в панель задач"),
                                    static_cast<int>(LauncherWindowBehavior::Minimize));
        m_launcherBehavior->addItem(QStringLiteral("Закрыть окно Atlas (игра продолжит работу)"),
                                    static_cast<int>(LauncherWindowBehavior::CloseWindow));
        const int launcherBehaviorIndex = m_launcherBehavior->findData(
            static_cast<int>(m_result.launcherWindowBehavior));
        m_launcherBehavior->setCurrentIndex(launcherBehaviorIndex >= 0 ? launcherBehaviorIndex : 0);
        advancedForm->addRow(QStringLiteral("После запуска Minecraft"), m_launcherBehavior);
        advancedForm->addRow(QString(), label(
            QStringLiteral("Minecraft запускается отдельным процессом Java и не зависит от открытого окна Atlas."),
            QStringLiteral("muted")));
        m_safeMode = new QCheckBox(QStringLiteral("Безопасный режим: не применять мои дополнительные аргументы"));
        m_safeMode->setChecked(m_result.safeMode);
        advancedForm->addRow(QString(), m_safeMode);
        m_javaMode = new QComboBox;
        m_javaMode->addItem(QStringLiteral("Автовыбор — подходящая Java Atlas"), static_cast<int>(JavaRuntimeMode::Automatic));
        m_javaMode->addItem(QStringLiteral("Выбрать локальную Java Atlas"), static_cast<int>(JavaRuntimeMode::AtlasManaged));
        m_javaMode->addItem(QStringLiteral("Свой javaw.exe / java.exe"), static_cast<int>(JavaRuntimeMode::Custom));
        const int javaModeIndex = m_javaMode->findData(static_cast<int>(m_result.java.runtimeMode));
        m_javaMode->setCurrentIndex(javaModeIndex >= 0 ? javaModeIndex : 0);
        advancedForm->addRow(QStringLiteral("Источник Java"), m_javaMode);
        m_javaRequired = label(QString(), QStringLiteral("muted"));
        advancedForm->addRow(QStringLiteral("Подходящая Java"), m_javaRequired);
        m_atlasJavaMajor = new QComboBox;
        m_atlasJavaMajor->addItem(QStringLiteral("Java 8"), 8);
        m_atlasJavaMajor->addItem(QStringLiteral("Java 17"), 17);
        m_atlasJavaMajor->addItem(QStringLiteral("Java 21"), 21);
        const int atlasMajor = m_result.java.managedMajor > 0 ? m_result.java.managedMajor
            : javaMajorForMinecraftVersion(m_version->currentData().toString());
        const int atlasMajorIndex = m_atlasJavaMajor->findData(atlasMajor);
        if (atlasMajorIndex >= 0) m_atlasJavaMajor->setCurrentIndex(atlasMajorIndex);
        advancedForm->addRow(QStringLiteral("Локальная Java Atlas"), m_atlasJavaMajor);
        auto *managedJavaRow = new QHBoxLayout;
        m_downloadJavaButton = button(QStringLiteral("Скачать подходящую Java"));
        m_managedJavaStatus = label(QString(), QStringLiteral("muted"));
        managedJavaRow->addWidget(m_downloadJavaButton);
        managedJavaRow->addWidget(m_managedJavaStatus, 1);
        advancedForm->addRow(QStringLiteral("Java Atlas"), managedJavaRow);
        auto *customJavaRow = new QHBoxLayout;
        m_customJava = new QLineEdit(m_result.java.path);
        m_customJava->setPlaceholderText(QStringLiteral("Путь к javaw.exe или java.exe"));
        m_customJavaBrowse = button(QStringLiteral("Обзор и проверить"));
        customJavaRow->addWidget(m_customJava, 1);
        customJavaRow->addWidget(m_customJavaBrowse);
        advancedForm->addRow(QStringLiteral("Свой файл Java"), customJavaRow);
        m_customJavaStatus = label(QString(), QStringLiteral("muted"));
        advancedForm->addRow(QString(), m_customJavaStatus);
        m_jvmArguments = new QPlainTextEdit(m_result.java.jvmArguments.join(QLatin1Char('\n')));
        m_jvmArguments->setPlaceholderText(QStringLiteral("Один аргумент JVM на строку, например: -XX:+UseG1GC"));
        m_jvmArguments->setMaximumHeight(76);
        advancedForm->addRow(QStringLiteral("Аргументы JVM"), m_jvmArguments);
        m_gameArguments = new QPlainTextEdit(m_result.gameArguments.join(QLatin1Char('\n')));
        m_gameArguments->setPlaceholderText(QStringLiteral("Один игровой аргумент на строку"));
        m_gameArguments->setMaximumHeight(76);
        advancedForm->addRow(QStringLiteral("Аргументы игры"), m_gameArguments);
        advancedLayout->addLayout(advancedForm);
        layout->addWidget(m_advanced);
        m_advanced->setVisible(false);

        scroll->setWidget(content);
        outer->addWidget(scroll, 1);
        auto *buttons = new QDialogButtonBox;
        buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
        m_acceptButton = buttons->addButton(m_editing ? QStringLiteral("Сохранить") : QStringLiteral("Создать и установить"), QDialogButtonBox::AcceptRole);
        outer->addWidget(buttons);

        const QVector<QPair<QString, LoaderKind>> loaderOptions{
            {QStringLiteral("Vanilla — без загрузчика"), LoaderKind::Vanilla},
            {QStringLiteral("Fabric"), LoaderKind::Fabric},
            {QStringLiteral("Legacy Fabric — для 1.13.2 и старее"), LoaderKind::LegacyFabric},
            {QStringLiteral("Quilt"), LoaderKind::Quilt},
            {QStringLiteral("Forge"), LoaderKind::Forge},
            {QStringLiteral("NeoForge"), LoaderKind::NeoForge}
        };
        const auto updateCompatibleLoaders = [this, loaderOptions]() {
            const LoaderKind previous = m_loader->currentIndex() >= 0
                ? static_cast<LoaderKind>(m_loader->currentData().toInt()) : m_result.loader.kind;
            const QString minecraftVersion = m_version->currentData().toString();
            QSignalBlocker blocker(m_loader);
            m_loader->clear();
            for (const auto &option : loaderOptions) {
                const bool show = option.second == LoaderKind::Vanilla || minecraftVersion.isEmpty()
                    || (m_loaderInstallService && m_loaderInstallService->supportsMinecraftVersion(option.second, minecraftVersion));
                if (show) m_loader->addItem(option.first, static_cast<int>(option.second));
            }
            int desiredIndex = m_loader->findData(static_cast<int>(previous));
            if (desiredIndex < 0) desiredIndex = m_loader->findData(static_cast<int>(LoaderKind::Vanilla));
            m_loader->setCurrentIndex(qMax(0, desiredIndex));
        };
        const auto refreshLoaderVersions = [this]() {
            const LoaderKind kind = static_cast<LoaderKind>(m_loader->currentData().toInt());
            const QString minecraftVersion = m_version->currentData().toString();
            const bool hasLoader = kind != LoaderKind::Vanilla;
            m_loaderVersionLabel->setVisible(hasLoader);
            m_loaderVersionCombo->setVisible(hasLoader);
            m_loaderVersionCombo->clear();

            if (!hasLoader) {
                m_loaderVersionCombo->setEnabled(false);
                m_loaderHint->setText(QStringLiteral("Vanilla не требует отдельного загрузчика."));
                return;
            }
            if (minecraftVersion.isEmpty()) {
                m_loaderVersionCombo->addItem(QStringLiteral("Сначала выберите Minecraft"));
                m_loaderVersionCombo->setEnabled(false);
                m_loaderHint->setText(QStringLiteral("Выберите официальную версию Minecraft, чтобы получить совместимые версии %1.")
                                          .arg(loaderKindToString(kind)));
                return;
            }
            if (m_loaderInstallService && !m_loaderInstallService->supportsMinecraftVersion(kind, minecraftVersion)) {
                m_loaderVersionCombo->addItem(QStringLiteral("Загрузчик недоступен для этой версии"));
                m_loaderVersionCombo->setEnabled(false);
                m_loaderHint->setText(QStringLiteral("%1 не поддерживает Minecraft %2. Atlas скрывает несовместимые загрузчики автоматически.")
                                          .arg(loaderKindToString(kind), minecraftVersion));
                return;
            }
            if (!m_loaderInstallService) {
                m_loaderVersionCombo->addItem(QStringLiteral("Сервис версий недоступен"));
                m_loaderVersionCombo->setEnabled(false);
                m_loaderHint->setText(QStringLiteral("Не удалось инициализировать официальный список версий %1.")
                                          .arg(loaderKindToString(kind)));
                return;
            }

            m_loaderVersionCombo->addItem(QStringLiteral("Загрузка доступных версий…"));
            m_loaderVersionCombo->setEnabled(false);
            m_loaderHint->setText(QStringLiteral("Получаем совместимые версии %1 для Minecraft %2 из официального источника…")
                                      .arg(loaderKindToString(kind), minecraftVersion));
            m_loaderInstallService->refreshVersions(kind, minecraftVersion);
        };
        if (m_loaderInstallService) {
            connect(m_loaderInstallService, &LoaderInstallService::versionsReady, this,
                    [this](LoaderKind kind, const QString &minecraftVersion,
                           const QVector<LoaderVersionDescriptor> &versions) {
                const LoaderKind selectedKind = static_cast<LoaderKind>(m_loader->currentData().toInt());
                if (kind != selectedKind || minecraftVersion != m_version->currentData().toString()) return;

                m_loaderVersionCombo->clear();
                if (kind == LoaderKind::Fabric || kind == LoaderKind::LegacyFabric || kind == LoaderKind::Quilt) {
                    m_loaderVersionCombo->addItem(QStringLiteral("Последняя стабильная (авто)"), QString());
                }
                for (const LoaderVersionDescriptor &descriptor : versions) {
                    if (!descriptor.isValid()) continue;
                    const QString caption = descriptor.stable
                        ? QStringLiteral("%1 · стабильная").arg(descriptor.version)
                        : QStringLiteral("%1 · предварительная").arg(descriptor.version);
                    m_loaderVersionCombo->addItem(caption, descriptor.version);
                }
                if (m_loaderVersionCombo->count() == 0) {
                    m_loaderVersionCombo->addItem(QStringLiteral("Совместимые версии не найдены"));
                    m_loaderVersionCombo->setEnabled(false);
                    m_loaderHint->setText(QStringLiteral("Официальный источник не вернул совместимых версий %1 для Minecraft %2.")
                                              .arg(loaderKindToString(kind), minecraftVersion));
                    return;
                }

                const bool restoringExisting = m_result.loader.kind == kind
                    && m_result.minecraftVersion == minecraftVersion
                    && !m_result.loader.version.trimmed().isEmpty();
                if (restoringExisting) {
                    const int savedIndex = m_loaderVersionCombo->findData(m_result.loader.version);
                    if (savedIndex >= 0) m_loaderVersionCombo->setCurrentIndex(savedIndex);
                }
                m_loaderVersionCombo->setEnabled(true);
                if (kind == LoaderKind::Fabric || kind == LoaderKind::LegacyFabric || kind == LoaderKind::Quilt) {
                    m_loaderHint->setText(QStringLiteral("Можно оставить «Последняя стабильная (авто)» или явно выбрать версию %1.")
                                              .arg(loaderKindToString(kind)));
                } else {
                    m_loaderHint->setText(QStringLiteral("Выберите точную совместимую версию %1 из официального Maven.")
                                              .arg(loaderKindToString(kind)));
                }
            });
            connect(m_loaderInstallService, &LoaderInstallService::versionsError, this,
                    [this](LoaderKind kind, const QString &minecraftVersion, const QString &message) {
                const LoaderKind selectedKind = static_cast<LoaderKind>(m_loader->currentData().toInt());
                if (kind != selectedKind || minecraftVersion != m_version->currentData().toString()) return;
                m_loaderVersionCombo->clear();
                m_loaderVersionCombo->addItem(QStringLiteral("Не удалось загрузить версии"));
                m_loaderVersionCombo->setEnabled(false);
                m_loaderHint->setText(QStringLiteral("Не удалось получить версии %1: %2")
                                          .arg(loaderKindToString(kind), message));
            });
        }
        if (m_minecraftInstallService) {
            connect(m_minecraftInstallService, &MinecraftInstallService::versionsReady, this,
                    [this](const QVector<MinecraftVersionDescriptor> &minecraftVersions) {
                setMinecraftVersions(minecraftVersions);
            });
            connect(m_minecraftInstallService, &MinecraftInstallService::versionsError, this,
                    [this](const QString &message) {
                showMinecraftVersionsError(message);
            });
        }
        connect(m_versionRefreshButton, &QPushButton::clicked, this, [this]() {
            requestMinecraftVersions();
        });
        updateCompatibleLoaders();
        refreshLoaderVersions();
        connect(m_loader, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [refreshLoaderVersions](int) { refreshLoaderVersions(); });
        connect(m_version, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [updateCompatibleLoaders, refreshLoaderVersions](int) {
                    updateCompatibleLoaders();
                    refreshLoaderVersions();
                });
        connect(m_useAtlasFolder, &QCheckBox::toggled, this, [this](bool useAtlasFolder) {
            m_gameFolder->setEnabled(!useAtlasFolder);
            m_folderBrowse->setEnabled(!useAtlasFolder);
        });
        m_useAtlasFolder->toggled(m_useAtlasFolder->isChecked());
        connect(m_folderBrowse, &QPushButton::clicked, this, [this]() {
            const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка игры"), m_gameFolder->text());
            if (!directory.isEmpty()) m_gameFolder->setText(QDir::toNativeSeparators(directory));
        });
        connect(advancedToggle, &QPushButton::toggled, this, [this, advancedToggle](bool visible) {
            m_advanced->setVisible(visible);
            advancedToggle->setText(visible ? QStringLiteral("▴  Скрыть дополнительные настройки") : QStringLiteral("▾  Больше настроек"));
        });
        const auto refreshJavaUi = [this]() {
            const JavaRuntimeMode mode = static_cast<JavaRuntimeMode>(m_javaMode->currentData().toInt());
            const int required = javaMajorForMinecraftVersion(m_version->currentData().toString());
            const int selectedManaged = mode == JavaRuntimeMode::AtlasManaged
                ? m_atlasJavaMajor->currentData().toInt() : required;
            const bool custom = mode == JavaRuntimeMode::Custom;
            const bool fixedAtlas = mode == JavaRuntimeMode::AtlasManaged;
            m_atlasJavaMajor->setVisible(fixedAtlas);
            m_downloadJavaButton->setVisible(!custom);
            m_managedJavaStatus->setVisible(!custom);
            m_customJava->setEnabled(custom);
            m_customJavaBrowse->setEnabled(custom);
            m_customJavaStatus->setVisible(custom);
            m_javaRequired->setText(QStringLiteral("Для Minecraft %1 нужна Java %2.")
                                        .arg(m_version->currentData().toString()).arg(required));
            if (custom) {
                m_customJavaStatus->setText(m_customJava->text().trimmed().isEmpty()
                    ? QStringLiteral("Выберите javaw.exe. Atlas проверит версию перед сохранением.")
                    : QStringLiteral("Нажмите «Обзор и проверить» или сохраните: версия будет проверена."));
                return;
            }
            const JavaRuntimeInfo runtime = m_javaRuntimeService ? m_javaRuntimeService->installedRuntime(selectedManaged) : JavaRuntimeInfo{};
            const QString modeTitle = mode == JavaRuntimeMode::Automatic
                ? QStringLiteral("Авто выбирает") : QStringLiteral("Выбрана");
            if (runtime.isValid()) {
                m_managedJavaStatus->setText(QStringLiteral("%1 Java %2: %3").arg(modeTitle).arg(selectedManaged).arg(runtime.javawPath));
                m_downloadJavaButton->setText(QStringLiteral("Java Atlas готова"));
                m_downloadJavaButton->setEnabled(true);
            } else {
                m_managedJavaStatus->setText(QStringLiteral("%1 Java %2 будет скачана в папку Atlas.").arg(modeTitle).arg(selectedManaged));
                m_downloadJavaButton->setText(QStringLiteral("Скачать Java %1").arg(selectedManaged));
                m_downloadJavaButton->setEnabled(m_javaRuntimeService != nullptr);
            }
        };
        connect(m_javaMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [refreshJavaUi](int) { refreshJavaUi(); });
        connect(m_atlasJavaMajor, qOverload<int>(&QComboBox::currentIndexChanged), this, [refreshJavaUi](int) { refreshJavaUi(); });
        connect(m_version, qOverload<int>(&QComboBox::currentIndexChanged), this, [refreshJavaUi](int) { refreshJavaUi(); });
        connect(m_downloadJavaButton, &QPushButton::clicked, this, [this, refreshJavaUi]() {
            if (!m_javaRuntimeService) return;
            const JavaRuntimeMode mode = static_cast<JavaRuntimeMode>(m_javaMode->currentData().toInt());
            const int required = javaMajorForMinecraftVersion(m_version->currentData().toString());
            const int major = mode == JavaRuntimeMode::AtlasManaged ? m_atlasJavaMajor->currentData().toInt() : required;
            if (!JavaRuntimeService::isCompatibleMajor(major, required)) {
                QMessageBox::warning(this, QStringLiteral("Неподходящая Java"),
                    QStringLiteral("Для выбранной версии Minecraft нужна Java %1. Переключите локальную Java Atlas на Java %1 либо выберите «Автовыбор».").arg(required));
                return;
            }
            m_downloadJavaButton->setEnabled(false);
            m_managedJavaStatus->setText(QStringLiteral("Запрошена загрузка Java %1…").arg(major));
            m_javaRuntimeService->ensureRuntime(major);
        });
        if (m_javaRuntimeService) {
            connect(m_javaRuntimeService, &JavaRuntimeService::runtimeReady, this, [refreshJavaUi](const JavaRuntimeInfo &) { refreshJavaUi(); });
            connect(m_javaRuntimeService, &JavaRuntimeService::runtimeError, this, [this, refreshJavaUi](int, const QString &message) {
                m_managedJavaStatus->setText(QStringLiteral("Ошибка Java: %1").arg(message));
                refreshJavaUi();
            });
        }
        connect(m_customJavaBrowse, &QPushButton::clicked, this, [this, refreshJavaUi]() {
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите javaw.exe или java.exe"), m_customJava->text(),
                                                              QStringLiteral("Java (javaw.exe java.exe);;Все файлы (*.*)"));
            if (path.isEmpty()) return;
            m_customJava->setText(QDir::toNativeSeparators(path));
            QString error;
            const JavaExecutableInfo info = JavaRuntimeService::inspectExecutable(path, &error);
            m_customJavaStatus->setText(info.isValid()
                ? QStringLiteral("Проверено: Java %1 (%2)").arg(info.versionText, info.executablePath)
                : QStringLiteral("Файл не подходит: %1").arg(error));
            refreshJavaUi();
        });
        refreshJavaUi();
        setMinecraftVersions(versions);
        if (versions.isEmpty()) requestMinecraftVersions();
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_acceptButton, &QPushButton::clicked, this, [this, defaultMinMemory]() {
            const QString name = m_name->text().trimmed();
            if (name.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Название не задано"), QStringLiteral("Введите название сборки."));
                m_name->setFocus();
                return;
            }
            if (m_version->currentData().toString().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Версия не выбрана"), QStringLiteral("Выберите официальную версию Minecraft."));
                return;
            }
            const LoaderKind kind = static_cast<LoaderKind>(m_loader->currentData().toInt());
            const QString loaderVersion = m_loaderVersionCombo->currentData().toString().trimmed();
            if (kind != LoaderKind::Vanilla && !m_loaderVersionCombo->isEnabled()) {
                QMessageBox::warning(this, QStringLiteral("Список версий загрузчика недоступен"),
                                     QStringLiteral("Дождитесь получения списка совместимых версий %1 либо исправьте указанную ошибку.")
                                         .arg(loaderKindToString(kind)));
                return;
            }
            if ((kind == LoaderKind::Forge || kind == LoaderKind::NeoForge) && loaderVersion.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Не выбрана версия загрузчика"),
                                     QStringLiteral("Для %1 выберите точную версию из официального совместимого списка.")
                                         .arg(loaderKindToString(kind)));
                m_loaderVersionCombo->setFocus();
                return;
            }
            if (!m_useAtlasFolder->isChecked() && m_gameFolder->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Папка игры не задана"), QStringLiteral("Выберите папку игры либо включите отдельную папку Atlas."));
                return;
            }
            m_result.name = name;
            m_result.minecraftVersion = m_version->currentData().toString();
            m_result.loader.kind = kind;
            m_result.loader.version = loaderVersion;
            m_result.rootPath = m_useAtlasFolder->isChecked() ? QString() : QDir::cleanPath(m_gameFolder->text().trimmed());
            m_result.resolutionWidth = m_width->value();
            m_result.resolutionHeight = m_height->value();
            m_result.fullscreen = m_fullscreen->isChecked();
            m_result.launcherWindowBehavior = static_cast<LauncherWindowBehavior>(
                m_launcherBehavior->currentData().toInt());
            m_result.hideLauncherOnGameStart = m_result.launcherWindowBehavior
                != LauncherWindowBehavior::KeepOpen;
            m_result.safeMode = m_safeMode->isChecked();
            m_result.java.runtimeMode = static_cast<JavaRuntimeMode>(m_javaMode->currentData().toInt());
            const int requiredJava = javaMajorForMinecraftVersion(m_result.minecraftVersion);
            m_result.java.managedMajor = m_result.java.runtimeMode == JavaRuntimeMode::AtlasManaged
                ? m_atlasJavaMajor->currentData().toInt() : 0;
            if (m_result.java.runtimeMode == JavaRuntimeMode::AtlasManaged
                && !JavaRuntimeService::isCompatibleMajor(m_result.java.managedMajor, requiredJava)) {
                QMessageBox::warning(this, QStringLiteral("Неподходящая Java Atlas"),
                    QStringLiteral("Для Minecraft %1 нужна Java %2. Выберите Java %2 или используйте «Автовыбор». ")
                        .arg(m_result.minecraftVersion).arg(requiredJava));
                return;
            }
            m_result.java.path = m_result.java.runtimeMode == JavaRuntimeMode::Custom ? m_customJava->text().trimmed() : QString();
            if (m_result.java.runtimeMode == JavaRuntimeMode::Custom) {
                if (m_result.java.path.isEmpty()) {
                    QMessageBox::warning(this, QStringLiteral("Java не задана"), QStringLiteral("Выберите javaw.exe либо включите локальную Java Atlas."));
                    return;
                }
                QString error;
                const JavaExecutableInfo info = JavaRuntimeService::inspectExecutable(m_result.java.path, &error);
                if (!info.isValid() || !JavaRuntimeService::isCompatibleMajor(info.major, requiredJava)) {
                    QMessageBox::warning(this, QStringLiteral("Неподходящая Java"),
                        QStringLiteral("Для Minecraft %1 нужна Java %2. %3")
                            .arg(m_result.minecraftVersion).arg(requiredJava).arg(error.isEmpty() ? QStringLiteral("Выбранный файл несовместим.") : error));
                    return;
                }
            }
            m_result.java.minMemoryMiB = qMax(256, qMin(defaultMinMemory, m_memory->value()));
            m_result.java.maxMemoryMiB = m_memory->value();
            m_result.java.jvmArguments = argumentLines(m_jvmArguments->toPlainText());
            m_result.gameArguments = argumentLines(m_gameArguments->toPlainText());
            accept();
        });
    }

    Instance result() const { return m_result; }

private:
    void setMinecraftVersions(const QVector<MinecraftVersionDescriptor> &versions)
    {
        const QString desiredVersion = !m_result.minecraftVersion.isEmpty()
            ? m_result.minecraftVersion : m_version->currentData().toString();
        m_version->clear();
        for (const MinecraftVersionDescriptor &version : versions) {
            const QString caption = version.type.isEmpty() || version.type == QStringLiteral("release")
                ? version.id : QStringLiteral("%1 · %2").arg(version.id, version.type);
            m_version->addItem(caption, version.id);
        }
        const int versionIndex = m_version->findData(desiredVersion);
        if (versionIndex >= 0) m_version->setCurrentIndex(versionIndex);

        const bool ready = m_version->count() > 0;
        m_version->setEnabled(ready);
        if (m_acceptButton) m_acceptButton->setEnabled(ready);
        if (m_versionRefreshButton) m_versionRefreshButton->setEnabled(m_minecraftInstallService != nullptr);
        if (m_versionHint) {
            m_versionHint->setText(ready
                ? QStringLiteral("Официальных версий доступно: %1. Выберите версию Minecraft, затем при необходимости — загрузчик.")
                      .arg(m_version->count())
                : QStringLiteral("Официальный список версий пока пуст."));
        }
    }

    void requestMinecraftVersions()
    {
        if (!m_minecraftInstallService) {
            showMinecraftVersionsError(QStringLiteral("Сервис получения официальных версий Minecraft не инициализирован."));
            return;
        }
        const bool hasVersions = m_version && m_version->count() > 0;
        if (m_versionRefreshButton) m_versionRefreshButton->setEnabled(false);
        if (m_acceptButton) m_acceptButton->setEnabled(hasVersions);
        if (m_versionHint) {
            m_versionHint->setText(hasVersions
                ? QStringLiteral("Обновляется официальный список версий Mojang…")
                : QStringLiteral("Получаем официальный список версий Mojang…"));
        }
        m_minecraftInstallService->refreshVersions(m_showSnapshots, m_showOldBeta, m_showOldAlpha);
    }

    void showMinecraftVersionsError(const QString &message)
    {
        const bool hasVersions = m_version && m_version->count() > 0;
        if (m_version) m_version->setEnabled(hasVersions);
        if (m_acceptButton) m_acceptButton->setEnabled(hasVersions);
        if (m_versionRefreshButton) m_versionRefreshButton->setEnabled(m_minecraftInstallService != nullptr);
        if (m_versionHint) {
            m_versionHint->setText(QStringLiteral("Не удалось получить список версий: %1 Нажмите «Обновить» и повторите попытку.")
                                   .arg(message));
        }
    }

    static QStringList argumentLines(const QString &text)
    {
        QStringList result;
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (!line.trimmed().isEmpty()) result.append(line.trimmed());
        }
        return result;
    }

    Instance m_result;
    bool m_editing = false;
    JavaRuntimeService *m_javaRuntimeService = nullptr;
    MinecraftInstallService *m_minecraftInstallService = nullptr;
    LoaderInstallService *m_loaderInstallService = nullptr;
    bool m_showSnapshots = false;
    bool m_showOldBeta = false;
    bool m_showOldAlpha = false;
    QLineEdit *m_name = nullptr;
    QComboBox *m_version = nullptr;
    QPushButton *m_versionRefreshButton = nullptr;
    QLabel *m_versionHint = nullptr;
    QComboBox *m_loader = nullptr;
    QLabel *m_loaderVersionLabel = nullptr;
    QComboBox *m_loaderVersionCombo = nullptr;
    QLabel *m_loaderHint = nullptr;
    QCheckBox *m_useAtlasFolder = nullptr;
    QLineEdit *m_gameFolder = nullptr;
    QPushButton *m_folderBrowse = nullptr;
    QFrame *m_advanced = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QCheckBox *m_fullscreen = nullptr;
    QSpinBox *m_memory = nullptr;
    QComboBox *m_launcherBehavior = nullptr;
    QCheckBox *m_safeMode = nullptr;
    QComboBox *m_javaMode = nullptr;
    QLabel *m_javaRequired = nullptr;
    QComboBox *m_atlasJavaMajor = nullptr;
    QPushButton *m_downloadJavaButton = nullptr;
    QLabel *m_managedJavaStatus = nullptr;
    QLineEdit *m_customJava = nullptr;
    QPushButton *m_customJavaBrowse = nullptr;
    QLabel *m_customJavaStatus = nullptr;
    QPlainTextEdit *m_jvmArguments = nullptr;
    QPlainTextEdit *m_gameArguments = nullptr;
    QPushButton *m_acceptButton = nullptr;
};

} // namespace

MainWindow::MainWindow(InstanceService *instanceService,
                       SettingsService *settingsService,
                       DownloadManager *downloadManager,
                       JavaRuntimeService *javaRuntimeService,
                       MinecraftInstallService *minecraftInstallService,
                       LoaderInstallService *loaderInstallService,
                       AuthService *authService,
                       LaunchService *launchService,
                       QWidget *parent)
    : QMainWindow(parent),
      m_instanceService(instanceService),
      m_settingsService(settingsService),
      m_downloadManager(downloadManager),
      m_javaRuntimeService(javaRuntimeService),
      m_minecraftInstallService(minecraftInstallService),
      m_loaderInstallService(loaderInstallService),
      m_authService(authService),
      m_launchService(launchService)
{
    QString settingsError;
    m_settings = m_settingsService->load(&settingsError);
    if (!settingsError.isEmpty()) {
        Logger::warning(QStringLiteral("Settings loaded with defaults: %1").arg(settingsError));
    }
    if (m_downloadManager) {
        m_downloadManager->setMaximumConcurrentDownloads(m_settings.maxConcurrentDownloads);
        m_downloadManager->setInactivityTimeoutSeconds(m_settings.inactivityTimeoutSeconds);
    }
    if (m_instanceService) {
        QString instancesPathError;
        if (!m_instanceService->setInstancesDirectory(m_settings.instancesPath, &instancesPathError)) {
            Logger::warning(QStringLiteral("Using default instances directory: %1").arg(instancesPathError));
            m_settings.instancesPath.clear();
        }
    }
    m_catalogIconNetwork = new QNetworkAccessManager(this);
    m_downloadQueueRefreshTimer = new QTimer(this);
    m_downloadQueueRefreshTimer->setSingleShot(true);
    m_downloadQueueRefreshTimer->setInterval(160);
    connect(m_downloadQueueRefreshTimer, &QTimer::timeout, this, [this]() { refreshDownloadQueue(); });
    m_modrinthClient = new ModrinthClient(m_settings.modrinthUserAgent, this);
    connect(m_modrinthClient, &ModrinthClient::searchFinished,
            this, &MainWindow::showCatalogResults);
    connect(m_modrinthClient, &ModrinthClient::versionFilesResolved,
            this, &MainWindow::installResolvedModrinthFiles);
    connect(m_modrinthClient, &ModrinthClient::requestFailed,
            this, &MainWindow::showCatalogError);
    connect(m_modrinthClient, &ModrinthClient::rateLimited,
            this, &MainWindow::showRateLimit);
    connect(m_modrinthClient, &ModrinthClient::categoriesReceived, this,
            [this](const QVector<ModrinthCategory> &categories) {
                m_modrinthCategories = categories;
                if (m_catalogProvider && m_catalogProvider->currentIndex() == 0) refreshCatalogCategories();
            });
    m_curseForgeClient = new CurseForgeClient(this);
    connect(m_curseForgeClient, &CurseForgeClient::searchFinished,
            this, &MainWindow::showCurseForgeResults);
    connect(m_curseForgeClient, &CurseForgeClient::fileResolved,
            this, &MainWindow::installResolvedCurseForgeFile);
    connect(m_curseForgeClient, &CurseForgeClient::requestFailed,
            this, &MainWindow::showCatalogError);
    connect(m_curseForgeClient, &CurseForgeClient::categoriesReceived, this,
            [this](const QVector<CurseForgeCategory> &categories, int classId) {
                if (classId != m_curseForgeCategoryClassId) return;
                m_curseForgeCategories = categories;
                if (m_catalogProvider && m_catalogProvider->currentIndex() == 1) refreshCatalogCategories();
            });
    if (m_downloadManager) {
        connect(m_downloadManager, &DownloadManager::taskAdded, this,
                [this](const DownloadTask &) { scheduleDownloadQueueRefresh(); });
        connect(m_downloadManager, &DownloadManager::taskChanged, this,
                [this](const DownloadTask &) { scheduleDownloadQueueRefresh(); });
        connect(m_downloadManager, &DownloadManager::queueIdle, this,
                [this]() { scheduleDownloadQueueRefresh(true); });
    }
    if (m_minecraftInstallService) {
        connect(m_minecraftInstallService, &MinecraftInstallService::versionsReady,
                this, &MainWindow::showMinecraftVersions);
        connect(m_minecraftInstallService, &MinecraftInstallService::versionsError,
                this, &MainWindow::showMinecraftVersionsError);
        connect(m_minecraftInstallService, &MinecraftInstallService::installFinished,
                this, &MainWindow::showVanillaInstallFinished);
        connect(m_minecraftInstallService, &MinecraftInstallService::installError,
                this, &MainWindow::showVanillaInstallError);
    }
    if (m_loaderInstallService) {
        connect(m_loaderInstallService, &LoaderInstallService::installFinished,
                this, &MainWindow::showLoaderInstallFinished);
        connect(m_loaderInstallService, &LoaderInstallService::installError,
                this, &MainWindow::showLoaderInstallError);
    }
    if (m_authService) {
        m_activeAccount = m_authService->offlineSession(m_settings.offlinePlayerName);
        connect(m_authService, &AuthService::deviceCodeReady, this, &MainWindow::showDeviceCode);
        connect(m_authService, &AuthService::sessionReady, this, &MainWindow::applyAuthenticatedSession);
        connect(m_authService, &AuthService::authenticationError, this, &MainWindow::showAuthenticationError);
        connect(m_authService, &AuthService::signedOut, this, [this]() {
            m_activeAccount = m_authService->offlineSession(m_settings.offlinePlayerName);
            updateAccountUi();
            updateHome();
            setStatus(QStringLiteral("Выполнен выход из Microsoft; активен офлайн-профиль."));
        });
    }
    if (m_launchService) {
        connect(m_launchService, &LaunchService::launchStarted, this, &MainWindow::showLaunchStarted);
        connect(m_launchService, &LaunchService::launchExited, this, &MainWindow::showLaunchExited);
        connect(m_launchService, &LaunchService::launchError, this, &MainWindow::showLaunchError);
        connect(m_launchService, &LaunchService::logLine, this, [this](const QString &, const QString &line) {
            Logger::info(QStringLiteral("Minecraft: %1").arg(line));
        });
    }
    if (m_javaRuntimeService) {
        connect(m_javaRuntimeService, &JavaRuntimeService::runtimeQueryStarted, this, [this](int major) {
            setStatus(QStringLiteral("Подбирается локальная Java %1…").arg(major), 0);
            refreshJavaStatus();
        });
        connect(m_javaRuntimeService, &JavaRuntimeService::runtimeInstallStarted, this, [this](int major) {
            setStatus(QStringLiteral("Распаковка Java %1 в папку Atlas…").arg(major), 0);
            refreshJavaStatus();
        });
        connect(m_javaRuntimeService, &JavaRuntimeService::runtimeReady, this, [this](const JavaRuntimeInfo &runtime) {
            if (m_javaPath && m_javaPath->text().trimmed().isEmpty()) m_javaPath->setText(runtime.javawPath);
            setStatus(QStringLiteral("Локальная Java %1 готова").arg(runtime.major));
            refreshJavaStatus();
            updateHome();

            const QString pendingVanillaId = m_pendingVanillaInstanceId;
            m_pendingVanillaInstanceId.clear();
            if (!pendingVanillaId.isEmpty() && m_minecraftInstallService && !m_minecraftInstallService->isInstalling()) {
                const auto found = std::find_if(m_instances.cbegin(), m_instances.cend(), [&pendingVanillaId](const Instance &item) {
                    return item.id == pendingVanillaId;
                });
                if (found != m_instances.cend()) {
                    m_minecraftInstallService->installVanilla(*found);
                    setStatus(QStringLiteral("Устанавливается Minecraft %1 для «%2»").arg(found->minecraftVersion, found->name), 0);
                    showPage(3);
                } else {
                    if (m_installVanillaButton) m_installVanillaButton->setEnabled(true);
                    setStatus(QStringLiteral("Не найдена сборка, ожидавшая установки Minecraft."));
                }
            }

            continuePendingLoaderInstallation();
            if (!m_pendingLaunchInstanceId.isEmpty()) {
                const QString pendingId = m_pendingLaunchInstanceId;
                m_pendingLaunchInstanceId.clear();
                if (m_instanceList) {
                    for (int row = 0; row < m_instanceList->count(); ++row) {
                        QListWidgetItem *item = m_instanceList->item(row);
                        if (item && item->data(Qt::UserRole).toString() == pendingId) {
                            m_instanceList->setCurrentRow(row);
                            break;
                        }
                    }
                }
                QTimer::singleShot(0, this, &MainWindow::launchSelected);
            }
        });
        connect(m_javaRuntimeService, &JavaRuntimeService::runtimeError, this, [this](int major, const QString &message) {
            const bool blockedInstallation = !m_pendingVanillaInstanceId.isEmpty();
            if (blockedInstallation) {
                m_pendingVanillaInstanceId.clear();
                m_pendingLoaderInstanceId.clear();
                m_pendingLoaderVanillaReady = false;
                if (m_installVanillaButton) m_installVanillaButton->setEnabled(true);
            }
            QMessageBox::warning(this, QStringLiteral("Не удалось установить Java %1").arg(major), message);
            setStatus(QStringLiteral("Ошибка установки Java %1").arg(major));
            refreshJavaStatus();
        });
    }
    buildUi();
    updateAccountUi();
    reloadInstances();
    refreshJavaStatus();
    refreshDownloadQueue();
    refreshMinecraftVersions();
    statusBar()->showMessage(QStringLiteral("Загружается список версий Minecraft…"));
    // Не позволяем начальному размеру окна выйти за рабочую область маленького
    // экрана (включая 1024×768), а содержимое остаётся адаптивным до 820 px.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1200, 740);
    const int availableWidth = qMax(480, available.width() - 20);
    const int availableHeight = qMax(360, available.height() - 40);
    resize(qMin(1200, availableWidth), qMin(740, availableHeight));
    setMinimumSize(qMin(760, availableWidth), qMin(520, availableHeight));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
    QCoreApplication::quit();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setMinimumWidth(168);
    sidebar->setMaximumWidth(204);
    sidebar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 22, 16, 18);
    sidebarLayout->setSpacing(6);

    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(7);
    brandRow->addWidget(label(QStringLiteral("A"), QStringLiteral("brandMark")));
    brandRow->addWidget(label(QStringLiteral("ATLAS"), QStringLiteral("brand")));
    brandRow->addStretch();
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addWidget(label(QStringLiteral("Личный игровой центр"), QStringLiteral("muted")));
    sidebarLayout->addSpacing(22);

    const QStringList navigation{
        QStringLiteral("⌂   Главная"),
        QStringLiteral("▣   Библиотека"),
        QStringLiteral("◇   Каталог контента"),
        QStringLiteral("↓   Загрузки"),
        QStringLiteral("⚙   Настройки")
    };
    for (int index = 0; index < navigation.size(); ++index) {
        auto *navButton = button(navigation.at(index), QStringLiteral("navButton"));
        navButton->setProperty("active", index == 0);
        navButton->setMinimumHeight(40);
        m_navButtons.append(navButton);
        sidebarLayout->addWidget(navButton);
        connect(navButton, &QPushButton::clicked, this, [this, index]() { showPage(index); });
    }

    sidebarLayout->addStretch();
    sidebarLayout->addWidget(lineSeparator());
    auto *offline = label(QStringLiteral("●  ОФЛАЙН-ЦЕНТР\nЛокальные экземпляры доступны без сети"));
    offline->setObjectName(QStringLiteral("muted"));
    offline->setMargin(4);
    sidebarLayout->addWidget(offline);
    rootLayout->addWidget(sidebar);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *topBar = new QFrame;
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(58);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(24, 10, 24, 10);
    topLayout->addWidget(label(QStringLiteral("ATLAS LAUNCHER"), QStringLiteral("muted")));
    topLayout->addStretch();
    m_accountLabel = label(QStringLiteral("Офлайн-профиль"), QStringLiteral("accent"));
    m_accountLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_accountLabel->setMinimumWidth(0);
    m_accountLabel->setToolTip(QStringLiteral("Активный игровой профиль"));
    topLayout->addWidget(m_accountLabel, 1);
    m_accountButton = button(QStringLiteral("Аккаунт"));
    m_accountButton->setMinimumHeight(32);
    connect(m_accountButton, &QPushButton::clicked, this, &MainWindow::manageAccount);
    topLayout->addWidget(m_accountButton);
    contentLayout->addWidget(topBar);

    auto wrapPage = [](QWidget *page) -> QScrollArea * {
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        return scroll;
    };
    m_pages = new QStackedWidget;
    m_pages->addWidget(wrapPage(buildHomePage()));
    m_pages->addWidget(wrapPage(buildLibraryPage()));
    m_pages->addWidget(wrapPage(buildCatalogPage()));
    m_pages->addWidget(wrapPage(buildDownloadsPage()));
    m_pages->addWidget(buildSettingsPage());
    contentLayout->addWidget(m_pages, 1);
    rootLayout->addWidget(content, 1);
    setCentralWidget(central);
}

QWidget *MainWindow::makeCard(const QString &objectName)
{
    auto *card = new QFrame;
    card->setObjectName(objectName);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);
    return card;
}

QWidget *MainWindow::buildHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(18);

    auto *heading = new QVBoxLayout;
    heading->setSpacing(2);
    heading->addWidget(label(QStringLiteral("Главная"), QStringLiteral("pageTitle")));
    heading->addWidget(label(QStringLiteral("Ваши игры, сборки и миры — в одном аккуратном месте."), QStringLiteral("pageSubtitle")));
    layout->addLayout(heading);

    auto *hero = new QFrame;
    hero->setObjectName(QStringLiteral("heroCard"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(24, 22, 24, 22);
    heroLayout->setSpacing(16);
    auto *heroText = new QVBoxLayout;
    heroText->setSpacing(5);
    heroText->addWidget(label(QStringLiteral("ВЫБРАННЫЙ ЭКЗЕМПЛЯР"), QStringLiteral("accent")));
    m_selectedName = label(QStringLiteral("Нет экземпляра"), QStringLiteral("cardTitle"));
    m_selectedName->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 700;"));
    heroText->addWidget(m_selectedName);
    m_selectedMeta = label(QStringLiteral("Создайте первый профиль в библиотеке"), QStringLiteral("heroVersion"));
    heroText->addWidget(m_selectedMeta);
    m_selectedPath = label(QString(), QStringLiteral("muted"));
    m_selectedPath->setWordWrap(true);
    m_selectedPath->setMinimumWidth(0);
    heroText->addWidget(m_selectedPath);
    heroLayout->addLayout(heroText, 1);
    m_launchButton = button(QStringLiteral("▶   Запустить"), QStringLiteral("primaryButton"));
    m_launchButton->setMinimumWidth(150);
    m_launchButton->setEnabled(false);
    connect(m_launchButton, &QPushButton::clicked, this, &MainWindow::launchSelected);
    heroLayout->addWidget(m_launchButton, 0, Qt::AlignVCenter);
    layout->addWidget(hero);

    auto *stats = new QHBoxLayout;
    stats->setSpacing(12);
    auto *libraryCard = qobject_cast<QFrame *>(makeCard(QStringLiteral("statCard")));
    m_libraryCount = label(QStringLiteral("0"), QStringLiteral("statValue"));
    libraryCard->layout()->addWidget(label(QStringLiteral("ЭКЗЕМПЛЯРЫ"), QStringLiteral("muted")));
    libraryCard->layout()->addWidget(m_libraryCount);
    libraryCard->layout()->addWidget(label(QStringLiteral("Локальные игровые профили"), QStringLiteral("muted")));
    stats->addWidget(libraryCard);

    auto *javaCard = qobject_cast<QFrame *>(makeCard(QStringLiteral("statCard")));
    m_javaStatus = label(QStringLiteral("Авто-поиск"), QStringLiteral("statValue"));
    m_javaStatus->setStyleSheet(QStringLiteral("font-size: 13pt; font-weight: 700;"));
    javaCard->layout()->addWidget(label(QStringLiteral("JAVA"), QStringLiteral("muted")));
    javaCard->layout()->addWidget(m_javaStatus);
    javaCard->layout()->addWidget(label(QStringLiteral("Среда запуска"), QStringLiteral("muted")));
    stats->addWidget(javaCard);

    auto *securityCard = qobject_cast<QFrame *>(makeCard(QStringLiteral("statCard")));
    securityCard->layout()->addWidget(label(QStringLiteral("РЕЖИМ"), QStringLiteral("muted")));
    securityCard->layout()->addWidget(label(QStringLiteral("Офлайн"), QStringLiteral("statValue")));
    securityCard->layout()->addWidget(label(QStringLiteral("Файлы остаются на этом ПК"), QStringLiteral("muted")));
    stats->addWidget(securityCard);
    layout->addLayout(stats);

    auto *infoCard = qobject_cast<QFrame *>(makeCard());
    infoCard->layout()->addWidget(label(QStringLiteral("Быстрый старт"), QStringLiteral("cardTitle")));
    infoCard->layout()->addWidget(label(QStringLiteral("Откройте библиотеку, создайте профиль Vanilla, Fabric, Legacy Fabric, Quilt, Forge или NeoForge, затем добавьте моды и ресурспаки из каталога. Импорт файловой сборки выполняется без обязательного подключения к сети."), QStringLiteral("muted")));
    auto *libraryButton = button(QStringLiteral("Перейти в библиотеку"));
    connect(libraryButton, &QPushButton::clicked, this, [this]() { showPage(1); });
    auto *quickAction = new QHBoxLayout;
    quickAction->addWidget(libraryButton);
    quickAction->addStretch();
    static_cast<QVBoxLayout *>(infoCard->layout())->addLayout(quickAction);
    layout->addWidget(infoCard);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::buildLibraryPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(16);

    auto *heading = new QHBoxLayout;
    auto *headingText = new QVBoxLayout;
    headingText->addWidget(label(QStringLiteral("Библиотека"), QStringLiteral("pageTitle")));
    headingText->addWidget(label(QStringLiteral("Управляйте всеми локальными экземплярами и сборками."), QStringLiteral("pageSubtitle")));
    heading->addLayout(headingText);
    heading->addStretch();
    auto *newButton = button(QStringLiteral("＋  Новая сборка"), QStringLiteral("primaryButton"));
    newButton->setToolTip(QStringLiteral("Выбрать версию и Vanilla / Fabric / Quilt / Forge / NeoForge, затем сразу начать установку"));
    connect(newButton, &QPushButton::clicked, this, &MainWindow::createInstance);
    heading->addWidget(newButton, 0, Qt::AlignBottom);
    layout->addLayout(heading);

    auto *versionCard = qobject_cast<QFrame *>(makeCard());
    // Карточка должна всегда показывать ряд действий: без минимальной высоты
    // QScrollArea мог сжать кнопки и сделать повторную установку недоступной.
    versionCard->setMinimumHeight(136);
    versionCard->layout()->addWidget(label(QStringLiteral("Установка Minecraft и загрузчиков"), QStringLiteral("cardTitle")));
    m_versionsStatus = label(QStringLiteral("Список официальных версий Mojang загружается…"), QStringLiteral("muted"));
    versionCard->layout()->addWidget(m_versionsStatus);
    auto *versionActions = new QHBoxLayout;
    m_refreshVersionsButton = button(QStringLiteral("Обновить версии"));
    connect(m_refreshVersionsButton, &QPushButton::clicked, this, &MainWindow::refreshMinecraftVersions);
    m_installVanillaButton = button(QStringLiteral("Установить для выбранного"), QStringLiteral("primaryButton"));
    m_installVanillaButton->setEnabled(false);
    connect(m_installVanillaButton, &QPushButton::clicked, this, &MainWindow::installSelectedVanilla);
    versionActions->addWidget(m_refreshVersionsButton);
    versionActions->addWidget(m_installVanillaButton);
    versionActions->addStretch();
    static_cast<QVBoxLayout *>(versionCard->layout())->addLayout(versionActions);
    layout->addWidget(versionCard);

    auto *card = qobject_cast<QFrame *>(makeCard());
    m_instanceList = new QListWidget;
    m_instanceList->setMinimumHeight(240);
    connect(m_instanceList, &QListWidget::itemClicked, this, &MainWindow::selectInstance);
    card->layout()->addWidget(m_instanceList);
    m_libraryEmptyState = label(QStringLiteral("Сборок пока нет. Нажмите «Новая сборка», выберите Minecraft и загрузчик — Atlas подготовит Java и игровые файлы."), QStringLiteral("muted"));
    m_libraryEmptyState->setAlignment(Qt::AlignCenter);
    m_libraryEmptyState->setMinimumHeight(240);
    m_libraryEmptyState->setVisible(false);
    card->layout()->addWidget(m_libraryEmptyState);
    auto *actions = new QHBoxLayout;
    auto *refreshButton = button(QStringLiteral("Обновить"));
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::reloadInstances);
    actions->addWidget(refreshButton);
    auto *editButton = button(QStringLiteral("Изменить сборку"));
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editSelectedInstance);
    actions->addWidget(editButton);
    auto *importButton = button(QStringLiteral("Импортировать пакет"));
    connect(importButton, &QPushButton::clicked, this, &MainWindow::importPackage);
    actions->addWidget(importButton);
    actions->addStretch();
    static_cast<QVBoxLayout *>(card->layout())->addLayout(actions);
    layout->addWidget(card, 1);

    auto *note = label(QStringLiteral("Vanilla устанавливается из официальных metadata API. Для Fabric, Legacy Fabric и Quilt Atlas сначала подготовит Vanilla-основу, затем загрузит launcher profile и библиотеки; Forge и NeoForge используют проверяемые installer JAR из официальных Maven. Java ставится локально в папку Atlas."), QStringLiteral("muted"));
    layout->addWidget(note);
    return page;
}

QWidget *MainWindow::buildCatalogPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(16);
    layout->addWidget(label(QStringLiteral("Каталог контента"), QStringLiteral("pageTitle")));
    layout->addWidget(label(QStringLiteral("Ищите и устанавливайте проверяемые моды, ресурспаки и шейдеры. Параметры справа реально ограничивают официальный запрос."), QStringLiteral("pageSubtitle")));

    auto *catalogArea = new QWidget;
    auto *catalogLayout = new QHBoxLayout(catalogArea);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(12);
    auto *mainColumn = new QVBoxLayout;
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(16);

    auto *toolbar = qobject_cast<QFrame *>(makeCard());
    // Две короткие строки вместо одной жёсткой: на узком экране поле поиска
    // остаётся полезным, а источник и тип не выталкивают карточку вправо.
    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(0, 0, 0, 0);
    m_catalogSearch = new QLineEdit;
    m_catalogSearch->setPlaceholderText(QStringLiteral("Поиск по названию или проекту..."));
    m_catalogSearch->setMinimumWidth(0);
    searchRow->addWidget(m_catalogSearch, 1);
    auto *searchButton = button(QStringLiteral("Найти"), QStringLiteral("primaryButton"));
    searchRow->addWidget(searchButton);
    static_cast<QVBoxLayout *>(toolbar->layout())->addLayout(searchRow);
    auto *sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins(0, 0, 0, 0);
    m_catalogProvider = new QComboBox;
    m_catalogProvider->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_catalogProvider->addItem(QStringLiteral("Modrinth"));
    m_catalogProvider->addItem(QStringLiteral("CurseForge · API-ключ"));
    sourceRow->addWidget(m_catalogProvider, 1);
    m_catalogType = new QComboBox;
    m_catalogType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_catalogType->addItem(QStringLiteral("Весь контент"), static_cast<int>(ContentType::All));
    m_catalogType->addItem(QStringLiteral("Моды"), static_cast<int>(ContentType::Mod));
    m_catalogType->addItem(QStringLiteral("Сборки"), static_cast<int>(ContentType::Modpack));
    m_catalogType->addItem(QStringLiteral("Ресурспаки"), static_cast<int>(ContentType::ResourcePack));
    m_catalogType->addItem(QStringLiteral("Шейдеры"), static_cast<int>(ContentType::Shader));
    m_catalogType->addItem(QStringLiteral("Миры и карты"), static_cast<int>(ContentType::World));
    sourceRow->addWidget(m_catalogType, 1);
    static_cast<QVBoxLayout *>(toolbar->layout())->addLayout(sourceRow);

    // Ключ виден именно там, где пользователь выбирает CurseForge. Он существует
    // только в памяти процесса и не передаётся в LauncherSettings.
    m_curseForgeKeyPanel = new QWidget;
    auto *curseForgeKeyLayout = new QVBoxLayout(m_curseForgeKeyPanel);
    curseForgeKeyLayout->setContentsMargins(0, 0, 0, 0);
    curseForgeKeyLayout->setSpacing(5);
    curseForgeKeyLayout->addWidget(label(QStringLiteral("CurseForge API-ключ:")));
    m_curseForgeKeyInline = new QLineEdit;
    m_curseForgeKeyInline->setEchoMode(QLineEdit::Password);
    m_curseForgeKeyInline->setPlaceholderText(QStringLiteral("Только для текущего сеанса; не сохраняется"));
    m_curseForgeKeyInline->setToolTip(QStringLiteral("Ключ используется только для официальных запросов CurseForge в текущем запуске Atlas."));
    curseForgeKeyLayout->addWidget(m_curseForgeKeyInline);
    m_curseForgeKeyPanel->setVisible(false);
    static_cast<QVBoxLayout *>(toolbar->layout())->addWidget(m_curseForgeKeyPanel);
    mainColumn->addWidget(toolbar);

    auto *results = qobject_cast<QFrame *>(makeCard());
    results->layout()->addWidget(label(QStringLiteral("Результаты"), QStringLiteral("cardTitle")));
    m_catalogStatus = label(QStringLiteral("Введите запрос. Modrinth и CurseForge используют официальные API; CurseForge требует ваш персональный ключ только на время сеанса."), QStringLiteral("muted"));
    results->layout()->addWidget(m_catalogStatus);
    m_catalogList = new QListWidget;
    m_catalogList->setMinimumHeight(250);
    m_catalogList->setSelectionMode(QAbstractItemView::SingleSelection);
    results->layout()->addWidget(m_catalogList);
    auto *catalogActions = new QVBoxLayout;
    m_catalogInstallButton = button(QStringLiteral("Установить в выбранный экземпляр"), QStringLiteral("primaryButton"));
    m_catalogInstallButton->setToolTip(QStringLiteral("Устанавливает официальный файл с обязательной проверкой SHA-512 или SHA-1 в папку текущего экземпляра"));
    m_catalogInstallButton->setMinimumWidth(0);
    m_catalogInstallButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_catalogInstallButton, &QPushButton::clicked, this, &MainWindow::installSelectedCatalogProject);
    catalogActions->addWidget(m_catalogInstallButton, 0, Qt::AlignLeft);
    auto *pagingRow = new QHBoxLayout;
    m_catalogPreviousPage = button(QStringLiteral("◀ Предыдущая"));
    m_catalogPreviousPage->setMinimumWidth(0);
    m_catalogPreviousPage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_catalogNextPage = button(QStringLiteral("Следующая ▶"));
    m_catalogNextPage->setMinimumWidth(0);
    m_catalogNextPage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_catalogPageLabel = label(QStringLiteral("Страница 1"), QStringLiteral("muted"));
    m_catalogPageLabel->setMinimumWidth(0);
    m_catalogPageLabel->setAlignment(Qt::AlignCenter);
    pagingRow->addWidget(m_catalogPreviousPage);
    pagingRow->addWidget(m_catalogPageLabel, 1);
    pagingRow->addWidget(m_catalogNextPage);
    catalogActions->addLayout(pagingRow);
    static_cast<QVBoxLayout *>(results->layout())->addLayout(catalogActions);
    mainColumn->addWidget(results, 1);

    auto *filters = qobject_cast<QFrame *>(makeCard());
    filters->setMinimumWidth(205);
    filters->setMaximumWidth(250);
    filters->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    filters->layout()->addWidget(label(QStringLiteral("Параметры поиска"), QStringLiteral("cardTitle")));
    filters->layout()->addWidget(label(QStringLiteral("Фильтры применяются к следующему запросу. «Авто» берёт версию и загрузчик выбранной сборки."), QStringLiteral("muted")));
    // Подписи расположены над контролами: так параметры остаются целиком
    // читаемыми даже на распространённом разрешении 1366×768.
    auto *filterLayout = new QVBoxLayout;
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(7);
    m_catalogGameVersion = new QComboBox;
    m_catalogGameVersion->addItem(QStringLiteral("Авто (выбранная сборка)"), QStringLiteral("auto"));
    m_catalogGameVersion->addItem(QStringLiteral("Любая версия"), QStringLiteral("any"));
    filterLayout->addWidget(label(QStringLiteral("Версия Minecraft")));
    filterLayout->addWidget(m_catalogGameVersion);
    m_catalogLoader = new QComboBox;
    m_catalogLoader->addItem(QStringLiteral("Авто (выбранная сборка)"), QStringLiteral("auto"));
    m_catalogLoader->addItem(QStringLiteral("Не фильтровать"), QStringLiteral("any"));
    m_catalogLoader->addItem(QStringLiteral("Fabric"), static_cast<int>(LoaderKind::Fabric));
    m_catalogLoader->addItem(QStringLiteral("Legacy Fabric"), static_cast<int>(LoaderKind::LegacyFabric));
    m_catalogLoader->addItem(QStringLiteral("Forge"), static_cast<int>(LoaderKind::Forge));
    m_catalogLoader->addItem(QStringLiteral("NeoForge"), static_cast<int>(LoaderKind::NeoForge));
    m_catalogLoader->addItem(QStringLiteral("Quilt"), static_cast<int>(LoaderKind::Quilt));
    filterLayout->addWidget(label(QStringLiteral("Загрузчик")));
    filterLayout->addWidget(m_catalogLoader);
    m_catalogSort = new QComboBox;
    m_catalogSort->addItem(QStringLiteral("Рекомендуемые"), QStringLiteral("recommended"));
    m_catalogSort->addItem(QStringLiteral("По скачиваниям"), QStringLiteral("downloads"));
    m_catalogSort->addItem(QStringLiteral("Недавно обновлённые"), QStringLiteral("updated"));
    m_catalogSort->addItem(QStringLiteral("Новые"), QStringLiteral("newest"));
    m_catalogSort->addItem(QStringLiteral("По популярности"), QStringLiteral("popular"));
    m_catalogSort->setToolTip(QStringLiteral("Atlas передаёт соответствующий официальный параметр сортировки выбранному источнику."));
    filterLayout->addWidget(label(QStringLiteral("Сортировка")));
    filterLayout->addWidget(m_catalogSort);
    m_catalogCategory = new QComboBox;
    m_catalogCategory->addItem(QStringLiteral("Все категории"));
    filterLayout->addWidget(label(QStringLiteral("Категория")));
    filterLayout->addWidget(m_catalogCategory);
    static_cast<QVBoxLayout *>(filters->layout())->addLayout(filterLayout);
    static_cast<QVBoxLayout *>(filters->layout())->addStretch();

    catalogLayout->addLayout(mainColumn, 1);
    catalogLayout->addWidget(filters);
    layout->addWidget(catalogArea, 1);

    connect(m_curseForgeKeyInline, &QLineEdit::textChanged, this, [this](const QString &key) {
        if (m_curseForgeKey && m_curseForgeKey->text() != key) m_curseForgeKey->setText(key);
        if (m_catalogProvider && m_catalogProvider->currentIndex() == 1) {
            // Повторный ввод ключа должен заново запросить официальные категории,
            // но не на каждый введённый символ.
            m_curseForgeCategoryClassId = -1;
        }
    });
    connect(m_curseForgeKeyInline, &QLineEdit::editingFinished, this, [this]() {
        if (m_catalogProvider && m_catalogProvider->currentIndex() == 1) refreshCatalogCategories();
    });
    connect(m_catalogProvider, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool isCurseForge = index == 1;
        if (m_curseForgeKeyPanel) m_curseForgeKeyPanel->setVisible(isCurseForge);
        if (m_catalogStatus) m_catalogStatus->setText(isCurseForge
            ? QStringLiteral("Введите временный API-ключ CurseForge, настройте фильтры и укажите запрос. Ключ не сохраняется.")
            : QStringLiteral("Введите запрос и настройте фильтры. Modrinth не требует API-ключа."));
        if (isCurseForge && m_curseForgeKeyInline) m_curseForgeKeyInline->setFocus();
        m_catalogPage = 0;
        refreshCatalogCategories();
        updateCatalogPagination();
    });
    connect(m_catalogType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        m_catalogPage = 0;
        refreshCatalogCategories();
        updateCatalogPagination();
    });
    for (QComboBox *control : {m_catalogGameVersion, m_catalogLoader, m_catalogSort, m_catalogCategory}) {
        connect(control, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            m_catalogPage = 0;
            updateCatalogPagination();
        });
    }
    const auto startSearch = [this]() {
        m_catalogPage = 0;
        searchCatalog();
    };
    connect(searchButton, &QPushButton::clicked, this, startSearch);
    connect(m_catalogSearch, &QLineEdit::returnPressed, this, startSearch);
    connect(m_catalogPreviousPage, &QPushButton::clicked, this, [this]() {
        if (m_catalogPage <= 0) return;
        --m_catalogPage;
        searchCatalog();
    });
    connect(m_catalogNextPage, &QPushButton::clicked, this, [this]() {
        if ((m_catalogPage + 1) * m_catalogPageSize >= m_catalogTotalHits) return;
        ++m_catalogPage;
        searchCatalog();
    });
    QTimer::singleShot(0, this, [this]() {
        refreshCatalogVersionChoices();
        refreshCatalogCategories();
        updateCatalogPagination();
    });
    return page;
}

QWidget *MainWindow::buildDownloadsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(14);
    layout->addWidget(label(QStringLiteral("Загрузки"), QStringLiteral("pageTitle")));
    layout->addWidget(label(QStringLiteral("Здесь отображаются фактические операции: загрузка, проверка хеша и установка Java или игровых файлов."), QStringLiteral("pageSubtitle")));

    auto *card = qobject_cast<QFrame *>(makeCard());
    card->layout()->addWidget(label(QStringLiteral("Очередь"), QStringLiteral("cardTitle")));
    m_downloadQueueStatus = label(QStringLiteral("Очередь пуста"), QStringLiteral("muted"));
    card->layout()->addWidget(m_downloadQueueStatus);
    m_downloadQueue = new QListWidget;
    m_downloadQueue->setMinimumHeight(280);
    m_downloadQueue->setWordWrap(true);
    m_downloadQueue->setSelectionMode(QAbstractItemView::SingleSelection);
    card->layout()->addWidget(m_downloadQueue);
    auto *actions = new QHBoxLayout;
    auto *cancel = button(QStringLiteral("Отменить выбранное"));
    connect(cancel, &QPushButton::clicked, this, [this]() {
        if (!m_downloadManager || !m_downloadQueue || !m_downloadQueue->currentItem()) return;
        m_downloadManager->cancel(m_downloadQueue->currentItem()->data(Qt::UserRole).toString());
    });
    auto *clear = button(QStringLiteral("Очистить завершённые"));
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (m_downloadManager) m_downloadManager->clearFinished();
        refreshDownloadQueue();
    });
    actions->addWidget(cancel);
    actions->addWidget(clear);
    actions->addStretch();
    static_cast<QVBoxLayout *>(card->layout())->addLayout(actions);
    layout->addWidget(card, 1);
    return page;
}

QWidget *MainWindow::buildSettingsPage()
{
    auto *page = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(16);

    layout->addWidget(label(QStringLiteral("Настройки Atlas"), QStringLiteral("pageTitle")));
    layout->addWidget(label(QStringLiteral("Глобальные значения сохраняются в settings.json. Параметры Java, памяти и запуска конкретной сборки можно переопределить в её редакторе."), QStringLiteral("pageSubtitle")));

    auto *appearanceCard = qobject_cast<QFrame *>(makeCard());
    appearanceCard->layout()->addWidget(label(QStringLiteral("Внешний вид"), QStringLiteral("cardTitle")));
    auto *appearanceForm = new QFormLayout;
    appearanceForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_theme = new QComboBox;
    m_theme->addItem(QStringLiteral("Obsidian Atlas — тёмная графитовая"), QStringLiteral("obsidian"));
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(m_settings.theme)));
    appearanceForm->addRow(QStringLiteral("Цветовая схема"), m_theme);
    m_language = new QComboBox;
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_language->setCurrentIndex(qMax(0, m_language->findData(m_settings.language)));
    appearanceForm->addRow(QStringLiteral("Язык интерфейса"), m_language);
    m_enableAnimations = new QCheckBox(QStringLiteral("Включать плавные переходы, если они доступны"));
    m_enableAnimations->setChecked(m_settings.enableAnimations);
    appearanceForm->addRow(QString(), m_enableAnimations);
    static_cast<QVBoxLayout *>(appearanceCard->layout())->addLayout(appearanceForm);
    appearanceCard->layout()->addWidget(label(QStringLiteral("Atlas использует лёгкую схему без GPU-зависимых эффектов, чтобы оставаться совместимым с Windows 7 SP1 x64."), QStringLiteral("muted")));
    layout->addWidget(appearanceCard);

    auto *javaCard = qobject_cast<QFrame *>(makeCard());
    javaCard->layout()->addWidget(label(QStringLiteral("Java по умолчанию"), QStringLiteral("cardTitle")));
    auto *javaForm = new QFormLayout;
    javaForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto *managedJavaRow = new QHBoxLayout;
    m_javaMajor = new QComboBox;
    m_javaMajor->addItem(QStringLiteral("Java 8 — Minecraft до 1.16.5"), 8);
    m_javaMajor->addItem(QStringLiteral("Java 16 — Minecraft 1.17"), 16);
    m_javaMajor->addItem(QStringLiteral("Java 17 — Minecraft 1.18–1.20.4"), 17);
    m_javaMajor->addItem(QStringLiteral("Java 21 — Minecraft 1.20.5+"), 21);
    m_javaMajor->setCurrentIndex(2);
    m_installJavaButton = button(QStringLiteral("Скачать локально"), QStringLiteral("primaryButton"));
    m_installJavaButton->setToolTip(QStringLiteral("Скачивает Java в папку данных Atlas Launcher, без системной установки"));
    connect(m_installJavaButton, &QPushButton::clicked, this, &MainWindow::installSelectedJavaRuntime);
    managedJavaRow->addWidget(m_javaMajor, 1);
    managedJavaRow->addWidget(m_installJavaButton);
    javaForm->addRow(QStringLiteral("Локальная Java Atlas"), managedJavaRow);
    m_managedJavaStatus = label(QStringLiteral("Локальная Java пока не установлена"), QStringLiteral("muted"));
    javaForm->addRow(QString(), m_managedJavaStatus);
    m_javaPath = new QLineEdit(m_settings.javaPath);
    m_javaPath->setPlaceholderText(QStringLiteral("Необязательный путь к javaw.exe / java.exe"));
    auto *javaBrowse = button(QStringLiteral("Выбрать"));
    connect(javaBrowse, &QPushButton::clicked, this, &MainWindow::chooseJavaPath);
    auto *javaPathRow = new QHBoxLayout;
    javaPathRow->addWidget(m_javaPath, 1);
    javaPathRow->addWidget(javaBrowse);
    javaForm->addRow(QStringLiteral("Свой файл Java"), javaPathRow);
    static_cast<QVBoxLayout *>(javaCard->layout())->addLayout(javaForm);
    javaCard->layout()->addWidget(label(QStringLiteral("По умолчанию Atlas подбирает и устанавливает совместимую Java внутри своей папки. Собственный файл выбирайте только для особого случая."), QStringLiteral("muted")));
    layout->addWidget(javaCard);

    auto *minecraftCard = qobject_cast<QFrame *>(makeCard());
    minecraftCard->layout()->addWidget(label(QStringLiteral("Minecraft и экземпляры"), QStringLiteral("cardTitle")));
    auto *minecraftForm = new QFormLayout;
    minecraftForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_instancesPath = new QLineEdit(m_settings.instancesPath);
    m_instancesPath->setPlaceholderText(QStringLiteral("По умолчанию: папка данных AtlasLauncher"));
    minecraftForm->addRow(QStringLiteral("Папка экземпляров"), m_instancesPath);
    m_minMemory = new QSpinBox;
    m_minMemory->setRange(256, 65536);
    m_minMemory->setSuffix(QStringLiteral(" MiB"));
    m_minMemory->setValue(m_settings.minMemoryMiB);
    minecraftForm->addRow(QStringLiteral("Минимум памяти"), m_minMemory);
    m_maxMemory = new QSpinBox;
    m_maxMemory->setRange(256, 65536);
    m_maxMemory->setSuffix(QStringLiteral(" MiB"));
    m_maxMemory->setValue(m_settings.maxMemoryMiB);
    minecraftForm->addRow(QStringLiteral("Максимум памяти"), m_maxMemory);
    m_showSnapshots = new QCheckBox(QStringLiteral("Показывать Snapshots в официальном списке Minecraft"));
    m_showSnapshots->setChecked(m_settings.showSnapshots);
    minecraftForm->addRow(QString(), m_showSnapshots);
    m_showOldBeta = new QCheckBox(QStringLiteral("Показывать Beta (old_beta)"));
    m_showOldBeta->setChecked(m_settings.showOldBeta);
    minecraftForm->addRow(QString(), m_showOldBeta);
    m_showOldAlpha = new QCheckBox(QStringLiteral("Показывать Alpha (old_alpha)"));
    m_showOldAlpha->setChecked(m_settings.showOldAlpha);
    minecraftForm->addRow(QString(), m_showOldAlpha);
    static_cast<QVBoxLayout *>(minecraftCard->layout())->addLayout(minecraftForm);
    minecraftCard->layout()->addWidget(label(QStringLiteral("Release показаны всегда. Snapshots, Beta и Alpha Atlas получает из официального Mojang manifest только при включении соответствующего переключателя. Память используется как значение по умолчанию для новых профилей."), QStringLiteral("muted")));
    layout->addWidget(minecraftCard);

    auto *authCard = qobject_cast<QFrame *>(makeCard());
    authCard->layout()->addWidget(label(QStringLiteral("Авторизация"), QStringLiteral("cardTitle")));
    auto *authForm = new QFormLayout;
    authForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_microsoftClientId = new QLineEdit(m_settings.microsoftClientId);
    m_microsoftClientId->setPlaceholderText(QStringLiteral("Application (client) ID вашего приложения Microsoft"));
    authForm->addRow(QStringLiteral("Microsoft client ID"), m_microsoftClientId);
    m_offlinePlayerName = new QLineEdit(m_settings.offlinePlayerName);
    m_offlinePlayerName->setMaxLength(16);
    m_offlinePlayerName->setPlaceholderText(QStringLiteral("Player_123"));
    m_offlinePlayerName->setToolTip(QStringLiteral("От 3 до 16 символов: английские буквы, цифры и _."));
    m_offlinePlayerName->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9_]{0,16}")), m_offlinePlayerName));
    m_applyOfflineNicknameButton = button(QStringLiteral("Применить ник"));
    m_applyOfflineNicknameButton->setToolTip(QStringLiteral("Сохраняет ник и сразу переключает активный офлайн-профиль."));
    connect(m_applyOfflineNicknameButton, &QPushButton::clicked,
            this, &MainWindow::applyOfflineNickname);
    auto *offlineNicknameRow = new QHBoxLayout;
    offlineNicknameRow->addWidget(m_offlinePlayerName, 1);
    offlineNicknameRow->addWidget(m_applyOfflineNicknameButton);
    authForm->addRow(QStringLiteral("Ник офлайн-игрока"), offlineNicknameRow);
    static_cast<QVBoxLayout *>(authCard->layout())->addLayout(authForm);
    authCard->layout()->addWidget(label(QStringLiteral("Введите ник от 3 до 16 символов: английские буквы, цифры и _. Кнопка «Применить ник» сохраняет его и использует в следующем офлайн-запуске. Microsoft-вход использует имя лицензионного аккаунта — Atlas не может и не будет подменять его."), QStringLiteral("muted")));
    layout->addWidget(authCard);

    auto *networkCard = qobject_cast<QFrame *>(makeCard());
    networkCard->layout()->addWidget(label(QStringLiteral("Сеть и каталоги"), QStringLiteral("cardTitle")));
    auto *networkForm = new QFormLayout;
    networkForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_modrinthUserAgent = new QLineEdit(m_settings.modrinthUserAgent);
    m_modrinthUserAgent->setPlaceholderText(QStringLiteral("AtlasLauncher/0.2 (personal launcher)"));
    networkForm->addRow(QStringLiteral("Modrinth User-Agent"), m_modrinthUserAgent);
    m_concurrentDownloads = new QSpinBox;
    m_concurrentDownloads->setRange(1, 16);
    m_concurrentDownloads->setValue(m_settings.maxConcurrentDownloads);
    m_concurrentDownloads->setSuffix(QStringLiteral(" файлов одновременно"));
    m_concurrentDownloads->setToolTip(QStringLiteral("Библиотеки и ресурсы Minecraft скачиваются параллельно. Значение 8 обычно быстрее, не перегружая сеть и диск."));
    networkForm->addRow(QStringLiteral("Параллельные загрузки"), m_concurrentDownloads);
    m_inactivityTimeout = new QSpinBox;
    m_inactivityTimeout->setRange(15, 600);
    m_inactivityTimeout->setValue(m_settings.inactivityTimeoutSeconds);
    m_inactivityTimeout->setSuffix(QStringLiteral(" секунд"));
    m_inactivityTimeout->setToolTip(QStringLiteral("Если один HTTPS-файл не передаёт данные дольше этого времени, Atlas повторит запрос и при необходимости использует резервный источник."));
    networkForm->addRow(QStringLiteral("Тайм-аут без передачи данных"), m_inactivityTimeout);
    m_githubRepository = new QLineEdit(m_settings.githubRepository);
    m_githubRepository->setPlaceholderText(QStringLiteral("owner/repository"));
    m_githubRepository->setToolTip(QStringLiteral("Публичный GitHub-репозиторий, в Releases которого публикуются архив и SHA256SUMS.txt."));
    networkForm->addRow(QStringLiteral("GitHub Releases"), m_githubRepository);
    m_autoCheckForUpdates = new QCheckBox(QStringLiteral("Проверять обновления при запуске"));
    m_autoCheckForUpdates->setChecked(m_settings.autoCheckForUpdates);
    networkForm->addRow(QString(), m_autoCheckForUpdates);
    m_checkForUpdatesButton = button(QStringLiteral("Проверить обновления сейчас"));
    connect(m_checkForUpdatesButton, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    networkForm->addRow(QString(), m_checkForUpdatesButton);
    m_curseForgeKey = new QLineEdit;
    m_curseForgeKey->setEchoMode(QLineEdit::Password);
    m_curseForgeKey->setPlaceholderText(QStringLiteral("Только для текущего запуска"));
    if (m_curseForgeKeyInline) m_curseForgeKey->setText(m_curseForgeKeyInline->text());
    connect(m_curseForgeKey, &QLineEdit::textChanged, this, [this](const QString &key) {
        if (m_curseForgeKeyInline && m_curseForgeKeyInline->text() != key) {
            m_curseForgeKeyInline->setText(key);
        }
    });
    networkForm->addRow(QStringLiteral("CurseForge API-ключ"), m_curseForgeKey);
    static_cast<QVBoxLayout *>(networkCard->layout())->addLayout(networkForm);
    networkCard->layout()->addWidget(label(QStringLiteral("Ключ CurseForge остаётся только в оперативной памяти, не сохраняется в settings.json и не попадает в журнал. GitHub updater использует публичные Releases и не хранит токен: ZIP принимается только при совпадении SHA-256 из SHA256SUMS.txt."), QStringLiteral("muted")));
    layout->addWidget(networkCard);

    auto *advancedCard = qobject_cast<QFrame *>(makeCard());
    advancedCard->layout()->addWidget(label(QStringLiteral("Дополнительно и диагностика"), QStringLiteral("cardTitle")));
    m_verifyHashes = new QCheckBox(QStringLiteral("Проверять SHA-1/SHA-256/SHA-512 перед установкой файлов"));
    m_verifyHashes->setChecked(true);
    m_verifyHashes->setEnabled(false);
    m_verifyHashes->setToolTip(QStringLiteral("Проверка хеша обязательна для файлов официальных источников и не отключается ради безопасности."));
    advancedCard->layout()->addWidget(m_verifyHashes);
    auto *openDataFolder = button(QStringLiteral("Открыть папку данных и atlas.log"));
    connect(openDataFolder, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settingsService->dataDirectory()));
    });
    auto *clearDownloadCache = button(QStringLiteral("Очистить временный кэш загрузок"));
    connect(clearDownloadCache, &QPushButton::clicked, this, [this]() {
        if (!m_settingsService) return;
        if (m_downloadManager && m_downloadManager->hasActiveDownloads()) {
            QMessageBox::warning(this, QStringLiteral("Очистка недоступна"),
                                 QStringLiteral("Сначала дождитесь завершения или отмените активные загрузки."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Очистить временный кэш"),
                                  QStringLiteral("Будут удалены только повторно загружаемые ZIP-архивы Java и папка кэша Atlas. Установленные игры, моды, миры и настройки не изменятся."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        const QString dataDirectory = m_settingsService->dataDirectory();
        const QStringList cacheDirectories = {
            QDir(dataDirectory).filePath(QStringLiteral("runtime/downloads")),
            QDir(dataDirectory).filePath(QStringLiteral("cache"))
        };
        int removed = 0;
        for (const QString &path : cacheDirectories) {
            QDir directory(path);
            if (directory.exists() && directory.removeRecursively()) ++removed;
        }
        setStatus(removed > 0 ? QStringLiteral("Временный кэш очищен")
                              : QStringLiteral("Временный кэш уже пуст"));
    });
    auto *openDataFolderRow = new QHBoxLayout;
    openDataFolderRow->addWidget(openDataFolder);
    openDataFolderRow->addWidget(clearDownloadCache);
    openDataFolderRow->addStretch();
    static_cast<QVBoxLayout *>(advancedCard->layout())->addLayout(openDataFolderRow);
    advancedCard->layout()->addWidget(label(QStringLiteral("Журнал Atlas записывается в atlas.log. Контрольные суммы всегда включены, чтобы официальные файлы не устанавливались без проверки целостности."), QStringLiteral("muted")));
    layout->addWidget(advancedCard);

    auto *save = button(QStringLiteral("Сохранить настройки"), QStringLiteral("primaryButton"));
    connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
    layout->addWidget(save, 0, Qt::AlignLeft);
    layout->addStretch();
    scroll->setWidget(content);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    return page;
}

void MainWindow::showPage(int pageIndex)
{
    if (!m_pages || pageIndex < 0 || pageIndex >= m_pages->count()) return;
    m_currentPage = pageIndex;
    m_pages->setCurrentIndex(pageIndex);
    setActiveNavigation(pageIndex);
}

void MainWindow::setActiveNavigation(int pageIndex)
{
    for (int index = 0; index < m_navButtons.size(); ++index) {
        m_navButtons.at(index)->setProperty("active", index == pageIndex);
        m_navButtons.at(index)->style()->unpolish(m_navButtons.at(index));
        m_navButtons.at(index)->style()->polish(m_navButtons.at(index));
    }
}

void MainWindow::reloadInstances()
{
    // QListWidget::clear() снимает currentItem. Сохраняем выбор до очистки,
    // чтобы завершение установки не переключало главную карточку на первую сборку.
    const QString previouslySelectedId = !m_selectedInstanceId.isEmpty()
        ? m_selectedInstanceId : selectedInstance().id;

    QString error;
    m_instances = m_instanceService->loadAll(&error);
    if (!error.isEmpty()) Logger::warning(error);

    if (m_instanceList) {
        m_instanceList->clear();
        int selectedRow = -1;
        for (const Instance &instance : m_instances) {
            const QString loader = loaderKindToString(instance.loader.kind);
            auto *item = new QListWidgetItem(QStringLiteral("%1\n%2  ·  %3 %4")
                .arg(instance.name, instance.minecraftVersion, loader,
                     instance.loader.version.isEmpty() ? QStringLiteral("standard") : instance.loader.version));
            item->setData(Qt::UserRole, instance.id);
            item->setToolTip(instance.rootPath);
            m_instanceList->addItem(item);
            if (instance.id == previouslySelectedId) selectedRow = m_instanceList->count() - 1;
        }
        if (!m_instances.isEmpty()) {
            m_instanceList->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
        } else {
            m_selectedInstanceId.clear();
        }
        m_instanceList->setVisible(!m_instances.isEmpty());
    }
    if (m_libraryEmptyState) m_libraryEmptyState->setVisible(m_instances.isEmpty());

    if (m_libraryCount) m_libraryCount->setText(QString::number(m_instances.size()));
    selectInstance(nullptr);
}

Instance MainWindow::selectedInstance() const
{
    const QString id = !m_selectedInstanceId.isEmpty()
        ? m_selectedInstanceId
        : (m_instanceList && m_instanceList->currentItem()
            ? m_instanceList->currentItem()->data(Qt::UserRole).toString() : QString());
    for (const Instance &instance : m_instances) {
        if (instance.id == id) return instance;
    }
    return Instance();
}

void MainWindow::selectInstance(QListWidgetItem *)
{
    if (m_instanceList && m_instanceList->currentItem()) {
        m_selectedInstanceId = m_instanceList->currentItem()->data(Qt::UserRole).toString();
    }
    const Instance instance = selectedInstance();
    const bool supportedLoader = instance.loader.kind == LoaderKind::Vanilla ||
        instance.loader.kind == LoaderKind::Fabric || instance.loader.kind == LoaderKind::LegacyFabric ||
        instance.loader.kind == LoaderKind::Quilt || instance.loader.kind == LoaderKind::Forge ||
        instance.loader.kind == LoaderKind::NeoForge;
    const bool installerBusy = (m_minecraftInstallService && m_minecraftInstallService->isInstalling()) ||
        (m_loaderInstallService && m_loaderInstallService->isInstalling());
    const bool canInstall = !instance.id.isEmpty() && supportedLoader &&
        !instance.minecraftVersion.trimmed().isEmpty() && m_minecraftInstallService && !installerBusy;
    if (m_installVanillaButton) {
        m_installVanillaButton->setEnabled(canInstall);
        const QString loader = loaderKindToString(instance.loader.kind);
        m_installVanillaButton->setText(instance.loader.kind == LoaderKind::Vanilla
            ? QStringLiteral("Установить Vanilla для выбранного")
            : (supportedLoader ? QStringLiteral("Установить %1 для выбранного").arg(loader)
                               : QStringLiteral("Установка %1 пока недоступна").arg(loader)));
    }
    updateHome();
}

void MainWindow::updateHome()
{
    const Instance instance = selectedInstance();
    const bool hasInstance = !instance.id.isEmpty();
    const bool launcherBusy = m_launchService && m_launchService->isRunning();
    const bool runningSelected = launcherBusy && m_launchService->runningInstanceId() == instance.id;
    if (m_selectedName) m_selectedName->setText(hasInstance ? instance.name : QStringLiteral("Нет экземпляра"));
    if (m_selectedMeta) {
        m_selectedMeta->setText(hasInstance
            ? QStringLiteral("Minecraft %1  ·  %2 %3")
                .arg(instance.minecraftVersion, loaderKindToString(instance.loader.kind), instance.loader.version)
            : QStringLiteral("Создайте первый профиль в библиотеке"));
    }
    if (m_selectedPath) m_selectedPath->setText(hasInstance ? instance.rootPath : QString());
    if (m_launchButton) {
        m_launchButton->setText(runningSelected ? QStringLiteral("■   Остановить") : QStringLiteral("▶   Запустить"));
        const bool installing = (m_minecraftInstallService && m_minecraftInstallService->isInstalling()) ||
            (m_loaderInstallService && m_loaderInstallService->isInstalling());
        m_launchButton->setEnabled(hasInstance && !installing && (!launcherBusy || runningSelected));
    }
    if (m_javaStatus) m_javaStatus->setText(m_settings.javaPath.isEmpty() ? QStringLiteral("Локальная авто-Java") : QStringLiteral("Внешняя Java"));
}

void MainWindow::updateAccountUi()
{
    const bool microsoft = m_activeAccount.isMicrosoft();
    if (m_accountLabel) {
        m_accountLabel->setText(microsoft
            ? QStringLiteral("Лицензия: %1").arg(m_activeAccount.playerName)
            : QStringLiteral("Офлайн-профиль: %1").arg(m_activeAccount.playerName));
    }
    if (m_accountButton) {
        m_accountButton->setText(microsoft ? QStringLiteral("Выйти") : QStringLiteral("Войти Microsoft"));
        m_accountButton->setEnabled(!m_authService || !m_authService->isBusy());
    }
}

int MainWindow::requiredJavaFor(const Instance &instance) const
{
    return javaMajorForMinecraftVersion(instance.minecraftVersion);
}

void MainWindow::manageAccount()
{
    if (!m_authService) {
        QMessageBox::critical(this, QStringLiteral("Аккаунт недоступен"), QStringLiteral("Сервис авторизации Atlas не инициализирован."));
        return;
    }
    if (m_activeAccount.isMicrosoft()) {
        if (QMessageBox::question(this, QStringLiteral("Выйти из Microsoft"),
            QStringLiteral("Удалить сохранённую Microsoft-сессию с этого Windows-профиля?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_authService->signOut();
        }
        return;
    }
    const QString clientId = m_microsoftClientId ? m_microsoftClientId->text().trimmed() : m_settings.microsoftClientId;
    if (clientId.isEmpty()) {
        showPage(4);
        QMessageBox::information(this, QStringLiteral("Нужен Microsoft client ID"),
            QStringLiteral("В настройках укажите Application (client) ID собственного приложения Microsoft. Atlas не содержит чужих идентификаторов и не запрашивает пароль."));
        return;
    }
    m_accountButton->setEnabled(false);
    const AccountSession saved = m_authService->savedSession();
    if (m_authService->hasSavedMicrosoftSession() && saved.clientId == clientId) {
        m_authService->restoreMicrosoftSession(clientId);
    } else {
        m_authService->beginMicrosoftLogin(clientId);
    }
}

void MainWindow::showDeviceCode(const DeviceCodePrompt &prompt)
{
    updateAccountUi();
    QDesktopServices::openUrl(QUrl(prompt.verificationUri));
    QMessageBox::information(this, QStringLiteral("Вход Microsoft"),
        QStringLiteral("Откройте страницу Microsoft и введите код:\n\n%1\n\nАдрес: %2\n\n%3\n\nПароль не вводится в Atlas. Окно можно закрыть: Atlas дождётся подтверждения в браузере.")
            .arg(prompt.userCode, prompt.verificationUri, prompt.message));
    setStatus(QStringLiteral("Ожидается подтверждение входа Microsoft…"), 0);
}

void MainWindow::applyAuthenticatedSession(const AccountSession &session)
{
    m_activeAccount = session;
    updateAccountUi();
    updateHome();
    setStatus(QStringLiteral("Выполнен вход Microsoft: %1").arg(session.playerName));
}

void MainWindow::showAuthenticationError(const QString &message)
{
    updateAccountUi();
    QMessageBox::warning(this, QStringLiteral("Вход Microsoft не завершён"), message);
    setStatus(QStringLiteral("Ошибка авторизации Microsoft"));
}

void MainWindow::showLaunchStarted(const QString &instanceId, qint64 processId)
{
    updateHome();
    LauncherWindowBehavior behavior = LauncherWindowBehavior::KeepOpen;
    for (const Instance &instance : std::as_const(m_instances)) {
        if (instance.id == instanceId) {
            behavior = instance.launcherWindowBehavior;
            break;
        }
    }
    setStatus(QStringLiteral("Minecraft запущен для экземпляра %1 (PID %2)")
                  .arg(instanceId, QString::number(processId)), 0);
    if (behavior == LauncherWindowBehavior::Minimize) {
        showMinimized();
    } else if (behavior == LauncherWindowBehavior::CloseWindow) {
        // Java was started detached, so this only closes Atlas's window and
        // cannot terminate the game process.
        close();
    }
}

void MainWindow::showLaunchExited(const QString &instanceId, int exitCode, bool crashed)
{
    if (m_hiddenForGame) {
        m_hiddenForGame = false;
        showNormal();
        raise();
        activateWindow();
    }
    updateHome();
    setStatus(crashed
        ? QStringLiteral("Minecraft для «%1» завершился аварийно (код %2). Подробности в журнале Atlas.").arg(instanceId).arg(exitCode)
        : QStringLiteral("Minecraft для «%1» закрыт (код %2).").arg(instanceId).arg(exitCode));
}

void MainWindow::showLaunchError(const QString &, const QString &message)
{
    if (m_hiddenForGame) {
        m_hiddenForGame = false;
        showNormal();
        raise();
        activateWindow();
    }
    updateHome();
    QMessageBox::critical(this, QStringLiteral("Не удалось запустить Minecraft"), message);
    setStatus(QStringLiteral("Ошибка запуска Minecraft"));
}

void MainWindow::installSelectedJavaRuntime()
{
    if (!m_javaRuntimeService || !m_javaMajor) {
        QMessageBox::critical(this, QStringLiteral("Java Runtime недоступна"),
                              QStringLiteral("Сервис локальной Java не инициализирован."));
        return;
    }
    const int major = m_javaMajor->currentData().toInt();
    if (m_javaRuntimeService->isRuntimeReady(major)) {
        const JavaRuntimeInfo runtime = m_javaRuntimeService->installedRuntime(major);
        if (m_javaPath) m_javaPath->setText(runtime.javawPath);
        refreshJavaStatus();
        setStatus(QStringLiteral("Локальная Java %1 уже готова").arg(major));
        return;
    }
    if (m_installJavaButton) m_installJavaButton->setEnabled(false);
    m_javaRuntimeService->ensureRuntime(major);
    setStatus(QStringLiteral("Запрошена локальная Java %1").arg(major), 0);
}

void MainWindow::refreshJavaStatus()
{
    if (!m_javaRuntimeService || !m_javaMajor || !m_managedJavaStatus) return;
    const int major = m_javaMajor->currentData().toInt();
    const JavaRuntimeInfo runtime = m_javaRuntimeService->installedRuntime(major);
    if (runtime.isValid()) {
        m_managedJavaStatus->setText(QStringLiteral("Java %1 установлена локально: %2")
                                     .arg(major).arg(runtime.javawPath));
        if (m_installJavaButton) {
            m_installJavaButton->setText(QStringLiteral("Использовать локальную"));
            m_installJavaButton->setEnabled(true);
        }
    } else {
        m_managedJavaStatus->setText(QStringLiteral("Java %1 будет скачана в папку данных Atlas. Системная Java не требуется.").arg(major));
        if (m_installJavaButton) {
            m_installJavaButton->setText(QStringLiteral("Скачать локально"));
            m_installJavaButton->setEnabled(true);
        }
    }
}

void MainWindow::scheduleDownloadQueueRefresh(bool immediate)
{
    if (!m_downloadQueueRefreshTimer) return;
    if (immediate) {
        m_downloadQueueRefreshTimer->stop();
        refreshDownloadQueue();
    } else if (!m_downloadQueueRefreshTimer->isActive()) {
        m_downloadQueueRefreshTimer->start();
    }
}

void MainWindow::refreshDownloadQueue()
{
    if (!m_downloadQueue || !m_downloadQueueStatus || !m_downloadManager) return;
    int active = 0;
    int total = 0;
    const QList<DownloadTask> tasks = m_downloadManager->tasksForDisplay(120, &active, &total);

    // Запрещаем промежуточные перерисовки: раньше clear() и добавление тысяч
    // QListWidgetItem выполнялись на каждый пакет сети и могли уронить приложение.
    m_downloadQueue->setUpdatesEnabled(false);
    m_downloadQueue->clear();
    for (const DownloadTask &task : tasks) {
        QString progress;
        if (task.bytesTotal > 0) {
            progress = QStringLiteral("%1 / %2 (%3%)")
                .arg(QLocale().formattedDataSize(task.bytesReceived),
                     QLocale().formattedDataSize(task.bytesTotal),
                     QString::number((task.bytesReceived * 100) / task.bytesTotal));
        } else if (task.bytesReceived > 0) {
            progress = QLocale().formattedDataSize(task.bytesReceived);
        }
        QString caption = QStringLiteral("%1\n%2%3")
            .arg(task.request.title,
                 downloadStateToString(task.state),
                 progress.isEmpty() ? QString() : QStringLiteral("  ·  %1").arg(progress));
        if (!task.error.isEmpty()) caption += QStringLiteral("\nОшибка: %1").arg(task.error);
        auto *item = new QListWidgetItem(caption, m_downloadQueue);
        item->setData(Qt::UserRole, task.request.id);
        item->setToolTip(task.request.destinationPath);
    }
    m_downloadQueue->setUpdatesEnabled(true);
    m_downloadQueue->viewport()->update();

    if (total == 0) {
        m_downloadQueueStatus->setText(QStringLiteral("Очередь пуста"));
    } else if (active > 0) {
        m_downloadQueueStatus->setText(QStringLiteral("Активных операций: %1. Показано %2 из %3 файлов.")
                                       .arg(active).arg(tasks.size()).arg(total));
    } else {
        m_downloadQueueStatus->setText(QStringLiteral("Операций в журнале: %1. Показано %2 последних записей.")
                                       .arg(total).arg(tasks.size()));
    }
}

void MainWindow::refreshMinecraftVersions()
{
    if (!m_minecraftInstallService) {
        showMinecraftVersionsError(QStringLiteral("Сервис установки Minecraft не инициализирован."));
        return;
    }
    if (m_refreshVersionsButton) m_refreshVersionsButton->setEnabled(false);
    if (m_versionsStatus) m_versionsStatus->setText(QStringLiteral("Загружается официальный список версий Mojang…"));
    m_minecraftInstallService->refreshVersions(m_settings.showSnapshots, m_settings.showOldBeta, m_settings.showOldAlpha);
}

void MainWindow::showMinecraftVersions(const QVector<atlas::MinecraftVersionDescriptor> &versions)
{
    m_allMinecraftVersions = versions;
    m_minecraftVersions.clear();
    for (const MinecraftVersionDescriptor &version : std::as_const(m_allMinecraftVersions)) {
        const bool visible = version.type == QStringLiteral("release")
            || (version.type == QStringLiteral("snapshot") && m_settings.showSnapshots)
            || (version.type == QStringLiteral("old_beta") && m_settings.showOldBeta)
            || (version.type == QStringLiteral("old_alpha") && m_settings.showOldAlpha);
        if (visible) m_minecraftVersions.append(version);
    }
    if (m_refreshVersionsButton) m_refreshVersionsButton->setEnabled(true);
    if (m_versionsStatus) {
        QStringList categories{QStringLiteral("Release")};
        if (m_settings.showSnapshots) categories.append(QStringLiteral("Snapshots"));
        if (m_settings.showOldBeta) categories.append(QStringLiteral("Beta"));
        if (m_settings.showOldAlpha) categories.append(QStringLiteral("Alpha"));
        m_versionsStatus->setText(m_minecraftVersions.isEmpty()
            ? QStringLiteral("Официальный список версий пуст. Проверьте подключение к сети.")
            : QStringLiteral("Доступно официальных версий: %1 (%2). При создании профиля версия выбирается из этого списка.")
                .arg(m_minecraftVersions.size())
                .arg(categories.join(QStringLiteral(", "))));
    }
    refreshCatalogVersionChoices();
    setStatus(m_minecraftVersions.isEmpty()
        ? QStringLiteral("Список версий Mojang пуст")
        : QStringLiteral("Официальные версии Minecraft обновлены"));
}

void MainWindow::showMinecraftVersionsError(const QString &message)
{
    if (m_refreshVersionsButton) m_refreshVersionsButton->setEnabled(true);
    if (m_versionsStatus) m_versionsStatus->setText(QStringLiteral("Не удалось получить версии: %1").arg(message));
    setStatus(QStringLiteral("Ошибка списка версий Minecraft"));
}

void MainWindow::installSelectedVanilla()
{
    const Instance instance = selectedInstance();
    if (instance.id.isEmpty()) return;
    if (instance.loader.kind != LoaderKind::Vanilla) {
        installSelectedLoader();
        return;
    }
    if (!m_minecraftInstallService || m_minecraftInstallService->isInstalling()) return;

    const int requiredJava = requiredJavaFor(instance);

    if (m_installVanillaButton) m_installVanillaButton->setEnabled(false);
    if (m_javaRuntimeService && !m_javaRuntimeService->isRuntimeReady(requiredJava)) {
        m_pendingVanillaInstanceId = instance.id;
        m_javaRuntimeService->ensureRuntime(requiredJava);
        setStatus(QStringLiteral("Сначала скачивается локальная Java %1, затем начнётся установка Minecraft").arg(requiredJava), 0);
        showPage(3);
        return;
    }

    m_minecraftInstallService->installVanilla(instance);
    setStatus(QStringLiteral("Устанавливается Minecraft %1 для «%2»").arg(instance.minecraftVersion, instance.name), 0);
    showPage(3);
}

void MainWindow::installSelectedLoader()
{
    const Instance instance = selectedInstance();
    if (instance.id.isEmpty()) return;
    if (instance.loader.kind != LoaderKind::Fabric && instance.loader.kind != LoaderKind::LegacyFabric
        && instance.loader.kind != LoaderKind::Quilt && instance.loader.kind != LoaderKind::Forge
        && instance.loader.kind != LoaderKind::NeoForge) {
        QMessageBox::information(this, QStringLiteral("Загрузчик пока недоступен"),
            QStringLiteral("Atlas устанавливает Fabric, Legacy Fabric и Quilt через официальные launcher-profile API, а Forge и NeoForge — через проверяемые installer JAR из официальных Maven."));
        return;
    }
    if (!m_loaderInstallService || !m_minecraftInstallService || m_loaderInstallService->isInstalling() || m_minecraftInstallService->isInstalling()) return;

    const int requiredJava = requiredJavaFor(instance);
    m_pendingLoaderInstanceId = instance.id;
    m_pendingLoaderVanillaReady = false;
    if (m_installVanillaButton) m_installVanillaButton->setEnabled(false);
    if (m_javaRuntimeService && !m_javaRuntimeService->isRuntimeReady(requiredJava)) {
        m_pendingVanillaInstanceId = instance.id;
        m_javaRuntimeService->ensureRuntime(requiredJava);
        setStatus(QStringLiteral("Сначала скачивается локальная Java %1, затем Vanilla и %2 для «%3».")
                      .arg(requiredJava)
                      .arg(loaderKindToString(instance.loader.kind))
                      .arg(instance.name), 0);
        showPage(3);
        return;
    }
    m_minecraftInstallService->installVanilla(instance);
    setStatus(QStringLiteral("Подготавливается Vanilla %1, затем будет установлен %2 для «%3».")
                  .arg(instance.minecraftVersion, loaderKindToString(instance.loader.kind), instance.name), 0);
    showPage(3);
}

void MainWindow::showVanillaInstallFinished(const QString &instanceId, const QString &version)
{
    if (m_installVanillaButton) m_installVanillaButton->setEnabled(true);
    refreshDownloadQueue();
    if (m_pendingLoaderInstanceId == instanceId && m_loaderInstallService) {
        m_pendingLoaderVanillaReady = true;
        continuePendingLoaderInstallation();
        return;
    }
    setStatus(QStringLiteral("Minecraft %1 установлен").arg(version));
    for (const Instance &instance : m_instances) {
        if (instance.id == instanceId) {
            QMessageBox::information(this, QStringLiteral("Vanilla установлена"),
                QStringLiteral("Minecraft %1 для профиля «%2» загружен и проверен. Следующим шагом будет запуск через локальную Java.")
                    .arg(version, instance.name));
            break;
        }
    }
}

void MainWindow::showVanillaInstallError(const QString &instanceId, const QString &message)
{
    if (m_pendingLoaderInstanceId == instanceId) {
        m_pendingLoaderInstanceId.clear();
        m_pendingLoaderVanillaReady = false;
    }
    if (m_installVanillaButton) m_installVanillaButton->setEnabled(true);
    refreshDownloadQueue();
    const QMessageBox::StandardButton choice = QMessageBox::critical(
        this, QStringLiteral("Не удалось установить Minecraft"),
        message + QStringLiteral("\n\nСбой сети обрабатывается автоматически до двух повторных попыток, но если ошибка "
                                 "повторяется, проверьте подключение к интернету (сети Mojang: launchermeta.mojang.com, "
                                 "libraries.minecraft.net, resources.download.minecraft.net) и нажмите кнопку установки снова — "
                                 "скачанное ранее не будет загружаться заново."),
        QMessageBox::Retry | QMessageBox::Close, QMessageBox::Retry);
    if (choice == QMessageBox::Retry) installSelectedVanilla();
    setStatus(QStringLiteral("Ошибка установки Minecraft"));
}

void MainWindow::continuePendingLoaderInstallation()
{
    if (m_pendingLoaderInstanceId.isEmpty() || !m_pendingLoaderVanillaReady || !m_loaderInstallService) return;
    for (const Instance &instance : m_instances) {
        if (instance.id != m_pendingLoaderInstanceId) continue;
        QString javaExecutable;
        if (instance.loader.kind == LoaderKind::Forge || instance.loader.kind == LoaderKind::NeoForge) {
            if (!m_javaRuntimeService) return;
            const int javaMajor = requiredJavaFor(instance);
            const QString loaderName = loaderKindToString(instance.loader.kind);
            if (!m_javaRuntimeService->isRuntimeReady(javaMajor)) {
                setStatus(QStringLiteral("Vanilla готова; ожидается локальная Java %1 для installer %2.").arg(javaMajor).arg(loaderName), 0);
                return;
            }
            javaExecutable = m_javaRuntimeService->installedRuntime(javaMajor).javawPath;
            if (javaExecutable.isEmpty()) {
                setStatus(QStringLiteral("Vanilla готова, но путь к локальной Java для %1 не найден.").arg(loaderName), 0);
                return;
            }
        }
        const QString instanceId = m_pendingLoaderInstanceId;
        m_pendingLoaderInstanceId.clear();
        m_pendingLoaderVanillaReady = false;
        m_loaderInstallService->install(instance, javaExecutable);
        setStatus(QStringLiteral("Vanilla готова; начинается установка %1.")
                      .arg(loaderKindToString(instance.loader.kind)), 0);
        updateHome();
        return;
    }
    m_pendingLoaderInstanceId.clear();
    m_pendingLoaderVanillaReady = false;
}

void MainWindow::showLoaderInstallFinished(const QString &instanceId, LoaderKind kind,
                                           const QString &loaderVersion, const QString &profileId)
{
    for (Instance instance : m_instances) {
        if (instance.id != instanceId) continue;
        instance.loader.version = loaderVersion;
        QString saveError;
        if (!m_instanceService->save(instance, &saveError)) {
            showLoaderInstallError(instanceId, QStringLiteral("Загрузчик установлен, но номер версии не сохранён в экземпляре: %1").arg(saveError));
            return;
        }
        reloadInstances();
        refreshDownloadQueue();
        setStatus(QStringLiteral("%1 %2 установлен для «%3».").arg(loaderKindToString(kind), loaderVersion, instance.name));
        QMessageBox::information(this, QStringLiteral("Загрузчик установлен"),
            QStringLiteral("%1 %2 установлен для профиля «%3». Launcher profile: %4\n\nТеперь профиль можно запускать через локальную Java.")
                .arg(loaderKindToString(kind), loaderVersion, instance.name, profileId));
        return;
    }
    showLoaderInstallError(instanceId, QStringLiteral("Загрузчик установлен, но экземпляр не найден в библиотеке."));
}

void MainWindow::showLoaderInstallError(const QString &, const QString &message)
{
    m_pendingLoaderInstanceId.clear();
    m_pendingLoaderVanillaReady = false;
    if (m_installVanillaButton) m_installVanillaButton->setEnabled(true);
    refreshDownloadQueue();
    QMessageBox::critical(this, QStringLiteral("Не удалось установить загрузчик"), message);
    setStatus(QStringLiteral("Ошибка установки загрузчика"));
    updateHome();
}

void MainWindow::createInstance()
{
    Instance draft;
    draft.loader.kind = LoaderKind::Vanilla;
    draft.java.runtimeMode = JavaRuntimeMode::Automatic;
    BuildEditorDialog dialog(m_minecraftVersions, draft, m_settings.minMemoryMiB, m_settings.maxMemoryMiB,
                             m_javaRuntimeService, m_minecraftInstallService, m_loaderInstallService,
                             m_settings.showSnapshots, m_settings.showOldBeta, m_settings.showOldAlpha, this);
    if (dialog.exec() != QDialog::Accepted) return;
    draft = dialog.result();

    Instance instance = m_instanceService->create(draft.name, draft.minecraftVersion, draft.loader.kind, draft.loader.version);
    instance.rootPath = draft.rootPath;
    instance.resolutionWidth = draft.resolutionWidth;
    instance.resolutionHeight = draft.resolutionHeight;
    instance.fullscreen = draft.fullscreen;
    instance.hideLauncherOnGameStart = draft.hideLauncherOnGameStart;
    instance.launcherWindowBehavior = draft.launcherWindowBehavior;
    instance.safeMode = draft.safeMode;
    instance.java = draft.java;
    instance.gameArguments = draft.gameArguments;
    QString error;
    if (!m_instanceService->save(instance, &error)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось создать сборку"), error);
        return;
    }
    Logger::info(QStringLiteral("Created %1 instance %2").arg(loaderKindToString(instance.loader.kind), instance.id));
    reloadInstances();
    if (m_instanceList) {
        for (int row = 0; row < m_instanceList->count(); ++row) {
            auto *item = m_instanceList->item(row);
            if (item->data(Qt::UserRole).toString() == instance.id) {
                m_instanceList->setCurrentItem(item);
                selectInstance(item);
                break;
            }
        }
    }
    showPage(1);
    setStatus(QStringLiteral("Создана сборка «%1». Начинается подготовка Minecraft и локальной Java…").arg(instance.name), 0);
    QTimer::singleShot(0, this, &MainWindow::installSelectedVanilla);
}

void MainWindow::editSelectedInstance()
{
    const Instance selected = selectedInstance();
    if (selected.id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Сначала выберите сборку"),
            QStringLiteral("Выберите сборку в списке, затем нажмите «Изменить сборку»."));
        return;
    }
    BuildEditorDialog dialog(m_minecraftVersions, selected, m_settings.minMemoryMiB, m_settings.maxMemoryMiB,
                             m_javaRuntimeService, m_minecraftInstallService, m_loaderInstallService,
                             m_settings.showSnapshots, m_settings.showOldBeta, m_settings.showOldAlpha, this);
    if (dialog.exec() != QDialog::Accepted) return;
    Instance updated = dialog.result();
    QString error;
    if (!m_instanceService->save(updated, &error)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось сохранить сборку"), error);
        return;
    }
    reloadInstances();
    if (m_instanceList) {
        for (int row = 0; row < m_instanceList->count(); ++row) {
            auto *item = m_instanceList->item(row);
            if (item->data(Qt::UserRole).toString() == updated.id) {
                m_instanceList->setCurrentItem(item);
                selectInstance(item);
                break;
            }
        }
    }
    setStatus(QStringLiteral("Настройки сборки «%1» сохранены").arg(updated.name));
}

void MainWindow::importPackage()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Выберите папку с package.json"), QString());
    if (directory.isEmpty()) return;

    const PackageValidation validation = m_packageService.inspectDirectory(directory);
    if (!validation.valid) {
        QMessageBox::critical(this, QStringLiteral("Пакет не прошёл проверку"),
            validation.errors.join(QStringLiteral("\n• ")));
        return;
    }

    const PackageManifest &manifest = validation.manifest;
    const QString text = QStringLiteral("Название: %1\nВерсия Minecraft: %2\nЗагрузчик: %3 %4\nЛокальных файлов: %5\n\nСоздать экземпляр и скопировать локальные файлы?")
        .arg(manifest.name, manifest.minecraftVersion, loaderKindToString(manifest.loader.kind),
             manifest.loader.version, QString::number(manifest.files.size()));
    if (QMessageBox::question(this, QStringLiteral("Импорт файловой сборки"), text,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
        return;
    }

    Instance instance = m_instanceService->create(manifest.name, manifest.minecraftVersion,
                                                   manifest.loader.kind, manifest.loader.version);
    QString error;
    if (!m_instanceService->save(instance, &error)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось создать экземпляр"), error);
        return;
    }

    const PackageImportResult result = m_packageService.importLocalFiles(directory, manifest, instance.rootPath);
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("Импорт завершён с ошибками"),
            QStringLiteral("Экземпляр создан, но некоторые файлы не были скопированы:\n• %1")
                .arg(result.errors.join(QStringLiteral("\n• "))));
    } else if (!result.pendingRemoteFiles.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Локальная часть импортирована"),
            QStringLiteral("Скопировано файлов: %1\n\nУдалённые зависимости (%2) будут добавлены в очередь загрузок после подключения каталогов.")
                .arg(result.copiedFiles.size()).arg(result.pendingRemoteFiles.size()));
    } else {
        QMessageBox::information(this, QStringLiteral("Импорт завершён"),
            QStringLiteral("Создан экземпляр «%1». Скопировано файлов: %2.")
                .arg(instance.name).arg(result.copiedFiles.size()));
    }

    Logger::info(QStringLiteral("Imported package %1 into instance %2").arg(manifest.name, instance.id));
    reloadInstances();
    showPage(1);
    setStatus(QStringLiteral("Импортирована сборка «%1»").arg(instance.name));
}

void MainWindow::launchSelected()
{
    const Instance instance = selectedInstance();
    if (instance.id.isEmpty()) return;
    if (!m_launchService || !m_javaRuntimeService) {
        QMessageBox::critical(this, QStringLiteral("Запуск недоступен"),
                              QStringLiteral("Сервисы запуска Atlas не инициализированы."));
        return;
    }
    if (m_launchService->isRunning()) {
        if (m_launchService->runningInstanceId() == instance.id) {
            m_launchService->stop();
            setStatus(QStringLiteral("Останавливается Minecraft для «%1»…").arg(instance.name), 0);
        } else {
            QMessageBox::information(this, QStringLiteral("Minecraft уже запущен"),
                QStringLiteral("Сначала закройте запущенный экземпляр, затем выберите другой профиль."));
        }
        return;
    }
    const int requiredJava = requiredJavaFor(instance);
    QString javaPath;
    if (instance.java.runtimeMode == JavaRuntimeMode::Custom) {
        javaPath = instance.java.path.trimmed();
        if (javaPath.isEmpty() || !QFileInfo::exists(javaPath)) {
            QMessageBox::warning(this, QStringLiteral("Не найдена Java профиля"),
                QStringLiteral("Для «%1» выбрана собственная Java, но javaw.exe не найден. Измените сборку и выберите существующий файл либо включите локальную Java Atlas.").arg(instance.name));
            return;
        }
    } else {
        const JavaRuntimeInfo runtime = m_javaRuntimeService->installedRuntime(requiredJava);
        javaPath = runtime.javawPath;
        if (javaPath.isEmpty()) {
            m_pendingLaunchInstanceId = instance.id;
            m_javaRuntimeService->ensureRuntime(requiredJava);
            setStatus(QStringLiteral("Для «%1» скачивается локальная Java %2. Запуск продолжится после установки.")
                          .arg(instance.name).arg(requiredJava), 0);
            return;
        }
    }

    LaunchOptions options;
    options.instance = instance;
    options.account = m_activeAccount;
    options.javaExecutable = javaPath;
    options.minMemoryMiB = instance.java.minMemoryMiB > 0 ? instance.java.minMemoryMiB : m_settings.minMemoryMiB;
    options.maxMemoryMiB = instance.java.maxMemoryMiB > 0 ? instance.java.maxMemoryMiB : m_settings.maxMemoryMiB;
    options.extraJvmArguments = instance.java.jvmArguments;
    options.extraGameArguments = instance.gameArguments;
    m_launchService->launch(options);
    updateHome();
}

void MainWindow::saveSettings()
{
    if (m_minMemory->value() > m_maxMemory->value()) {
        QMessageBox::warning(this, QStringLiteral("Проверьте память"),
                             QStringLiteral("Минимальная память не может быть больше максимальной."));
        return;
    }
    m_settings.javaPath = m_javaPath->text().trimmed();
    m_settings.theme = m_theme ? m_theme->currentData().toString() : QStringLiteral("obsidian");
    m_settings.language = m_language ? m_language->currentData().toString() : QStringLiteral("ru");
    const bool versionCategoriesChanged = m_settings.showSnapshots != (m_showSnapshots && m_showSnapshots->isChecked())
        || m_settings.showOldBeta != (m_showOldBeta && m_showOldBeta->isChecked())
        || m_settings.showOldAlpha != (m_showOldAlpha && m_showOldAlpha->isChecked());
    m_settings.enableAnimations = m_enableAnimations && m_enableAnimations->isChecked();
    m_settings.showSnapshots = m_showSnapshots && m_showSnapshots->isChecked();
    m_settings.showOldBeta = m_showOldBeta && m_showOldBeta->isChecked();
    m_settings.showOldAlpha = m_showOldAlpha && m_showOldAlpha->isChecked();
    m_settings.offlinePlayerName = m_offlinePlayerName ? m_offlinePlayerName->text().trimmed() : QStringLiteral("Player");
    const QRegularExpression offlineNamePattern(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    if (!offlineNamePattern.match(m_settings.offlinePlayerName).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("Проверьте ник"),
                             QStringLiteral("Ник офлайн-игрока должен содержать от 3 до 16 английских букв, цифр или символов _."));
        return;
    }
    m_settings.modrinthUserAgent = m_modrinthUserAgent ? m_modrinthUserAgent->text().trimmed() : m_settings.modrinthUserAgent;
    if (m_settings.modrinthUserAgent.isEmpty()) m_settings.modrinthUserAgent = QStringLiteral("AtlasLauncher/0.2 (personal launcher)");
    m_settings.githubRepository = m_githubRepository ? m_githubRepository->text().trimmed() : QString();
    m_settings.autoCheckForUpdates = m_autoCheckForUpdates && m_autoCheckForUpdates->isChecked();
    const QString requestedInstancesPath = m_instancesPath->text().trimmed();
    QString instancesPathError;
    if (m_instanceService && !m_instanceService->setInstancesDirectory(requestedInstancesPath, &instancesPathError)) {
        QMessageBox::warning(this, QStringLiteral("Недоступная папка экземпляров"), instancesPathError);
        return;
    }
    m_settings.instancesPath = requestedInstancesPath;
    m_settings.microsoftClientId = m_microsoftClientId ? m_microsoftClientId->text().trimmed() : QString();
    m_settings.minMemoryMiB = m_minMemory->value();
    m_settings.maxMemoryMiB = m_maxMemory->value();
    m_settings.maxConcurrentDownloads = m_concurrentDownloads ? m_concurrentDownloads->value() : 8;
    m_settings.inactivityTimeoutSeconds = m_inactivityTimeout ? m_inactivityTimeout->value() : 90;
    m_settings.verifyHashes = true;

    QString error;
    if (!m_settingsService->save(m_settings, &error)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось сохранить настройки"), error);
        return;
    }
    if (m_modrinthClient) m_modrinthClient->setUserAgent(m_settings.modrinthUserAgent);
    if (m_downloadManager) {
        m_downloadManager->setMaximumConcurrentDownloads(m_settings.maxConcurrentDownloads);
        m_downloadManager->setInactivityTimeoutSeconds(m_settings.inactivityTimeoutSeconds);
    }
    if (!m_activeAccount.isMicrosoft() && m_authService) {
        m_activeAccount = m_authService->offlineSession(m_settings.offlinePlayerName);
    }
    if (versionCategoriesChanged) {
        refreshMinecraftVersions();
    } else if (!m_allMinecraftVersions.isEmpty()) {
        showMinecraftVersions(m_allMinecraftVersions);
    }
    reloadInstances();
    updateAccountUi();
    updateHome();
    setStatus(QStringLiteral("Настройки сохранены"));
}

void MainWindow::applyOfflineNickname()
{
    if (!m_offlinePlayerName || !m_settingsService || !m_authService) return;

    const QString nickname = m_offlinePlayerName->text().trimmed();
    const QRegularExpression offlineNamePattern(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    if (!offlineNamePattern.match(nickname).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("Проверьте ник"),
                             QStringLiteral("Ник офлайн-игрока должен содержать от 3 до 16 английских букв, цифр или символов _."));
        return;
    }

    LauncherSettings updatedSettings = m_settings;
    updatedSettings.offlinePlayerName = nickname;
    QString error;
    if (!m_settingsService->save(updatedSettings, &error)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось сохранить ник"), error);
        return;
    }

    m_settings = updatedSettings;
    if (!m_activeAccount.isMicrosoft()) {
        m_activeAccount = m_authService->offlineSession(nickname);
    }
    updateAccountUi();
    updateHome();
    setStatus(QStringLiteral("Офлайн-ник изменён на %1").arg(nickname));
}

void MainWindow::checkForUpdates()
{
    const QString repository = m_githubRepository ? m_githubRepository->text().trimmed() : m_settings.githubRepository;
    if (repository.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("GitHub Releases"),
                                 QStringLiteral("Укажите GitHub-репозиторий в формате owner/repository и сохраните настройки."));
        return;
    }
    QString error;
    if (!UpdateService::launchCheckProcess(repository, QCoreApplication::applicationVersion(),
                                           QCoreApplication::applicationDirPath(),
                                           m_settingsService->dataDirectory(), &error)) {
        QMessageBox::critical(this, QStringLiteral("Проверка обновлений недоступна"), error);
        return;
    }
    setStatus(QStringLiteral("Запущена проверка GitHub Releases"));
}

void MainWindow::chooseJavaPath()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите Java"),
                                                       m_javaPath->text(),
                                                       QStringLiteral("Java (javaw.exe java.exe);;Все файлы (*.*)"));
    if (!path.isEmpty()) m_javaPath->setText(path);
}

QString MainWindow::catalogMinecraftVersion() const
{
    if (!m_catalogGameVersion) return selectedInstance().minecraftVersion;
    const QString selected = m_catalogGameVersion->currentData().toString();
    if (selected == QStringLiteral("any")) return {};
    if (selected == QStringLiteral("auto")) return selectedInstance().minecraftVersion;
    return selected;
}

LoaderKind MainWindow::catalogLoader() const
{
    if (!m_catalogLoader) return selectedInstance().id.isEmpty() ? LoaderKind::Unknown : selectedInstance().loader.kind;
    const QVariant selected = m_catalogLoader->currentData();
    const QString special = selected.toString();
    if (special == QStringLiteral("any")) return LoaderKind::Unknown;
    if (special == QStringLiteral("auto")) return selectedInstance().id.isEmpty() ? LoaderKind::Unknown : selectedInstance().loader.kind;
    return static_cast<LoaderKind>(selected.toInt());
}

void MainWindow::refreshCatalogVersionChoices()
{
    if (!m_catalogGameVersion) return;
    const QString previous = m_catalogGameVersion->currentData().toString();
    QSignalBlocker blocker(m_catalogGameVersion);
    m_catalogGameVersion->clear();
    m_catalogGameVersion->addItem(QStringLiteral("Авто (выбранная сборка)"), QStringLiteral("auto"));
    m_catalogGameVersion->addItem(QStringLiteral("Любая версия"), QStringLiteral("any"));
    QStringList knownVersions;
    for (const MinecraftVersionDescriptor &version : std::as_const(m_minecraftVersions)) {
        if (!version.id.isEmpty() && !knownVersions.contains(version.id)) knownVersions.append(version.id);
    }
    for (const QString &version : knownVersions) m_catalogGameVersion->addItem(version, version);
    const int restoreIndex = m_catalogGameVersion->findData(previous);
    m_catalogGameVersion->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
}

void MainWindow::refreshCatalogCategories()
{
    if (!m_catalogCategory || !m_catalogProvider || !m_catalogType) return;
    const int classId = curseForgeClassId(static_cast<ContentType>(m_catalogType->currentData().toInt()));
    const bool useCurseForge = m_catalogProvider->currentIndex() == 1;
    const QString previous = m_catalogCategory->currentData().toString();
    QSignalBlocker blocker(m_catalogCategory);
    m_catalogCategory->clear();
    m_catalogCategory->addItem(QStringLiteral("Все категории"));
    if (useCurseForge) {
        for (const CurseForgeCategory &category : std::as_const(m_curseForgeCategories)) {
            if (category.classId == classId) m_catalogCategory->addItem(category.name, category.id);
        }
    } else {
        const ContentType type = static_cast<ContentType>(m_catalogType->currentData().toInt());
        const QString projectType = type == ContentType::Mod ? QStringLiteral("mod")
            : type == ContentType::Modpack ? QStringLiteral("modpack")
            : type == ContentType::ResourcePack ? QStringLiteral("resourcepack")
            : type == ContentType::Shader ? QStringLiteral("shader") : QString();
        for (const ModrinthCategory &category : std::as_const(m_modrinthCategories)) {
            if (projectType.isEmpty() || category.projectType.isEmpty() || category.projectType == projectType) {
                m_catalogCategory->addItem(category.name, category.name);
            }
        }
    }
    const int restoreIndex = m_catalogCategory->findData(previous);
    m_catalogCategory->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
    if (useCurseForge && classId != m_curseForgeCategoryClassId) {
        m_curseForgeCategoryClassId = classId;
        m_curseForgeCategories.clear();
        const QString key = m_curseForgeKeyInline ? m_curseForgeKeyInline->text().trimmed() : QString();
        if (!key.isEmpty() && classId > 0) {
            m_curseForgeClient->setApiKey(key);
            m_curseForgeClient->fetchMinecraftCategories(classId);
        }
    } else if (!useCurseForge && m_modrinthCategories.isEmpty()) {
        m_modrinthClient->setUserAgent(m_settings.modrinthUserAgent);
        m_modrinthClient->fetchCategories();
    }
}

void MainWindow::updateCatalogPagination()
{
    const int totalPages = qMax(1, (m_catalogTotalHits + m_catalogPageSize - 1) / m_catalogPageSize);
    if (m_catalogPage >= totalPages) m_catalogPage = totalPages - 1;
    if (m_catalogPageLabel) {
        m_catalogPageLabel->setText(m_catalogTotalHits > 0
            ? QStringLiteral("Страница %1 из %2").arg(m_catalogPage + 1).arg(totalPages)
            : QStringLiteral("Страница 1"));
    }
    if (m_catalogPreviousPage) m_catalogPreviousPage->setEnabled(m_catalogPage > 0);
    if (m_catalogNextPage) m_catalogNextPage->setEnabled(m_catalogTotalHits > (m_catalogPage + 1) * m_catalogPageSize);
}

void MainWindow::searchCatalog()
{
    const QString query = m_catalogSearch ? m_catalogSearch->text().trimmed() : QString();
    if (query.isEmpty()) {
        m_catalogStatus->setText(QStringLiteral("Введите название проекта или мода для поиска."));
        return;
    }
    if (m_catalogProvider && m_catalogProvider->currentIndex() == 1) {
        CurseForgeSearchFilter curseFilter;
        curseFilter.query = query;
        curseFilter.minecraftVersion = catalogMinecraftVersion();
        const ContentType type = static_cast<ContentType>(m_catalogType->currentData().toInt());
        curseFilter.classId = curseForgeClassId(type);
        curseFilter.categoryId = m_catalogCategory ? m_catalogCategory->currentData().toInt() : 0;
        curseFilter.loader = catalogLoader();
        curseFilter.requireLoaderMatch = type == ContentType::Mod && curseFilter.loader != LoaderKind::Vanilla;
        const QString sort = m_catalogSort ? m_catalogSort->currentData().toString() : QStringLiteral("recommended");
        curseFilter.sortField = sort == QStringLiteral("updated") ? 3
            : sort == QStringLiteral("newest") ? 11
            : sort == QStringLiteral("popular") ? 2
            : sort == QStringLiteral("downloads") ? 6 : 1;
        curseFilter.sortAscending = false;
        curseFilter.pageSize = m_catalogPageSize;
        curseFilter.index = m_catalogPage * m_catalogPageSize;
        const QString curseForgeKey = m_curseForgeKeyInline
            ? m_curseForgeKeyInline->text().trimmed()
            : (m_curseForgeKey ? m_curseForgeKey->text().trimmed() : QString());
        if (curseForgeKey.isEmpty()) {
            m_catalogStatus->setText(QStringLiteral("Для поиска CurseForge введите временный API-ключ в панели над результатами."));
            if (m_curseForgeKeyInline) m_curseForgeKeyInline->setFocus();
            return;
        }
        m_curseForgeClient->setApiKey(curseForgeKey);
        m_catalogStatus->setText(QStringLiteral("Идёт поиск в CurseForge…"));
        if (m_catalogList) m_catalogList->clear();
        m_curseForgeClient->searchMinecraft(curseFilter);
        setStatus(QStringLiteral("CurseForge: поиск «%1»").arg(query));
        return;
    }

    ModrinthSearchFilter filter;
    filter.query = query;
    filter.minecraftVersion = catalogMinecraftVersion();
    filter.loader = catalogLoader();
    filter.type = static_cast<ContentType>(m_catalogType->currentData().toInt());
    filter.category = m_catalogCategory ? m_catalogCategory->currentData().toString() : QString();
    const QString sort = m_catalogSort ? m_catalogSort->currentData().toString() : QStringLiteral("recommended");
    filter.sortIndex = sort == QStringLiteral("recommended") ? QStringLiteral("relevance")
        : sort == QStringLiteral("popular") ? QStringLiteral("follows") : sort;
    filter.limit = m_catalogPageSize;
    filter.offset = m_catalogPage * m_catalogPageSize;

    m_catalogStatus->setText(QStringLiteral("Идёт поиск в Modrinth…"));
    if (m_catalogList) m_catalogList->clear();
    m_modrinthClient->setUserAgent(m_settings.modrinthUserAgent);
    m_modrinthClient->search(filter);
    setStatus(QStringLiteral("Modrinth: поиск «%1»").arg(query));
}

QString MainWindow::catalogDestinationDirectory(const Instance &instance, const QString &projectType) const
{
    const QDir root(instance.rootPath);
    if (projectType == QStringLiteral("mod")) return root.filePath(QStringLiteral("mods"));
    if (projectType == QStringLiteral("resourcepack")) return root.filePath(QStringLiteral("resourcepacks"));
    if (projectType == QStringLiteral("shader")) return root.filePath(QStringLiteral("shaderpacks"));
    return {};
}

void MainWindow::installSelectedCatalogProject()
{
    if (!m_catalogList || !m_catalogList->currentItem()) {
        QMessageBox::information(this, QStringLiteral("Выберите проект"),
                                 QStringLiteral("Сначала выберите строку результата каталога."));
        return;
    }
    const Instance instance = selectedInstance();
    if (instance.id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Выберите экземпляр"),
                                 QStringLiteral("Для установки контента сначала выберите профиль в библиотеке."));
        return;
    }
    const QListWidgetItem *item = m_catalogList->currentItem();
    const QString provider = item->data(Qt::UserRole + 3).toString();
    const QString projectId = item->data(Qt::UserRole).toString();
    const QString versionId = item->data(Qt::UserRole + 1).toString();
    if (projectId.isEmpty() || versionId.isEmpty()) {
        showCatalogError(QStringLiteral("В результате каталога отсутствует идентификатор файла для установки."));
        return;
    }
    // Тип записан вместе с конкретным результатом API. Нельзя подменять его
    // текущим фильтром: пользователь мог сменить фильтр после поиска, а при
    // «Весь контент» в списке одновременно находятся разные типы файлов.
    const QString projectType = item->data(Qt::UserRole + 2).toString();
    const QString destination = catalogDestinationDirectory(instance, projectType);
    if (destination.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Тип контента пока не устанавливается автоматически"),
            QStringLiteral("Atlas автоматически устанавливает только моды, ресурспаки и шейдеры. Миры и модпаки нельзя безопасно копировать как обычный файл: для них будет открыт отдельный импорт с проверкой ZIP и manifest.json."));
        return;
    }
    m_pendingCatalogInstanceId = instance.id;
    m_pendingCatalogProjectId = projectId;
    m_pendingCatalogVersionId = versionId;
    m_pendingCatalogProjectType = projectType;
    if (m_catalogInstallButton) m_catalogInstallButton->setEnabled(false);
    if (provider == QStringLiteral("modrinth")) {
        m_catalogStatus->setText(QStringLiteral("Получаются проверяемые файлы выбранной версии Modrinth…"));
        m_modrinthClient->resolveVersionFiles(projectId, versionId);
    } else if (provider == QStringLiteral("curseforge")) {
        m_catalogStatus->setText(QStringLiteral("Получается официальный файл CurseForge и его SHA-1…"));
        m_curseForgeClient->resolveFile(projectId, versionId);
    } else {
        showCatalogError(QStringLiteral("Неизвестный провайдер каталога."));
    }
}

void MainWindow::installResolvedModrinthFiles(const QString &projectId, const QString &versionId,
                                              const QVector<atlas::ModrinthFile> &files)
{
    if (projectId != m_pendingCatalogProjectId || versionId != m_pendingCatalogVersionId) return;
    const auto instanceIt = std::find_if(m_instances.cbegin(), m_instances.cend(), [this](const Instance &instance) {
        return instance.id == m_pendingCatalogInstanceId;
    });
    if (instanceIt == m_instances.cend()) {
        showCatalogError(QStringLiteral("Экземпляр для установки больше не существует."));
        return;
    }
    const QString destination = catalogDestinationDirectory(*instanceIt, m_pendingCatalogProjectType);
    const ModrinthFile *selected = nullptr;
    for (const ModrinthFile &file : files) {
        if (file.primary) {
            selected = &file;
            break;
        }
    }
    if (!selected && !files.isEmpty()) selected = &files.first();
    if (!selected || destination.isEmpty()) {
        showCatalogError(QStringLiteral("Не удалось выбрать файл Modrinth для установки."));
        return;
    }
    const QString safeName = QFileInfo(selected->fileName).fileName();
    const bool allowedFile = (m_pendingCatalogProjectType == QStringLiteral("mod") && safeName.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
        || ((m_pendingCatalogProjectType == QStringLiteral("resourcepack") || m_pendingCatalogProjectType == QStringLiteral("shader"))
            && safeName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive));
    if (safeName != selected->fileName || !allowedFile) {
        showCatalogError(QStringLiteral("Modrinth вернул файл неподходящего формата для выбранного типа контента."));
        return;
    }
    if (!QDir().mkpath(destination)) {
        showCatalogError(QStringLiteral("Не удалось создать папку назначения экземпляра."));
        return;
    }
    DownloadRequest request;
    request.id = QStringLiteral("modrinth-%1-%2").arg(projectId, QUuid::createUuid().toString(QUuid::WithoutBraces));
    request.title = QStringLiteral("Modrinth: %1").arg(safeName);
    request.url = QUrl(selected->downloadUrl);
    request.destinationPath = QDir(destination).filePath(safeName);
    request.expectedSize = selected->size;
    if (!selected->sha512.isEmpty()) {
        request.checksum = selected->sha512;
        request.checksumAlgorithm = ChecksumAlgorithm::Sha512;
    } else {
        request.checksum = selected->sha1;
        request.checksumAlgorithm = ChecksumAlgorithm::Sha1;
    }
    m_downloadManager->enqueue(request);
    m_downloadManager->start();
    const QString installedType = m_pendingCatalogProjectType;
    m_pendingCatalogInstanceId.clear();
    m_pendingCatalogProjectId.clear();
    m_pendingCatalogVersionId.clear();
    m_pendingCatalogProjectType.clear();
    if (m_catalogInstallButton) m_catalogInstallButton->setEnabled(true);
    m_catalogStatus->setText(QStringLiteral("Файл добавлен в проверяемую очередь: %1").arg(safeName));
    setStatus(QStringLiteral("Modrinth: %1 добавлен в %2").arg(safeName, installedType), 0);
    showPage(3);
}

void MainWindow::installResolvedCurseForgeFile(const atlas::CurseForgeFile &file)
{
    if (file.projectId != m_pendingCatalogProjectId || file.fileId != m_pendingCatalogVersionId) return;
    const auto instanceIt = std::find_if(m_instances.cbegin(), m_instances.cend(), [this](const Instance &instance) {
        return instance.id == m_pendingCatalogInstanceId;
    });
    if (instanceIt == m_instances.cend()) {
        showCatalogError(QStringLiteral("Экземпляр для установки больше не существует."));
        return;
    }
    const QString destination = catalogDestinationDirectory(*instanceIt, m_pendingCatalogProjectType);
    const QString safeName = QFileInfo(file.fileName).fileName();
    const bool allowedFile = (m_pendingCatalogProjectType == QStringLiteral("mod") && safeName.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
        || ((m_pendingCatalogProjectType == QStringLiteral("resourcepack") || m_pendingCatalogProjectType == QStringLiteral("shader"))
            && safeName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive));
    if (!file.isValid() || safeName != file.fileName || !allowedFile || destination.isEmpty()) {
        showCatalogError(QStringLiteral("CurseForge вернул файл неподходящего формата или без обязательной контрольной суммы SHA-1."));
        return;
    }
    if (!QDir().mkpath(destination)) {
        showCatalogError(QStringLiteral("Не удалось создать папку назначения экземпляра."));
        return;
    }
    DownloadRequest request;
    request.id = QStringLiteral("curseforge-%1-%2").arg(file.projectId, QUuid::createUuid().toString(QUuid::WithoutBraces));
    request.title = QStringLiteral("CurseForge: %1").arg(safeName);
    request.url = QUrl(file.downloadUrl);
    request.destinationPath = QDir(destination).filePath(safeName);
    request.expectedSize = file.size;
    request.checksum = file.sha1;
    request.checksumAlgorithm = ChecksumAlgorithm::Sha1;
    m_downloadManager->enqueue(request);
    m_downloadManager->start();
    const QString installedType = m_pendingCatalogProjectType;
    m_pendingCatalogInstanceId.clear();
    m_pendingCatalogProjectId.clear();
    m_pendingCatalogVersionId.clear();
    m_pendingCatalogProjectType.clear();
    if (m_catalogInstallButton) m_catalogInstallButton->setEnabled(true);
    m_catalogStatus->setText(QStringLiteral("Файл добавлен в проверяемую очередь: %1").arg(safeName));
    setStatus(QStringLiteral("CurseForge: %1 добавлен в %2").arg(safeName, installedType), 0);
    showPage(3);
}

void MainWindow::appendCatalogProject(const CatalogProject &project)
{
    if (!m_catalogList) return;
    const QString provider = project.provider.isEmpty() ? QStringLiteral("Modrinth") : project.provider;
    const QString author = project.author.trimmed().isEmpty() ? QStringLiteral("не указан") : project.author;
    const QString caption = QStringLiteral("%1\n%2  ·  %3  ·  %4 загрузок\n%5")
        .arg(project.title, provider, author, QLocale().toString(project.downloads), project.description);
    auto *item = new QListWidgetItem(caption);
    item->setData(Qt::UserRole, project.id);
    item->setData(Qt::UserRole + 1, project.latestVersionId);
    item->setData(Qt::UserRole + 2, project.type);
    item->setData(Qt::UserRole + 3, provider.compare(QStringLiteral("CurseForge"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("curseforge") : QStringLiteral("modrinth"));
    item->setData(Qt::UserRole + 4, project.iconUrl);
    item->setToolTip(project.categories.isEmpty() ? QStringLiteral("без категорий")
        : project.categories.join(QStringLiteral(", ")));

    // Миниатюры загружаются отдельно от поиска. Это не задерживает появление
    // результатов и не затрагивает файловую очередь Minecraft.
    QPixmap placeholder(48, 48);
    placeholder.fill(QColor(QStringLiteral("#25303d")));
    item->setIcon(QIcon(placeholder));
    m_catalogList->addItem(item);

    if (project.iconUrl.isEmpty()) return;
    const auto cached = m_catalogIconCache.constFind(project.iconUrl);
    if (cached != m_catalogIconCache.cend()) {
        item->setIcon(QIcon(*cached));
        return;
    }
    requestCatalogIcon(project.iconUrl);
}

void MainWindow::requestCatalogIcon(const QString &iconUrl)
{
    if (!m_catalogIconNetwork || m_catalogIconCache.contains(iconUrl)) return;
    const QUrl url(iconUrl);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) return;

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "image/avif,image/webp,image/png,image/jpeg,image/*;q=0.8");
    request.setRawHeader("User-Agent", "AtlasLauncher/0.3.2 (catalog thumbnails)");
    auto *reply = m_catalogIconNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, iconUrl]() {
        const QByteArray data = reply->readAll();
        const bool validReply = reply->error() == QNetworkReply::NoError
            && data.size() > 0 && data.size() <= 5 * 1024 * 1024;
        reply->deleteLater();
        if (!validReply) return;

        QPixmap image;
        if (!image.loadFromData(data)) return;
        const QPixmap icon = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation);
        if (icon.isNull()) return;
        if (m_catalogIconCache.size() >= 240) m_catalogIconCache.clear();
        m_catalogIconCache.insert(iconUrl, icon);
        applyCatalogIcon(iconUrl, icon);
    });
}

void MainWindow::applyCatalogIcon(const QString &iconUrl, const QPixmap &pixmap)
{
    if (!m_catalogList) return;
    const QIcon icon(pixmap);
    for (int row = 0; row < m_catalogList->count(); ++row) {
        auto *item = m_catalogList->item(row);
        if (item && item->data(Qt::UserRole + 4).toString() == iconUrl) item->setIcon(icon);
    }
}

void MainWindow::showCatalogResults(const QVector<atlas::ModrinthProject> &projects, int totalHits)
{
    if (!m_catalogList) return;
    m_catalogTotalHits = qMax(0, totalHits);
    m_catalogList->clear();
    for (const ModrinthProject &source : projects) {
        CatalogProject project;
        project.provider = QStringLiteral("Modrinth");
        project.id = source.id;
        project.slug = source.slug;
        project.title = source.title;
        project.description = source.description;
        project.author = source.author;
        project.type = source.type;
        project.categories = source.categories;
        project.gameVersions = source.gameVersions;
        project.downloads = source.downloads;
        project.iconUrl = source.iconUrl;
        project.latestVersionId = source.latestVersionId;
        appendCatalogProject(project);
    }
    m_catalogStatus->setText(projects.isEmpty()
        ? QStringLiteral("Ничего не найдено. Попробуйте снять фильтр экземпляра или изменить запрос.")
        : QStringLiteral("Показано %1 из %2 результатов. Миниатюры загружаются в фоне и кэшируются только в памяти.")
            .arg(projects.size()).arg(totalHits));
    updateCatalogPagination();
}

void MainWindow::showCurseForgeResults(const QVector<atlas::CatalogProject> &projects, int totalHits)
{
    if (!m_catalogList) return;
    m_catalogTotalHits = qMax(0, totalHits);
    m_catalogList->clear();
    for (const CatalogProject &project : projects) appendCatalogProject(project);
    m_catalogStatus->setText(projects.isEmpty()
        ? QStringLiteral("Ничего не найдено в CurseForge.")
        : QStringLiteral("Показано %1 из %2 результатов CurseForge. Миниатюры загружаются в фоне и не сохраняются на диск.")
            .arg(projects.size()).arg(totalHits));
    updateCatalogPagination();
}

void MainWindow::showCatalogError(const QString &message)
{
    m_pendingCatalogInstanceId.clear();
    m_pendingCatalogProjectId.clear();
    m_pendingCatalogVersionId.clear();
    m_pendingCatalogProjectType.clear();
    if (m_catalogInstallButton) m_catalogInstallButton->setEnabled(true);
    if (m_catalogStatus) m_catalogStatus->setText(message);
    setStatus(QStringLiteral("Ошибка каталога"));
}

void MainWindow::showRateLimit(int retryAfterSeconds)
{
    const QString message = QStringLiteral("Modrinth временно ограничил запросы. Повторите через %1 с.")
        .arg(retryAfterSeconds);
    if (m_catalogStatus) m_catalogStatus->setText(message);
    setStatus(message, 8000);
}

void MainWindow::setStatus(const QString &message, int timeout)
{
    statusBar()->showMessage(message, timeout);
}

} // namespace atlas
