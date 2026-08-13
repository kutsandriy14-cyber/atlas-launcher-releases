#include "providers/modrinth_client.h"

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

QString contentTypeToApiValue(ContentType type)
{
    switch (type) {
    case ContentType::Mod: return QStringLiteral("mod");
    case ContentType::Modpack: return QStringLiteral("modpack");
    case ContentType::ResourcePack: return QStringLiteral("resourcepack");
    case ContentType::Shader: return QStringLiteral("shader");
    case ContentType::All:
    case ContentType::World:
        return QString();
    }
    return QString();
}

QStringList stringArray(const QJsonValue &value)
{
    QStringList values;
    for (const QJsonValue &entry : value.toArray()) values.append(entry.toString());
    return values;
}

} // namespace

ModrinthClient::ModrinthClient(const QString &userAgent, QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_userAgent(userAgent.trimmed())
{
    if (m_userAgent.isEmpty()) {
        m_userAgent = QStringLiteral("AtlasLauncher/0.1.0 (personal launcher)");
    }
    qRegisterMetaType<ModrinthFile>();
    qRegisterMetaType<QVector<ModrinthFile>>();
}

void ModrinthClient::setUserAgent(const QString &userAgent)
{
    const QString normalized = userAgent.trimmed();
    if (!normalized.isEmpty()) m_userAgent = normalized;
}

QString ModrinthClient::facetsFor(const ModrinthSearchFilter &filter) const
{
    QJsonArray groups;
    if (!filter.minecraftVersion.trimmed().isEmpty()) {
        groups.append(QJsonArray{QStringLiteral("versions:%1").arg(filter.minecraftVersion.trimmed())});
    }
    const QString loader = filter.loader == LoaderKind::LegacyFabric
        ? QStringLiteral("fabric") : loaderKindToString(filter.loader);
    if (filter.loader != LoaderKind::Vanilla && filter.loader != LoaderKind::Unknown) {
        groups.append(QJsonArray{QStringLiteral("categories:%1").arg(loader)});
    }
    const QString type = contentTypeToApiValue(filter.type);
    if (!type.isEmpty()) {
        groups.append(QJsonArray{QStringLiteral("project_type:%1").arg(type)});
    }
    if (!filter.category.trimmed().isEmpty()) {
        groups.append(QJsonArray{QStringLiteral("categories:%1").arg(filter.category.trimmed())});
    }
    return QString::fromUtf8(QJsonDocument(groups).toJson(QJsonDocument::Compact));
}

void ModrinthClient::search(const ModrinthSearchFilter &filter)
{
    cancel();
    QUrl url(QStringLiteral("https://api.modrinth.com/v2/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("query"), filter.query.trimmed());
    static const QStringList supportedIndexes = {
        QStringLiteral("relevance"), QStringLiteral("downloads"), QStringLiteral("follows"),
        QStringLiteral("newest"), QStringLiteral("updated")
    };
    const QString sortIndex = supportedIndexes.contains(filter.sortIndex)
        ? filter.sortIndex : QStringLiteral("downloads");
    query.addQueryItem(QStringLiteral("index"), sortIndex);
    query.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, filter.limit, 100)));
    query.addQueryItem(QStringLiteral("offset"), QString::number(qMax(0, filter.offset)));
    const QString facets = facetsFor(filter);
    if (!facets.isEmpty()) query.addQueryItem(QStringLiteral("facets"), facets);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    m_activeReply = m_network->get(request);
    connect(m_activeReply, &QNetworkReply::finished, this, [this, reply = m_activeReply]() {
        handleSearchReply(reply);
    });
}

void ModrinthClient::fetchCategories()
{
    if (m_categoriesReply) {
        m_categoriesReply->abort();
        m_categoriesReply->deleteLater();
        m_categoriesReply = nullptr;
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.modrinth.com/v2/tag/category")));
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    m_categoriesReply = m_network->get(request);
    connect(m_categoriesReply, &QNetworkReply::finished, this, [this, reply = m_categoriesReply]() {
        handleCategoriesReply(reply);
    });
}

void ModrinthClient::resolveVersionFiles(const QString &projectId, const QString &versionId)
{
    cancel();
    if (projectId.trimmed().isEmpty() || versionId.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("Для установки Modrinth отсутствует идентификатор проекта или версии."));
        return;
    }
    const QString encodedVersion = QString::fromLatin1(QUrl::toPercentEncoding(versionId.trimmed()));
    QNetworkRequest request(QUrl(QStringLiteral("https://api.modrinth.com/v2/version/%1").arg(encodedVersion)));
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    m_activeReply = m_network->get(request);
    connect(m_activeReply, &QNetworkReply::finished, this,
            [this, reply = m_activeReply, projectId, versionId]() {
        handleVersionFilesReply(reply, projectId, versionId);
    });
}

