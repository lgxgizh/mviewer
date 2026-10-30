#pragma once

#include <QIcon>
#include <QPixmap>
#include <QStringList>

namespace mviewer::ui
{

// Logical size of a toolbar icon. Masks are pre-rendered at 1x, 1.5x, 2x and
// 3x of this size (resources/icons/<id>.png and the @1.5x / @2x / @3x siblings).
inline constexpr int kToolbarIconPx = 16;

// Outline toolbar icons. Source SVGs live in resources/icons; the runtime
// loads the pre-rendered masks and tints them from the active tokens.
QIcon toolbarIcon(const char *id);
QPixmap toolbarIconPixmap(const char *id);
QStringList toolbarIconIds();

} // namespace mviewer::ui
