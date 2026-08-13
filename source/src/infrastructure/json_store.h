#pragma once

#include <QJsonObject>
#include <QString>

namespace atlas {

class JsonStore
{
public:
    static bool readObject(const QString &filePath, QJsonObject *object, QString *error = nullptr);
    static bool writeObject(const QString &filePath, const QJsonObject &object, QString *error = nullptr);
};

} // namespace atlas
