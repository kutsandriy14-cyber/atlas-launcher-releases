#include "infrastructure/logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace atlas {
namespace {

QMutex logMutex;
QString logPath;

void writeLine(const QString &level, const QString &message)
{
    QMutexLocker locker(&logMutex);
    const QString line = QStringLiteral("[%1] [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), level, message);

    if (!logPath.isEmpty()) {
        QFile file(logPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << line;
        }
    }
    QTextStream console(stderr);
    console << line;
    console.flush();
}

} // namespace

void Logger::install(const QString &dataDirectory)
{
    QDir directory(dataDirectory);
    if (!directory.exists()) directory.mkpath(QStringLiteral("."));
    logPath = directory.filePath(QStringLiteral("atlas.log"));
    writeLine(QStringLiteral("INFO"), QStringLiteral("Atlas Launcher logging started"));
}

void Logger::info(const QString &message)
{
    writeLine(QStringLiteral("INFO"), message);
}

void Logger::warning(const QString &message)
{
    writeLine(QStringLiteral("WARN"), message);
}

void Logger::error(const QString &message)
{
    writeLine(QStringLiteral("ERROR"), message);
}

} // namespace atlas
