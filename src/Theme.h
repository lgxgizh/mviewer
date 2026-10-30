#pragma once

#include "ThemeTokens.h"

#include <QString>

namespace mviewer::ui
{

enum class ThemeMode
{
    Dark,
    System, // follows the Windows light/dark app setting, live
    Light
};

enum class ThemeRole
{
    Bg0,
    Bg1,
    Bg2,
    Bg3,
    Border,
    TextPrimary,
    TextSecondary,
    TextDisabled,
    Accent,
    AccentHover,
    AccentPressed,
    Danger,
    Sidebar,
    Separator,
    Hover
};

class Theme
{
  public:
    static void initTheme();
    // First launch (no saved choice) follows the system; an existing choice,
    // including a saved "dark", is kept. Call before initTheme in main().
    static void seedDefaultThemeChoice();
    // Persisted settings value for a mode ("dark" / "system" / "light").
    static QString themeModeKey(ThemeMode mode);
    static ThemeMode themeModeFromKey(const QString &key);
    static void applyTheme(ThemeMode mode);
    static ThemeMode currentTheme();
    static QString themeModeName(ThemeMode mode);

    // System follows the desktop color scheme. Dark Windows resolves to the
    // dark tokens; anything else uses the light token swap.
    static bool systemPrefersDark();
    static ThemeTokens tokensFor(ThemeMode mode, bool systemPrefersDark);
    static QString buildStyleSheet(const ThemeTokens &tokens);

    // Active token colors. Valid after static init (defaults to dark) and
    // after applyTheme.
    static QString themeColor(ThemeRole role);
    static QString themeRgba(ThemeRole role, int alpha);
};

} // namespace mviewer::ui
