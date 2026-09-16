#pragma once

#include <QString>

namespace mviewer::ui
{

enum class ThemeMode
{
    Dark,
    System
};

class Theme
{
public:
    static void initTheme();
    static void applyTheme(ThemeMode mode);
    static ThemeMode currentTheme();
    static QString themeModeName(ThemeMode mode);
};

} // namespace mviewer::ui
