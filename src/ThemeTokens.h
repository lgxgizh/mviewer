#pragma once

#include <QString>

namespace mviewer::ui
{

// Flat macOS-like tokens. One accent, a 4 px spacing grid, and a short radius
// scale. Image canvases do not use these backgrounds; they keep a separate
// near-black (QPalette::Dark).
//
// Retint swaps token strings in live widget stylesheets. Every color value is
// unique within a theme and different across dark and light, except onAccent,
// which stays literal #ffffff so overlay text is not rewritten. rgba values
// are full strings (exact match), never a bare white or black hex.
struct ThemeTokens
{
    QString bg0;     // content
    QString bg1;     // window chrome (toolbar, menu, dialog)
    QString bg2;     // controls
    QString bg3;     // raised / pressed control
    QString sidebar; // folder tree and favorites
    QString border;  // visible edge (inputs)
    QString separator;
    QString hover;
    QString bevel; // Fusion light bevel, palette only
    QString textPrimary;
    QString textSecondary;
    QString textDisabled; // tertiary / placeholder
    QString accent;
    QString accentHover;
    QString accentPressed;
    QString accentTint;
    QString selection;
    QString danger;
    QString onAccent;
    QString scrollHandle;
    QString scrollHandleHover;
    QString fontFamily;
    int space4 = 4;
    int space8 = 8;
    int space12 = 12;
    int space16 = 16;
    int radiusControl = 7;
    int radiusPanel = 10;
    int radiusChip = 5;
    int radiusSearch = 14;
    int radiusTooltip = 6;
    int fontPt = 10;
    int fontBodyPx = 13;
    int fontCaptionPx = 11;
    int fontTitlePx = 15;
    int fontLargeTitlePx = 20;
};

inline ThemeTokens makeThemeTokens(bool dark)
{
    ThemeTokens tokens;
    // Content and controls stay off pure #ffffff so a live theme switch can
    // tell the token apart from literal white text on image overlays.
    tokens.bg0 = QString::fromLatin1(dark ? "#1c1c1e" : "#fbfbfd");
    tokens.bg1 = QString::fromLatin1(dark ? "#2a2a2c" : "#f5f5f7");
    tokens.bg2 = QString::fromLatin1(dark ? "#3a3a3c" : "#fefefe");
    tokens.bg3 = QString::fromLatin1(dark ? "#48484a" : "#e5e5ea");
    tokens.sidebar = QString::fromLatin1(dark ? "#262628" : "#f2f2f4");
    tokens.border = QString::fromLatin1(dark ? "#545456" : "#d1d1d6");
    tokens.separator = QString::fromLatin1(dark ? "rgba(255, 255, 255, 20)" : "rgba(0, 0, 0, 23)");
    tokens.hover = QString::fromLatin1(dark ? "rgba(255, 255, 255, 15)" : "rgba(0, 0, 0, 15)");
    tokens.bevel = QString::fromLatin1(dark ? "#424244" : "#f7f7f9");
    tokens.textPrimary = QString::fromLatin1(dark ? "#ebebf0" : "#1d1d1f");
    tokens.textSecondary = QString::fromLatin1(dark ? "#98989f" : "#6e6e73");
    tokens.textDisabled = QString::fromLatin1(dark ? "#636366" : "#aeaeb2");
    tokens.accent = QString::fromLatin1(dark ? "#0a84ff" : "#007aff");
    tokens.accentHover = QString::fromLatin1(dark ? "#409cff" : "#0a84ff");
    tokens.accentPressed = QString::fromLatin1(dark ? "#0071e3" : "#0062cc");
    tokens.accentTint =
        QString::fromLatin1(dark ? "rgba(10, 132, 255, 46)" : "rgba(0, 122, 255, 38)");
    tokens.selection =
        QString::fromLatin1(dark ? "rgba(10, 132, 255, 77)" : "rgba(0, 122, 255, 64)");
    tokens.danger = QString::fromLatin1(dark ? "#ff453a" : "#ff3b30");
    tokens.onAccent = QString::fromLatin1("#ffffff");
    tokens.scrollHandle =
        QString::fromLatin1(dark ? "rgba(235, 235, 240, 64)" : "rgba(29, 29, 31, 64)");
    tokens.scrollHandleHover =
        QString::fromLatin1(dark ? "rgba(235, 235, 240, 102)" : "rgba(29, 29, 31, 102)");
    tokens.fontFamily = QString::fromLatin1(
        "\"Segoe UI Variable Text\", \"Segoe UI Variable\", \"Microsoft YaHei UI\", "
        "\"Segoe UI\", sans-serif");
    tokens.fontPt = 10;
    tokens.fontBodyPx = 13;
    tokens.fontCaptionPx = 11;
    tokens.fontTitlePx = 15;
    tokens.fontLargeTitlePx = 20;
    return tokens;
}

} // namespace mviewer::ui
