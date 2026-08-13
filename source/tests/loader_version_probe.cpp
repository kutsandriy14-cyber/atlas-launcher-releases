#include "services/download_manager.h"
#include "services/loader_install_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>

namespace {

struct ProbeCase {
    atlas::LoaderKind kind;
    QString minecraftVersion;
    QString label;
};

class LoaderVersionProbe final : public QObject
{
    Q_OBJECT
public:
    explicit LoaderVersionProbe(QObject *parent = nullptr)
        : QObject(parent)
        , m_temporaryDirectory()
        , m_loaderDataDirectory(m_temporaryDirectory.path() + QStringLiteral("/atlas"))
        , m_service(m_loaderDataDirectory, &m_downloadManager, this)
    {
        m_cases = {
            {atlas::LoaderKind::Fabric, QStringLiteral("1.20.1"), QStringLiteral("Fabric 1.20.1")},
            {atlas::LoaderKind::LegacyFabric, QStringLiteral("1.12.2"), QStringLiteral("Legacy Fabric 1.12.2")},
            {atlas::LoaderKind::Quilt, QStringLiteral("1.20.1"), QStringLiteral("Quilt 1.20.1")},
            {atlas::LoaderKind::Forge, QStringLiteral("1.20.1"), QStringLiteral("Forge 1.20.1")},
            {atlas::LoaderKind::NeoForge, QStringLiteral("1.20.2"), QStringLiteral("NeoForge 1.20.2")}
        };
        connect(&m_service, &atlas::LoaderInstallService::versionsReady,
                this, &LoaderVersionProbe::onVersionsReady);
        connect(&m_service, &atlas::LoaderInstallService::versionsError,
                this, &LoaderVersionProbe::onVersionsError);
        connect(&m_timeout, &QTimer::timeout, this, &LoaderVersionProbe::onTimeout);
        m_timeout.setSingleShot(true);
    }

    void start()
    {
        if (!m_temporaryDirectory.isValid()) {
            fail(QStringLiteral("Cannot create temporary directory for LoaderVersionProbe."));
            return;
        }
        QDir().mkpath(m_loaderDataDirectory);
        requestNext();
    }

    int exitCode() const { return m_exitCode; }

private slots:
    void onVersionsReady(atlas::LoaderKind kind, const QString &minecraftVersion,
                         const QVector<atlas::LoaderVersionDescriptor> &versions)
    {
        if (m_index < 0 || m_index >= m_cases.size()) return;
        const ProbeCase &current = m_cases.at(m_index);
        if (kind != current.kind || minecraftVersion != current.minecraftVersion) return;
        m_timeout.stop();
        if (versions.isEmpty()) {
            fail(QStringLiteral("%1 returned an empty version list.").arg(current.label));
            return;
        }
        QTextStream(stdout) << "OK " << current.label << ": " << versions.first().version
                            << " (" << versions.size() << " versions)\n";
        ++m_index;
        requestNext();
    }

    void onVersionsError(atlas::LoaderKind kind, const QString &minecraftVersion, const QString &message)
    {
        if (m_index < 0 || m_index >= m_cases.size()) return;
        const ProbeCase &current = m_cases.at(m_index);
        if (kind == current.kind && minecraftVersion == current.minecraftVersion) {
            fail(QStringLiteral("%1 failed: %2").arg(current.label, message));
        }
    }

    void onTimeout()
    {
        if (m_index < 0 || m_index >= m_cases.size()) return;
        fail(QStringLiteral("Timed out while requesting %1.").arg(m_cases.at(m_index).label));
    }

private:
    void requestNext()
    {
        if (m_index >= m_cases.size()) {
            QTextStream(stdout) << "LoaderVersionProbe completed successfully.\n";
            m_exitCode = 0;
            QCoreApplication::quit();
            return;
        }
        const ProbeCase &current = m_cases.at(m_index);
        QTextStream(stdout) << "Requesting " << current.label << "...\n";
        m_timeout.start(45000);
        m_service.refreshVersions(current.kind, current.minecraftVersion);
    }

    void fail(const QString &message)
    {
        if (m_exitCode != 0) return;
        m_timeout.stop();
        m_exitCode = 1;
        QTextStream(stderr) << "LoaderVersionProbe failed: " << message << "\n";
        QCoreApplication::quit();
    }

    QTemporaryDir m_temporaryDirectory;
    QString m_loaderDataDirectory;
    atlas::DownloadManager m_downloadManager;
    atlas::LoaderInstallService m_service;
    QVector<ProbeCase> m_cases;
    QTimer m_timeout;
    int m_index = 0;
    int m_exitCode = 1;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    LoaderVersionProbe probe;
    QTimer::singleShot(0, &probe, &LoaderVersionProbe::start);
    application.exec();
    return probe.exitCode();
}

#include "loader_version_probe.moc"
