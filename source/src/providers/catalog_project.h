#pragma once

#include <QString>
#include <QStringList>

namespace atlas {

struct CatalogProject
{
    QString provider;
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

} // namespace atlas

Q_DECLARE_METATYPE(atlas::CatalogProject)
