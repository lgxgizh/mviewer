#pragma once

#include <QIcon>
#include <QPixmap>
#include <QStringList>

namespace mviewer::ui
{

// Outline toolbar icons. Source SVGs live in resources/icons; the runtime
// loads the pre-rendered 1x/2x masks and tints them from the active tokens.
QIcon toolbarIcon(const char *id);
QPixmap toolbarIconPixmap(const char *id);
QStringList toolbarIconIds();

} // namespace mviewer::ui
