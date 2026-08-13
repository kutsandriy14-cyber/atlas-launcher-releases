#include "providers/curseforge_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace atlas {
namespace {
constexpr int MinecraftGameId = 432;

QStringList categoryNames(const QJsonArray &categories)
{
    QStringList names;
    for (const QJsonValue &value : categories) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) names.append(name);
    }
    return names;
}

QString firstAuthor(const QJsonArray &authors)
{
    return authors.isEmpty() ? QString() : authors.first().toObject().value(QStringLiteral("name")).toString();
}

int curseForgeLoaderId(LoaderKind loader)
{
    switch (loader) {
    case LoaderKind::Forge: return 1;
    case LoaderKind::Fabric:
    case LoaderKind::LegacyFabric: return 4;
    case LoaderKind::Quilt: return 5;
    case LoaderKind::NeoForge: return 6;
    case LoaderKind::Vanilla:
    case LoaderKind::Unknown: return 0;
    }
    return 0;
}

QString catalogTypeForClassId(int classId)
{
    switch (classId) {
    case 6: return QStringLiteral("mod");
    case 4471: return QStringLiteral("modpack");
    case 12: return QStringLiteral("resourcepack");
    case 6552: return QStringLiteral("shader");
    case 17: return QStringLiteral("world");
    default: return QStringLiteral("project");
    }
}

QString compatibleFileId(const QJsonArray &indexes, const CurseForgeSearchFilter &filter,
                         QStringList *versions)
{
    const int expectedLoader = curseForgeLoaderId(filter.loader);
    QString versionMatchWithoutLoader;
    for (const QJsonValue &value : indexes) {
        const QJsonObject index = value.toObject();
        const QString version = index.value(QStringLiteral("gameVersion")).toString();
        if (!version.isEmpty() && versions && !versions->contains(version)) versions->append(version);
        const QString fileId = QString::number(index.value(QStringLiteral("fileId")).toVariant().toLongLong());
        if (fileId.isEmpty() || fileId == QStringLiteral("0")) continue;
        const bool versionMatches = filter.minecraftVersion.isEmpty() || version == filter.minecraftVersion;
        if (!versionMatches) continue;
        const int reportedLoader = index.value(QStringLiteral("modLoader")).toInt();
        if (expectedLoader > 0 && reportedLoader == expectedLoader) return fileId;
        if (versionMatchWithoutLoader.isEmpty()) versionMatchWithoutLoader = fileId;
    }
    // Ресурспаки, миры и шейдеры не обязаны содержать идентификатор загрузчика.
    return filter.requireLoaderMatch && expectedLoader > 0 ? QString() : versionMatchWithoutLoader;
}

} // namespace

bool CurseForgeFile::isValid() const
{
    const QUrl url(downloadUrl);
    return !projectId.isEmpty() && !fileId.isEmpty() && !fileName.isEmpty()
        && url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && sha1.size() == 40 && size >= 0;
}

CurseForgeClient::CurseForgeClient(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

void CurseForgeClient::setApiKey(const QString &apiKey)
{
    // The key remains in volatile process memory and is never logged or serialized.
    m_apiKey = apiKey.trimmed();
}

bool CurseForgeClient::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

void CurseForgeClient::searchMinecraft(const CurseForgeSearchFilter &filter)
{
    cancel();
    m_activeSearchFilter = filter;
    if (!hasApiKey()) {
        emit requestFailed(QStringLiteral("Для CurseForge требуется ваш персональный API-ключ. Он не хранится в JSON-файле и не включается в программу."));
        return;
    }

    QUrl url(QStringLiteral("https://api.curseforge.com/v1/mods/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("gameId"), QString::number(MinecraftGameId));
    query.addQueryItem(QStringLiteral("searchFilter"), filter.query.trimmed());
    if (!filter.minecraftVersion.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("gameVersion"), filter.minecraftVersion.trimmed());
    }
    if (filter.classId > 0) query.addQueryItem(QStringLiteral("classId"), QString::number(filter.classId));
    if (filter.categoryId > 0) query.addQueryItem(QStringLiteral("categoryId"), QString::number(filter.categoryId));
    const int loaderId = curseForgeLoaderId(filter.loader);
    if (filter.requireLoaderMatch && loaderId > 0 && !filter.minecraftVersion.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(loaderId));
    }
    query.addQueryItem(QStringLiteral("sortField"), QString::number(qBound(1, filter.sortField, 12)));
    query.addQueryItem(QStringLiteral("sortOrder"), filter.sortAscending ? QStringLiteral("asc") : QStringLiteral("desc"));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(qBound(1, filter.pageSize, 50)));
    query.addQueryItem(QStringLiteral("index"), QString::number(qMax(0, filter.index)));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    m_activeReply = m_network->get(request);
    connect(m_activeReply, &QNetworkReply::finished, this, [this, reply = m_activeReply]() {
        handleReply(reply, RequestPurpose::Search);
    });
}

