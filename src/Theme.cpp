#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

static void ensureThemeResources()
{
    Q_INIT_RESOURCE(mviewer);
}

namespace mviewer::ui
{

namespace
{

ThemeTokens g_tokens = makeThemeTokens(true);
ThemeMode g_mode = ThemeMode::Dark;

// Image canvases (RawImageView, compare base surface) fill with QPalette::Dark.
// Keep that role near-black so the picture well does not follow chrome layers.
constexpr QColor kImageCanvas(0x12, 0x12, 0x14);

struct TokenSlot
{
    const char *key;
    QString value;
};

QColor mix30(const QColor &accent, const QColor &base)
{
    const auto channel = [](int src, int dst) { return (src * 30 + dst * 70) / 100; };
    return QColor(channel(accent.red(), base.red()), channel(accent.green(), base.green()),
                  channel(accent.blue(), base.blue()));
}

void applyGroup(QPalette &palette, QPalette::ColorGroup group, const ThemeTokens &tokens,
                bool disabled)
{
    QColor text = disabled ? QColor(tokens.textDisabled) : QColor(tokens.textPrimary);
    QColor button = disabled ? QColor(tokens.bg1) : QColor(tokens.bg2);
    palette.setColor(group, QPalette::Window, QColor(tokens.bg1));
    palette.setColor(group, QPalette::WindowText, text);
    palette.setColor(group, QPalette::Base, QColor(tokens.bg0));
    palette.setColor(group, QPalette::AlternateBase, QColor(tokens.bg1));
    palette.setColor(group, QPalette::ToolTipBase, QColor(tokens.bg2));
    palette.setColor(group, QPalette::ToolTipText, QColor(tokens.textPrimary));
    palette.setColor(group, QPalette::Text, text);
    palette.setColor(group, QPalette::Button, button);
    palette.setColor(group, QPalette::ButtonText, text);
    palette.setColor(group, QPalette::BrightText, QColor(tokens.onAccent));
    palette.setColor(group, QPalette::Link, QColor(tokens.accent));
    palette.setColor(group, QPalette::LinkVisited, QColor(tokens.accent));
    palette.setColor(group, QPalette::Highlight, mix30(QColor(tokens.accent), QColor(tokens.bg0)));
    palette.setColor(group, QPalette::HighlightedText, QColor(tokens.textPrimary));
    palette.setColor(group, QPalette::Mid, QColor(tokens.border));
    palette.setColor(group, QPalette::Midlight, QColor(tokens.bg3));
    palette.setColor(group, QPalette::Dark, kImageCanvas);
    palette.setColor(group, QPalette::Shadow, QColor(0x10, 0x10, 0x12));
    palette.setColor(group, QPalette::PlaceholderText, QColor(tokens.textSecondary));
}

QPalette buildPalette(const ThemeTokens &tokens)
{
    QPalette palette;
    applyGroup(palette, QPalette::Active, tokens, false);
    applyGroup(palette, QPalette::Inactive, tokens, false);
    applyGroup(palette, QPalette::Disabled, tokens, true);
    return palette;
}

void applyThemeFont(const ThemeTokens &tokens)
{
    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI Variable"), QStringLiteral("Microsoft YaHei UI"),
                      QStringLiteral("Segoe UI")});
    font.setPointSize(tokens.fontPt);
    font.setStyleHint(QFont::SansSerif, QFont::PreferOutline);
    QApplication::setFont(font);
}

} // namespace

void Theme::initTheme()
{
    ensureThemeResources();
    QSettings settings;
    QString mode = settings.value(QStringLiteral("uiTheme"), QStringLiteral("dark")).toString();
    g_mode = (mode.toLower() == QStringLiteral("system")) ? ThemeMode::System : ThemeMode::Dark;
    applyTheme(g_mode);
}

void Theme::applyTheme(ThemeMode mode)
{
    ensureThemeResources();
    g_mode = mode;
    g_tokens = tokensFor(mode, systemPrefersDark());
    if (QStyle *style = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(style);
    QApplication::setPalette(buildPalette(g_tokens));
    applyThemeFont(g_tokens);
    if (qApp)
        qApp->setStyleSheet(buildStyleSheet(g_tokens));

    QSettings settings;
    settings.setValue(QStringLiteral("uiTheme"),
                      mode == ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("system"));
}

ThemeMode Theme::currentTheme()
{
    return g_mode;
}

QString Theme::themeModeName(ThemeMode mode)
{
    return mode == ThemeMode::Dark ? QStringLiteral("深色模式") : QStringLiteral("系统默认");
}

bool Theme::systemPrefersDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

ThemeTokens Theme::tokensFor(ThemeMode mode, bool prefersDark)
{
    const bool dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && prefersDark);
    return makeThemeTokens(dark);
}

QString Theme::buildStyleSheet(const ThemeTokens &tokens)
{
    ensureThemeResources();
    QFile file(QStringLiteral(":/theme/dark.qss"));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QString qss = QString::fromUtf8(file.readAll());
    const TokenSlot rows[] = {
        {"{{bg0}}", tokens.bg0},
        {"{{bg1}}", tokens.bg1},
        {"{{bg2}}", tokens.bg2},
        {"{{bg3}}", tokens.bg3},
        {"{{border}}", tokens.border},
        {"{{textPrimary}}", tokens.textPrimary},
        {"{{textSecondary}}", tokens.textSecondary},
        {"{{textDisabled}}", tokens.textDisabled},
        {"{{accentHover}}", tokens.accentHover},
        {"{{accentPressed}}", tokens.accentPressed},
        {"{{accentTint}}", tokens.accentTint},
        {"{{accent}}", tokens.accent},
        {"{{selection}}", tokens.selection},
        {"{{danger}}", tokens.danger},
        {"{{onAccent}}", tokens.onAccent},
        {"{{fontFamily}}", tokens.fontFamily},
        {"{{space4}}", QString::number(tokens.space4)},
        {"{{space8}}", QString::number(tokens.space8)},
        {"{{space12}}", QString::number(tokens.space12)},
        {"{{space16}}", QString::number(tokens.space16)},
        {"{{radiusControl}}", QString::number(tokens.radiusControl)},
        {"{{radiusPanel}}", QString::number(tokens.radiusPanel)},
        {"{{fontPt}}", QString::number(tokens.fontPt)},
    };
    for (const TokenSlot &row : rows)
        qss.replace(QLatin1String(row.key), row.value);
    return qss;
}

QString Theme::themeColor(ThemeRole role)
{
    const ThemeTokens &tokens = g_tokens;
    switch (role)
    {
    case ThemeRole::Bg0:
        return tokens.bg0;
    case ThemeRole::Bg1:
        return tokens.bg1;
    case ThemeRole::Bg2:
        return tokens.bg2;
    case ThemeRole::Bg3:
        return tokens.bg3;
    case ThemeRole::Border:
        return tokens.border;
    case ThemeRole::TextPrimary:
        return tokens.textPrimary;
    case ThemeRole::TextSecondary:
        return tokens.textSecondary;
    case ThemeRole::TextDisabled:
        return tokens.textDisabled;
    case ThemeRole::Accent:
        return tokens.accent;
    case ThemeRole::AccentHover:
        return tokens.accentHover;
    case ThemeRole::AccentPressed:
        return tokens.accentPressed;
    case ThemeRole::Danger:
        return tokens.danger;
    default:
        return tokens.textPrimary;
    }
}

QString Theme::themeRgba(ThemeRole role, int alpha)
{
    QColor color(themeColor(role));
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

} // namespace mviewer::ui
