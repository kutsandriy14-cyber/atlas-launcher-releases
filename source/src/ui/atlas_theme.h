#pragma once

#include <QString>

class QApplication;

namespace atlas {

class AtlasTheme
{
public:
    static void apply(QApplication &application);
    static QString styleSheet();
};

} // namespace atlas