void CurseForgeClient::fetchMinecraftCategories(int classId)
{
    if (!hasApiKey() || classId <= 0) {
        emit categoriesReceived({}, classId);
        return;
    }
    if (m_categoriesReply) {
        m_categoriesReply->abort();
        m_categoriesReply->deleteLater();
        m_categoriesReply = nullptr;
    }
    QUrl url(QStringLiteral("https://api.curseforge.com/v1/categories"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("gameId"), QString::number(MinecraftGameId));
    query.addQueryItem(QStringLiteral("classId"), QString::number(classId));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    m_categoriesReply = m_network->get(request);
    connect(m_categoriesReply, &QNetworkReply::finished, this, [this, reply = m_categoriesReply, classId]() {
        handleReply(reply, RequestPurpose::Categories, {}, {}, classId);
    });
}

void CurseForgeClient::resolveFile(const QString &projectId, const QString &fileId)
{
    cancel();
    if (!hasApiKey()) {
        emit requestFailed(QStringLiteral("Для CurseForge требуется персональный API-ключ только на время текущего сеанса."));
        return;
    }
    bool projectOk = false;
    bool fileOk = false;
    projectId.toULongLong(&projectOk);
    fileId.toULongLong(&fileOk);
    if (!projectOk || !fileOk) {
        emit requestFailed(QStringLiteral("CurseForge вернул недопустимый идентификатор проекта или файла."));
        return;
    }
    const QUrl url(QStringLiteral("https://api.curseforge.com/v1/mods/%1/files/%2").arg(projectId, fileId));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    m_activeReply = m_network->get(request);
    connect(m_activeReply, &QNetworkReply::finished, this, [this, reply = m_activeReply, projectId, fileId]() {
        handleReply(reply, RequestPurpose::File, projectId, fileId);
    });
}

void CurseForgeClient::cancel()
{
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

void CurseForgeClient::handleReply(QNetworkReply *reply, RequestPurpose purpose, const QString &projectId,
                                   const QString &fileId, int classId)
{
    if (!reply) return;
    if (reply == m_activeReply) m_activeReply = nullptr;
    if (reply == m_categoriesReply) m_categoriesReply = nullptr;

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (statusCode == 401 || statusCode == 403) {
        emit requestFailed(QStringLiteral("CurseForge отклонил API-ключ (HTTP %1). Скопируйте полный ключ из CurseForge Console и убедитесь, что у организации есть доступ к публичному каталогу Minecraft. Ключ остаётся только в памяти Atlas.").arg(statusCode));
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit requestFailed(QStringLiteral("Ошибка сети CurseForge: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        emit requestFailed(QStringLiteral("CurseForge вернул HTTP %1. Проверьте персональный API-ключ и условия доступа.").arg(statusCode));
        reply->deleteLater();
        return;
    }

    QString error;
    if (purpose == RequestPurpose::File) {
        const CurseForgeFile file = parseFileResponse(body, projectId, fileId, &error);
        if (!error.isEmpty()) emit requestFailed(error);
        else emit fileResolved(file);
        reply->deleteLater();
        return;
    }
    if (purpose == RequestPurpose::Categories) {
        const QVector<CurseForgeCategory> categories = parseCategoriesResponse(body, &error);
        if (!error.isEmpty()) emit requestFailed(error);
        else emit categoriesReceived(categories, classId);
        reply->deleteLater();
        return;
    }
    const QVector<CatalogProject> projects = parseSearchResponse(body, m_activeSearchFilter, &error);
    if (!error.isEmpty()) {
        emit requestFailed(error);
        reply->deleteLater();
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const int totalHits = document.object().value(QStringLiteral("pagination")).toObject()
        .value(QStringLiteral("totalCount")).toInt(projects.size());
    emit searchFinished(projects, totalHits);
    reply->deleteLater();
}

QVector<CurseForgeCategory> CurseForgeClient::parseCategoriesResponse(const QByteArray &body, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Неверный ответ категорий CurseForge: %1").arg(parseError.errorString());
        return {};
    }
    QVector<CurseForgeCategory> categories;
    for (const QJsonValue &value : document.object().value(QStringLiteral("data")).toArray()) {
        const QJsonObject object = value.toObject();
        CurseForgeCategory category;
        category.id = object.value(QStringLiteral("id")).toInt();
        category.classId = object.value(QStringLiteral("classId")).toInt();
        category.parentCategoryId = object.value(QStringLiteral("parentCategoryId")).toInt();
        category.name = object.value(QStringLiteral("name")).toString().trimmed();
        if (category.isValid()) categories.append(category);
    }
    return categories;
}

CurseForgeFile CurseForgeClient::parseFileResponse(const QByteArray &body, const QString &projectId, const QString &fileId, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Неверный ответ файла CurseForge: %1").arg(parseError.errorString());
        return {};
    }
    const QJsonObject data = document.object().value(QStringLiteral("data")).toObject();
    CurseForgeFile file;
    file.projectId = projectId;
    file.fileId = QString::number(data.value(QStringLiteral("id")).toVariant().toLongLong());
    file.fileName = data.value(QStringLiteral("fileName")).toString();
    file.downloadUrl = data.value(QStringLiteral("downloadUrl")).toString();
    file.size = data.value(QStringLiteral("fileLength")).toVariant().toLongLong();
    for (const QJsonValue &value : data.value(QStringLiteral("hashes")).toArray()) {
        const QJsonObject hash = value.toObject();
        if (hash.value(QStringLiteral("algo")).toInt() == 1) {
            file.sha1 = hash.value(QStringLiteral("value")).toString().toLower();
            break;
        }
    }
    if (file.fileId != fileId || !file.isValid()) {
        if (error) *error = QStringLiteral("CurseForge не вернул пригодный для проверяемой установки HTTPS-файл с SHA-1. Выберите другую версию проекта.");
        return {};
    }
    return file;
}

QVector<CatalogProject> CurseForgeClient::parseSearchResponse(const QByteArray &body,
                                                               const CurseForgeSearchFilter &filter,
                                                               QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Неверный ответ CurseForge: %1").arg(parseError.errorString());
        return {};
    }

    QVector<CatalogProject> projects;
    for (const QJsonValue &value : document.object().value(QStringLiteral("data")).toArray()) {
        const QJsonObject object = value.toObject();
        CatalogProject project;
        project.provider = QStringLiteral("CurseForge");
        project.id = QString::number(object.value(QStringLiteral("id")).toVariant().toLongLong());
        project.slug = object.value(QStringLiteral("slug")).toString();
        project.title = object.value(QStringLiteral("name")).toString();
        project.description = object.value(QStringLiteral("summary")).toString();
        project.author = firstAuthor(object.value(QStringLiteral("authors")).toArray());
        // classId приходит от официального API и точнее пользовательского фильтра,
        // поэтому именно он определяет безопасную папку назначения при установке.
        const int classId = object.value(QStringLiteral("classId")).toInt();
        project.type = catalogTypeForClassId(classId > 0 ? classId : filter.classId);
        project.categories = categoryNames(object.value(QStringLiteral("categories")).toArray());
        project.downloads = object.value(QStringLiteral("downloadCount")).toVariant().toLongLong();
        project.iconUrl = object.value(QStringLiteral("logo")).toObject().value(QStringLiteral("thumbnailUrl")).toString();
        project.latestVersionId = compatibleFileId(object.value(QStringLiteral("latestFilesIndexes")).toArray(),
                                                    filter, &project.gameVersions);
        // mainFileId может относиться к другой версии Minecraft или загрузчику.
        // Не показываем проект как устанавливаемый, пока официальный API не
        // вернёт подходящий индекс файла.
        if (!project.id.isEmpty() && !project.title.isEmpty() && !project.latestVersionId.isEmpty()) projects.append(project);
    }
    return projects;
}

} // namespace atlas
