#pragma once

#include "domain/types.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace atlas {

class InstanceService final : public QObject
{
    Q_OBJECT

public:
    explicit InstanceService(const QString &dataDirectory, QObject *parent = nullptr);

    QString dataDirectory() const;
    QString instancesDirectory() const;
    bool setInstancesDirectory(const QString &path, QString *error = nullptr);
    QVector<Instance> loadAll(QString *error = nullptr) const;
    bool save(const Instance &instance, QString *error = nullptr) const;
    bool remove(const QString &id, QString *error = nullptr) const;
    Instance create(const QString &name, const QString &minecraftVersion,
                    LoaderKind loader, const QString &loaderVersion) const;
    QString instanceDirectory(const QString &id) const;

private:
    QString filePath(const QString &id) const;
    bool isSafeId(const QString &id) const;

    QString m_dataDirectory;
    QString m_instancesDirectory;
};

} // namespace atlas