void ModrinthClient::cancel()
{
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

void ModrinthClient::handleSearchReply(QNetworkReply *reply)
{
    if (!reply) return;
    if (reply == m_activeReply) m_activeReply = nullptr;

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QByteArray resetHeader = reply->rawHeader("X-Ratelimit-Reset");

    if (statusCode == 429) {
        bool okay = false;
        const int seconds = resetHeader.toInt(&okay);
        emit rateLimited(okay ? seconds : 60);
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit requestFailed(QStringLiteral("Ошибка сети Modrinth: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        emit requestFailed(QStringLiteral("Modrinth вернул HTTP %1").arg(statusCode));
        reply->deleteLater();
        return;
    }

    QString parseError;
    const QVector<ModrinthProject> projects = parseSearchResponse(body, &parseError);
    if (!parseError.isEmpty()) {
        emit requestFailed(parseError);
        reply->deleteLater();
        return;
    }

    QJsonParseError documentError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &documentError);
    const int totalHits = document.object().value(QStringLiteral("total_hits")).toInt(projects.size());
    emit searchFinished(projects, totalHits);
    reply->deleteLater();
}

void ModrinthClient::handleCategoriesReply(QNetworkReply *reply)
{
    if (!reply) return;
    if (reply == m_categoriesReply) m_categoriesReply = nullptr;
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        emit requestFailed(QStringLiteral("Не удалось получить категории Modrinth: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        emit requestFailed(QStringLiteral("Modrinth не вернул категории (HTTP %1).").arg(statusCode));
        reply->deleteLater();
        return;
    }
    QString error;
    const QVector<ModrinthCategory> categories = parseCategoriesResponse(body, &error);
    if (!error.isEmpty()) emit requestFailed(error);
    else emit categoriesReceived(categories);
    reply->deleteLater();
}

void ModrinthClient::handleVersionFilesReply(QNetworkReply *reply, const QString &projectId, const QString &versionId)
{
    if (!reply) return;
    if (reply == m_activeReply) m_activeReply = nullptr;

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QByteArray resetHeader = reply->rawHeader("X-Ratelimit-Reset");
    if (statusCode == 429) {
        bool okay = false;
        const int seconds = resetHeader.toInt(&okay);
        emit rateLimited(okay ? seconds : 60);
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit requestFailed(QStringLiteral("Ошибка сети Modrinth при получении файла: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        emit requestFailed(QStringLiteral("Modrinth не вернул данные версии (HTTP %1).").arg(statusCode));
        reply->deleteLater();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit requestFailed(QStringLiteral("Неверный ответ Modrinth о версии: %1").arg(parseError.errorString()));
        reply->deleteLater();
        return;
    }
    const QJsonObject version = document.object();
    if (version.value(QStringLiteral("project_id")).toString() != projectId ||
        version.value(QStringLiteral("id")).toString() != versionId) {
        emit requestFailed(QStringLiteral("Modrinth вернул версию, не относящуюся к выбранному проекту."));
        reply->deleteLater();
        return;
    }
    QVector<ModrinthFile> files;
    for (const QJsonValue &fileValue : version.value(QStringLiteral("files")).toArray()) {
        const QJsonObject file = fileValue.toObject();
        const QUrl url(file.value(QStringLiteral("url")).toString());
        const QJsonObject hashes = file.value(QStringLiteral("hashes")).toObject();
        const QString sha1 = hashes.value(QStringLiteral("sha1")).toString();
        const QString sha512 = hashes.value(QStringLiteral("sha512")).toString();
        const QString name = file.value(QStringLiteral("filename")).toString();
        if (!url.isValid() || url.scheme() != QStringLiteral("https") || name.isEmpty() || (sha1.isEmpty() && sha512.isEmpty())) continue;
        ModrinthFile resolved;
        resolved.projectId = projectId;
        resolved.versionId = versionId;
        resolved.fileName = name;
        resolved.downloadUrl = url.toString();
        resolved.sha1 = sha1;
        resolved.sha512 = sha512;
        resolved.size = file.value(QStringLiteral("size")).toVariant().toLongLong();
        resolved.primary = file.value(QStringLiteral("primary")).toBool();
        files.append(resolved);
    }
    if (files.isEmpty()) {
        emit requestFailed(QStringLiteral("У выбранной версии Modrinth нет проверяемых HTTPS-файлов для установки."));
        reply->deleteLater();
        return;
    }
    emit versionFilesResolved(projectId, versionId, files);
    reply->deleteLater();
}

QVector<ModrinthCategory> ModrinthClient::parseCategoriesResponse(const QByteArray &body, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("Неверный ответ категорий Modrinth: %1").arg(parseError.errorString());
        return {};
    }
    QVector<ModrinthCategory> categories;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        ModrinthCategory category;
        category.name = object.value(QStringLiteral("name")).toString().trimmed();
        category.projectType = object.value(QStringLiteral("project_type")).toString().trimmed();
        category.header = object.value(QStringLiteral("header")).toString().trimmed();
        if (category.isValid()) categories.append(category);
    }
    return categories;
}

QVector<ModrinthProject> ModrinthClient::parseSearchResponse(const QByteArray &body, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Неверный ответ Modrinth: %1").arg(parseError.errorString());
        return {};
    }

    QVector<ModrinthProject> projects;
    const QJsonArray hits = document.object().value(QStringLiteral("hits")).toArray();
    projects.reserve(hits.size());
    for (const QJsonValue &hitValue : hits) {
        const QJsonObject hit = hitValue.toObject();
        ModrinthProject project;
        project.id = hit.value(QStringLiteral("project_id")).toString();
        project.slug = hit.value(QStringLiteral("slug")).toString();
        project.title = hit.value(QStringLiteral("title")).toString();
        project.description = hit.value(QStringLiteral("description")).toString();
        project.author = hit.value(QStringLiteral("author")).toString();
        project.type = hit.value(QStringLiteral("project_type")).toString();
        project.categories = stringArray(hit.value(QStringLiteral("display_categories")));
        project.gameVersions = stringArray(hit.value(QStringLiteral("versions")));
        project.downloads = hit.value(QStringLiteral("downloads")).toVariant().toLongLong();
        project.iconUrl = hit.value(QStringLiteral("icon_url")).toString();
        project.latestVersionId = hit.value(QStringLiteral("latest_version")).toString();
        if (project.id.isEmpty() || project.title.isEmpty()) continue;
        projects.append(project);
    }
    return projects;
}

} // namespace atlas
