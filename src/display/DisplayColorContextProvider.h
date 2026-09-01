#pragma once

#include "core/image/DisplayColorContext.h"

#include <QWindow>

// UI-layer bridge from a QWindow to the active per-display ICC profile.  The
// Qt-free DisplayColorContext value crosses into core; Win32/Qt handles stay
// in this header/implementation boundary.
class DisplayColorContextProvider
{
  public:
    static mviewer::core::DisplayColorContext forWindow(const QWindow *window);
};
