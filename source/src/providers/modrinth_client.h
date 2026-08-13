#pragma once

#include "domain/types.h"

#include <QObject>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace atlas {

enum class ContentType {
    All,
    Mod,
    Modpack,
    ResourcePack,
    Shader,
    World
};

struct ModrinthProject
{
    QString id;
    QString slug;
    QString title;
    QString description;
    QString author;
    QString type;
    QStringList categories;
    QStringList gameVersions;
    qint64 downloads = 0;
    QString iconUrl;
    QString latestVersionId;
};

struct ModrinthFile
{
    QString projectId;
    QString versionId;
    QString fileName;
    QString downloadUrl;
    QString sha1;
    QString sha512;
    qint64 size = -1;
    bool primary = false;
};

struct ModrinthSearchFilter
{
    QString query;
    QString minecraftVersion;
    LoaderKind loader = LoaderKind::Unknown;
    ContentType type = ContentType::All;
    QString category;
    QString sortIndex = QStringLiteral("downloads");
    int limit = 20;
    int offset = 0;
};

struct ModrinthCategory
{
    QString name;
    QString projectType;
    QString header;

    bool isValid() const { return !name.trimmed().isEmpty(); }
};

class ModrinthClient final : public QObject
{
    Q_OBJECT

public:
    explicit ModrinthClient(const QString &userAgent, QObject *parent = nullptr);

    void setUserAgent(const QString &userAgent);
    void search(const ModrinthSearchFilter &filter);
    void resolveVersionFiles(const QString &projectId, const QString &versionId);
    void fetchCategories();
    void cancel();

signals:
    void searchFinished(const QVector<atlas::ModrinthProject> &projects, int totalHits);
    void versionFilesResolved(const QString &projectId, const QString &versionId,
                              const QVector<atlas::ModrinthFile> &files);
    void categoriesReceived(const QVector<atlas::ModrinthCategory> &categories);
    void requestFailed(const QString &message);
    void rateLimited(int retryAfterSeconds);

private:
    QVector<ModrinthProject> parseSearchResponse(const QByteArray &body, QString *error) const;
    QVector<ModrinthCategory> parseCategoriesResponse(const QByteArray &body, QString *error) const;
    QString facetsFor(const ModrinthSearchFilter &filter) const;
    void handleSearchReply(QNetworkReply *reply);
    void handleVersionFilesReply(QNetworkReply *reply, const QString &projectId, const QString &versionId);
    void handleCategoriesReply(QNetworkReply *reply);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QNetworkReply *m_categoriesReply = nullptr;
    QString m_userAgent;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::ModrinthProject)
Q_DECLARE_METATYPE(atlas::ModrinthFile)
Q_DECLARE_METATYPE(QVector<atlas::ModrinthFile>)
