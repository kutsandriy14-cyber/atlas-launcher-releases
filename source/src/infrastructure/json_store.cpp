#include "infrastructure/json_store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace atlas {

bool JsonStore::readObject(const QString &filePath, QJsonObject *object, QString *error)
{
    if (!object) {
        if (error) *error = QStringLiteral("Output object is null");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("JSON root must be an object")
                : parseError.errorString();
        }
        return false;
    }

    *object = document.object();
    return true;
}

bool JsonStore::writeObject(const QString &filePath, const QJsonObject &object, QString *error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    const QJsonDocument document(object);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace atlas
