#include "Theme.h"
#include "ThemeMetrics.h"

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QPalette>
#include <QProxyStyle>
#include <QRegularExpression>
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

void applyGroup(QPalette &palette, QPalette::ColorGroup group, const ThemeTokens &tokens,
                bool disabled)
{
    QColor text = disabled ? QColor(tokens.textDisabled) : QColor(tokens.textPrimary);
    QColor button = disabled ? QColor(tokens.bg1) : QColor(tokens.bg2);
    palette.setColor(group, QPalette::Window, QColor(tokens.bg1));
    palette.setColor(group, QPalette::WindowText, text);
    palette.setColor(group, QPalette::Base, QColor(tokens.bg0));
    palette.setColor(group, QPalette::AlternateBase, QColor(tokens.sidebar));
    palette.setColor(group, QPalette::ToolTipBase, QColor(tokens.bg1));
    palette.setColor(group, QPalette::ToolTipText, QColor(tokens.textPrimary));
    palette.setColor(group, QPalette::Text, text);
    palette.setColor(group, QPalette::Button, button);
    palette.setColor(group, QPalette::ButtonText, text);
    palette.setColor(group, QPalette::BrightText, QColor(tokens.onAccent));
    palette.setColor(group, QPalette::Link, QColor(tokens.accent));
    palette.setColor(group, QPalette::LinkVisited, QColor(tokens.accent));
    palette.setColor(group, QPalette::Highlight, QColor(tokens.accent));
    palette.setColor(group, QPalette::HighlightedText, QColor(tokens.onAccent));
    palette.setColor(group, QPalette::Mid, QColor(tokens.border));
    palette.setColor(group, QPalette::Midlight, QColor(tokens.bg3));
    palette.setColor(group, QPalette::Light, QColor(tokens.bevel));
    palette.setColor(group, QPalette::Dark, kImageCanvas);
    palette.setColor(group, QPalette::Shadow, QColor(0x10, 0x10, 0x12));
    palette.setColor(group, QPalette::PlaceholderText, QColor(tokens.textDisabled));
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
    font.setFamilies({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI Variable"),
                      QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    // 13 px body at the 96 DPI baseline (9.75 pt). Point size tracks DPI.
    font.setPointSizeF(tokens.fontBodyPx * 72.0 / 96.0);
    font.setStyleHint(QFont::SansSerif, QFont::PreferOutline);
    QApplication::setFont(font);
}

// Fusion with the app spacing scale: layouts that do not set explicit margins
// or spacing (most dialogs, form layouts, button boxes) land on the same grid.
class ScaledStyle : public QProxyStyle
{
  public:
    using QProxyStyle::QProxyStyle;

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        namespace m = mviewer::ui::metrics;
        switch (metric)
        {
        case PM_LayoutLeftMargin:
        case PM_LayoutTopMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutBottomMargin:
            return (widget != nullptr && widget->isWindow()) ? m::kWindowMargin : m::kChildMargin;
        case PM_LayoutHorizontalSpacing:
        case PM_LayoutVerticalSpacing:
            return m::kLayoutSpacing;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// Every dialog (our own, QMessageBox, QInputDialog, Qt's file dialog) gets its
// accept button marked primary when shown, so the accent stays on the action
// that commits instead of following keyboard focus like QPushButton:default.
class DialogPolisher : public QObject
{
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && watched->isWidgetType())
        {
            if (auto *dialog = qobject_cast<QDialog *>(watched))
                markPrimaryButtons(dialog);
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    static void markPrimaryButtons(QDialog *dialog)
    {
        const auto boxes = dialog->findChildren<QDialogButtonBox *>();
        for (QDialogButtonBox *box : boxes)
        {
            const auto buttons = box->buttons();
            for (QAbstractButton *button : buttons)
            {
                const QDialogButtonBox::ButtonRole role = box->buttonRole(button);
                const bool primary =
                    role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole;
                if (button->property("primary").toBool() == primary)
                    continue;
                button->setProperty("primary", primary);
                button->style()->unpolish(button);
                button->style()->polish(button);
            }
        }
    }
};

void installDialogPolisher()
{
    static DialogPolisher *polisher = nullptr;
    if (polisher != nullptr || qApp == nullptr)
        return;
    polisher = new DialogPolisher(qApp);
    qApp->installEventFilter(polisher);
}

// Token colors baked into per-widget stylesheets and rich-text labels at build
// time are swapped to the new theme in one pass, so a live light/dark switch
// does not leave dark islands behind. Colors that are not tokens are untouched.
//
// Hex tokens remap by #rrggbb. rgba tokens remap by their full string: a second
// pass on the RGB alone would retint a just-written accent as the other theme's
// accentHover (those two share a hex across the pair). Literal #ffffff / #000000
// are never keys, so overlay white and black stay put.
struct RgbaTokenSwap
{
    QString from;
    QString to;
};

struct RetintPlan
{
    QHash<QString, QString> hex;
    QList<RgbaTokenSwap> rgba;
};

const QString ThemeTokens::*tokenColorFields[] = {
    &ThemeTokens::bg0,         &ThemeTokens::bg1,           &ThemeTokens::bg2,
    &ThemeTokens::bg3,         &ThemeTokens::sidebar,       &ThemeTokens::border,
    &ThemeTokens::separator,   &ThemeTokens::hover,         &ThemeTokens::bevel,
    &ThemeTokens::textPrimary, &ThemeTokens::textSecondary, &ThemeTokens::textDisabled,
    &ThemeTokens::accent,      &ThemeTokens::accentHover,   &ThemeTokens::accentPressed,
    &ThemeTokens::accentTint,  &ThemeTokens::selection,     &ThemeTokens::danger,
    &ThemeTokens::onAccent,    &ThemeTokens::scrollHandle,  &ThemeTokens::scrollHandleHover,
};

RetintPlan retintPlan(const ThemeTokens &from, const ThemeTokens &to)
{
    RetintPlan plan;
    for (const auto field : tokenColorFields)
    {
        const QString &oldValue = from.*field;
        const QString &newValue = to.*field;
        if (oldValue.isEmpty() || oldValue == newValue)
            continue;
        if (oldValue.startsWith(QLatin1String("rgba(")))
        {
            plan.rgba.append({oldValue, newValue});
            continue;
        }
        const QColor oldColor(oldValue);
        const QColor newColor(newValue);
        if (!oldColor.isValid() || !newColor.isValid())
            continue;
        const QString key = oldColor.name().toLower();
        if (key == QLatin1String("#ffffff") || key == QLatin1String("#000000"))
            continue;
        plan.hex.insert(key, newColor.name().toLower());
    }
    return plan;
}

QString remapLooseColors(const QString &text, const QHash<QString, QString> &map)
{
    static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}\\b"));
    static const QRegularExpression rgba(QStringLiteral("rgba\\((\\d+),\\s*(\\d+),\\s*(\\d+),"));
    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    auto it = hex.globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        const auto found = map.constFind(m.captured(0).toLower());
        if (found == map.constEnd())
            continue;
        out += text.mid(last, m.capturedStart(0) - last) + found.value();
        last = m.capturedEnd(0);
    }
    out += text.mid(last);

    QString result;
    last = 0;
    auto rit = rgba.globalMatch(out);
    while (rit.hasNext())
    {
        const QRegularExpressionMatch m = rit.next();
        const QColor c(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        const auto found = map.constFind(c.name());
        if (found == map.constEnd())
            continue;
        const QColor n(found.value());
        result += out.mid(last, m.capturedStart(0) - last) +
                  QStringLiteral("rgba(%1,%2,%3,").arg(n.red()).arg(n.green()).arg(n.blue());
        last = m.capturedEnd(0);
    }
    result += out.mid(last);
    return result;
}

QString remapColors(const QString &text, const RetintPlan &plan)
{
    QString protectedText = text;
    for (qsizetype i = 0; i < plan.rgba.size(); ++i)
    {
        const QString marker = QStringLiteral("\x01T%1\x01").arg(i);
        protectedText.replace(plan.rgba.at(i).from, marker);
    }
    QString remapped = remapLooseColors(protectedText, plan.hex);
    for (qsizetype i = 0; i < plan.rgba.size(); ++i)
        remapped.replace(QStringLiteral("\x01T%1\x01").arg(i), plan.rgba.at(i).to);
    return remapped;
}

void retintLiveWidgets(const ThemeTokens &from, const ThemeTokens &to)
{
    if (qApp == nullptr)
        return;
    const RetintPlan plan = retintPlan(from, to);
    if (plan.hex.isEmpty() && plan.rgba.isEmpty())
        return;
    const auto widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets)
    {
        const QString sheet = widget->styleSheet();
        if (!sheet.isEmpty())
        {
            const QString mapped = remapColors(sheet, plan);
            if (mapped != sheet)
                widget->setStyleSheet(mapped);
        }
        auto *label = qobject_cast<QLabel *>(widget);
        if (label != nullptr && label->textFormat() == Qt::RichText)
        {
            const QString text = label->text();
            const QString mapped = remapColors(text, plan);
            if (mapped != text)
                label->setText(mapped);
        }
    }
}

// In System mode, re-apply when Windows switches between light and dark apps.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
void onSystemColorSchemeChanged(Qt::ColorScheme)
{
    if (g_mode == ThemeMode::System)
        Theme::applyTheme(ThemeMode::System);
}
#endif

void installSystemSchemeFollower()
{
    static bool installed = false;
    if (installed || qApp == nullptr)
        return;
    installed = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Use a plain function (not a lambda) for the slot. clang-cl +
    // -fsanitize-trap=undefined traps inside Qt's QCallableObject path when
    // connecting a lambda here (Nightly UBSan selftest Illegal at Theme.cpp).
    QStyleHints *hints = QGuiApplication::styleHints();
    if (hints == nullptr)
        return;
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, qApp, onSystemColorSchemeChanged);
#endif
}

} // namespace

