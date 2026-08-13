#pragma once

#include "domain/types.h"
#include "providers/catalog_project.h"

#include <QObject>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace atlas {

struct CurseForgeFile
{
    QString projectId;
    QString fileId;
    QString fileName;
    QString downloadUrl;
    QString sha1;
    qint64 size = -1;
    bool isValid() const;
};

struct CurseForgeSearchFilter
{
    QString query;
    QString minecraftVersion;
    LoaderKind loader = LoaderKind::Unknown;
    // Для модов требуется точное совпадение загрузчика. Ресурспаки, миры и
    // шейдеры не привязаны к нему, поэтому для них используется false.
    bool requireLoaderMatch = false;
    int classId = 0;
    int categoryId = 0;
    int sortField = 2;
    bool sortAscending = false;
    int pageSize = 20;
    int index = 0;
};

struct CurseForgeCategory
{
    int id = 0;
    int classId = 0;
    int parentCategoryId = 0;
    QString name;

    bool isValid() const { return id > 0 && !name.trimmed().isEmpty(); }
};

class CurseForgeClient final : public QObject
{
    Q_OBJECT

public:
    explicit CurseForgeClient(QObject *parent = nullptr);

    void setApiKey(const QString &apiKey);
    bool hasApiKey() const;
    void searchMinecraft(const CurseForgeSearchFilter &filter);
    void resolveFile(const QString &projectId, const QString &fileId);
    void fetchMinecraftCategories(int classId);
    void cancel();

signals:
    void searchFinished(const QVector<atlas::CatalogProject> &projects, int totalHits);
    void fileResolved(const atlas::CurseForgeFile &file);
    void categoriesReceived(const QVector<atlas::CurseForgeCategory> &categories, int classId);
    void requestFailed(const QString &message);

private:
    enum class RequestPurpose { Search, File, Categories };
    void handleReply(QNetworkReply *reply, RequestPurpose purpose, const QString &projectId = QString(),
                     const QString &fileId = QString(), int classId = 0);
    QVector<CatalogProject> parseSearchResponse(const QByteArray &body,
                                                const CurseForgeSearchFilter &filter,
                                                QString *error) const;
    QVector<CurseForgeCategory> parseCategoriesResponse(const QByteArray &body, QString *error) const;
    CurseForgeFile parseFileResponse(const QByteArray &body, const QString &projectId, const QString &fileId, QString *error) const;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QNetworkReply *m_categoriesReply = nullptr;
    QString m_apiKey;
    CurseForgeSearchFilter m_activeSearchFilter;
};

} // namespace atlas
