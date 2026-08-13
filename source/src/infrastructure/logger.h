#pragma once

#include <QString>

namespace atlas {

class Logger
{
public:
    static void install(const QString &dataDirectory);
    static void info(const QString &message);
    static void warning(const QString &message);
    static void error(const QString &message);
};

} // namespace atlas