void Theme::initTheme()
{
    ensureThemeResources();
    QSettings settings;
    // Without a saved choice the fallback stays Dark; main() seeds "system" on a
    // first launch via seedDefaultThemeChoice().
    g_mode = themeModeFromKey(
        settings.value(QStringLiteral("uiTheme"), QStringLiteral("dark")).toString());
    applyTheme(g_mode);
}

void Theme::seedDefaultThemeChoice()
{
    QSettings settings;
    if (!settings.contains(QStringLiteral("uiTheme")))
        settings.setValue(QStringLiteral("uiTheme"), themeModeKey(ThemeMode::System));
}

QString Theme::themeModeKey(ThemeMode mode)
{
    switch (mode)
    {
    case ThemeMode::System:
        return QStringLiteral("system");
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
    default:
        return QStringLiteral("dark");
    }
}

ThemeMode Theme::themeModeFromKey(const QString &key)
{
    const QString lower = key.trimmed().toLower();
    if (lower == QStringLiteral("system"))
        return ThemeMode::System;
    if (lower == QStringLiteral("light"))
        return ThemeMode::Light;
    return ThemeMode::Dark;
}

void Theme::applyTheme(ThemeMode mode)
{
    ensureThemeResources();
    g_mode = mode;
    const ThemeTokens previous = g_tokens;
    g_tokens = tokensFor(mode, systemPrefersDark());
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(new ScaledStyle(fusion));
    QApplication::setPalette(buildPalette(g_tokens));
    applyThemeFont(g_tokens);
    if (qApp)
        qApp->setStyleSheet(buildStyleSheet(g_tokens));
    installDialogPolisher();
    retintLiveWidgets(previous, g_tokens);

    installSystemSchemeFollower();

    QSettings settings;
    settings.setValue(QStringLiteral("uiTheme"), themeModeKey(mode));
}

