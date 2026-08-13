#include "services/download_manager.h"
#include "services/minecraft_install_service.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

namespace {

class MinecraftVersionsRefreshProbe final : public QObject
{
    Q_OBJECT
public:
    explicit MinecraftVersionsRefreshProbe(QObject *parent = nullptr)
        : QObject(parent)
        , m_service(m_tempDirectory.path(), &m_downloadManager, this)
    {
        connect(&m_service, &atlas::MinecraftInstallService::versionsReady,
                this, &MinecraftVersionsRefreshProbe::onVersionsReady);
        connect(&m_service, &atlas::MinecraftInstallService::versionsError,
                this, &MinecraftVersionsRefreshProbe::onVersionsError);
        connect(&m_timeout, &QTimer::timeout, this, &MinecraftVersionsRefreshProbe::onTimeout);
        m_timeout.setSingleShot(true);
    }

    void start()
    {
        if (!m_tempDirectory.isValid()) {
            fail(QStringLiteral("Cannot create temporary directory."));
            return;
        }

        m_timeout.start(45000);
        // This is the exact path used when a user opens the editor while the
        // main-window request is still in flight and presses "Обновить".
        m_service.refreshVersions(false);
        QTimer::singleShot(0, this, [this]() { m_service.refreshVersions(false); });
    }

    int exitCode() const { return m_exitCode; }

private slots:
    void onVersionsReady(const QVector<atlas::MinecraftVersionDescriptor> &versions)
    {
        if (m_finished) return;
        m_finished = true;
        m_timeout.stop();
        if (versions.isEmpty() || !versions.first().isValid()) {
            fail(QStringLiteral("Mojang returned no valid official versions after refresh."));
            return;
        }
        QTextStream(stdout) << "MinecraftVersionsRefreshProbe OK: " << versions.size()
                            << " official versions; latest=" << versions.first().id << "\n";
        m_exitCode = 0;
        QCoreApplication::quit();
    }

    void onVersionsError(const QString &message)
    {
        if (m_finished) return;
        fail(QStringLiteral("Manifest request failed after repeated refresh: %1").arg(message));
    }

    void onTimeout()
    {
        if (!m_finished) fail(QStringLiteral("Timed out after repeated manifest refresh."));
    }

private:
    void fail(const QString &message)
    {
        if (m_finished) return;
        m_finished = true;
        m_timeout.stop();
        QTextStream(stderr) << "MinecraftVersionsRefreshProbe failed: " << message << "\n";
        QCoreApplication::quit();
    }

    QTemporaryDir m_tempDirectory;
    atlas::DownloadManager m_downloadManager;
    atlas::MinecraftInstallService m_service;
    QTimer m_timeout;
    bool m_finished = false;
    int m_exitCode = 1;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    MinecraftVersionsRefreshProbe probe;
    QTimer::singleShot(0, &probe, &MinecraftVersionsRefreshProbe::start);
    application.exec();
    return probe.exitCode();
}

#include "minecraft_versions_refresh_probe.moc"
