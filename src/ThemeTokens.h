#pragma once

#include <QString>

namespace mviewer::ui
{

// Flat theme tokens. One accent, a 4 px spacing grid, and two corner radii.
// Image canvases do not use these backgrounds; they keep a separate near-black.
struct ThemeTokens
{
    QString bg0;
    QString bg1;
    QString bg2;
    QString bg3;
    QString border;
    QString textPrimary;
    QString textSecondary;
    QString textDisabled;
    QString accent;
    QString accentHover;
    QString accentPressed;
    QString accentTint;
    QString selection;
    QString danger;
    QString onAccent;
    QString fontFamily;
    int space4 = 4;
    int space8 = 8;
    int space12 = 12;
    int space16 = 16;
    int radiusControl = 6;
    int radiusPanel = 8;
    int fontPt = 9;
};

inline ThemeTokens makeThemeTokens(bool dark)
{
    ThemeTokens tokens;
    tokens.bg0 = QString::fromLatin1(dark ? "#1e1e1e" : "#f5f5f5");
    tokens.bg1 = QString::fromLatin1(dark ? "#252526" : "#ffffff");
    tokens.bg2 = QString::fromLatin1(dark ? "#2d2d30" : "#ececef");
    tokens.bg3 = QString::fromLatin1(dark ? "#3a3a3d" : "#e1e1e5");
    tokens.border = QString::fromLatin1(dark ? "#3f3f46" : "#d0d0d6");
    tokens.textPrimary = QString::fromLatin1(dark ? "#e6e6e6" : "#1a1a1a");
    tokens.textSecondary = QString::fromLatin1(dark ? "#a0a0a0" : "#5e5e66");
    tokens.textDisabled = QString::fromLatin1(dark ? "#6b6b6b" : "#9a9aa2");
    tokens.accent = QString::fromLatin1("#4c8dff");
    tokens.accentHover = QString::fromLatin1(dark ? "#6c9fff" : "#3d7ef0");
    tokens.accentPressed = QString::fromLatin1(dark ? "#3a74db" : "#2f66d6");
    tokens.accentTint = QString::fromLatin1("rgba(76, 141, 255, 71)");
    tokens.selection = QString::fromLatin1("rgba(76, 141, 255, 77)");
    tokens.danger = QString::fromLatin1("#e5534b");
    tokens.onAccent = QString::fromLatin1("#ffffff");
    tokens.fontFamily = QString::fromLatin1(
        "\"Segoe UI Variable\", \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif");
    tokens.fontPt = 9;
    return tokens;
}

} // namespace mviewer::ui