ThemeMode Theme::currentTheme()
{
    return g_mode;
}

QString Theme::themeModeName(ThemeMode mode)
{
    switch (mode)
    {
    case ThemeMode::System:
        return QStringLiteral("Follow system");
    case ThemeMode::Light:
        return QStringLiteral("Light mode");
    case ThemeMode::Dark:
    default:
        return QStringLiteral("Dark mode");
    }
}

bool Theme::systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    const QPalette pal = QGuiApplication::palette();
    return pal.color(QPalette::WindowText).lightness() > pal.color(QPalette::Window).lightness();
#endif
}

ThemeTokens Theme::tokensFor(ThemeMode mode, bool prefersDark)
{
    const bool dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && prefersDark);
    // ThemeMode::Light always uses the light token swap.
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
        {"{{sidebar}}", tokens.sidebar},
        {"{{border}}", tokens.border},
        {"{{separator}}", tokens.separator},
        {"{{hover}}", tokens.hover},
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
        {"{{scrollHandleHover}}", tokens.scrollHandleHover},
        {"{{scrollHandle}}", tokens.scrollHandle},
        {"{{fontFamily}}", tokens.fontFamily},
        {"{{space4}}", QString::number(tokens.space4)},
        {"{{space8}}", QString::number(tokens.space8)},
        {"{{space12}}", QString::number(tokens.space12)},
        {"{{space16}}", QString::number(tokens.space16)},
        {"{{radiusControl}}", QString::number(tokens.radiusControl)},
        {"{{radiusPanel}}", QString::number(tokens.radiusPanel)},
        {"{{radiusChip}}", QString::number(tokens.radiusChip)},
        {"{{radiusSearch}}", QString::number(tokens.radiusSearch)},
        {"{{radiusTooltip}}", QString::number(tokens.radiusTooltip)},
        {"{{fontBodyPx}}", QString::number(tokens.fontBodyPx)},
        {"{{fontCaptionPx}}", QString::number(tokens.fontCaptionPx)},
        {"{{fontTitlePx}}", QString::number(tokens.fontTitlePx)},
        {"{{fontLargeTitlePx}}", QString::number(tokens.fontLargeTitlePx)},
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
    case ThemeRole::Sidebar:
        return tokens.sidebar;
    case ThemeRole::Separator:
        return tokens.separator;
    case ThemeRole::Hover:
        return tokens.hover;
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
