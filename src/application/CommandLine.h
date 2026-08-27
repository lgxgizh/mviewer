#pragma once

#include <QString>

namespace mviewer::application
{

// Return whether an argument can name a file or directory to open. Unix
// absolute paths begin with '/', while Windows also uses '/' for switches;
// keep that distinction at the command-line boundary instead of treating all
// slash-prefixed arguments as options on every platform.
inline bool isPositionalOpenArgument(const QString &argument)
{
    if (argument.startsWith(QLatin1Char('-')))
        return false;
#ifdef Q_OS_WIN
    if (argument.startsWith(QLatin1Char('/')))
        return false;
#endif
    return true;
}

} // namespace mviewer::application
