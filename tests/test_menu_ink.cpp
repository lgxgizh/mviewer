// Popup menu indicator/icon ink regression (light-theme menus showed check
// marks, radio dots and icons only on the blue hover row). Mirrors #287's
// polluted-palette test: QSS can leave QPalette::Text white on a light menu,
// and the row ink must not follow it.
#include "test_menu_ink.h"
#include "MenuItemInk.h"
#include "Theme.h"
#include "ThemeIcons.h"

#include <QApplication>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <algorithm>
#include <cstdio>

namespace
{

int g_menuFailures = 0;

void expect(bool cond, const char *msg)
{
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond)
        ++g_menuFailures;
}

QPalette pollutedPalette(const QColor &window, const QColor &text)
{
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::Base, window);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Highlight, QColor(0x00, 0x7a, 0xff));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    return palette;
}

struct ColumnInk
{
    bool inked = false;
    int darkest = 255;
    int lightest = 0;
};

// Draws one row through the menu's real style (QSS + ScaledStyle + Fusion)
// over `fill` and reports the ink found in the check/icon column.
ColumnInk renderRow(QMenu &menu, const QPalette &palette, const QColor &fill, bool selected,
                    QStyleOptionMenuItem::CheckType checkType, bool checked, const QIcon &icon)
{
    QStyleOptionMenuItem opt;
    opt.initFrom(&menu);
    opt.palette = palette;
    opt.rect = QRect(0, 0, 220, 26);
    opt.menuItemType = QStyleOptionMenuItem::Normal;
    opt.checkType = checkType;
    opt.checked = checked;
    opt.icon = icon;
    opt.maxIconWidth = 16;
    opt.text = QStringLiteral("Item\tCtrl+1");
    opt.state = QStyle::State_Enabled;
    if (selected)
        opt.state |= QStyle::State_Selected;

    QImage img(opt.rect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(fill);
    {
        QPainter p(&img);
        menu.style()->drawControl(QStyle::CE_MenuItem, &opt, &p, &menu);
    }
    const QRect column = mviewer::ui::menu_ink::indicatorColumn(opt).adjusted(2, 4, -2, -4);
    ColumnInk ink;
    for (int y = column.top(); y <= column.bottom(); ++y)
        for (int x = column.left(); x <= column.right(); ++x)
        {
            const QColor px = QColor::fromRgb(img.pixel(x, y));
            if (px.rgb() == fill.rgb())
                continue;
            // Ignore the highlight's own antialiased edge pixels.
            ink.inked = true;
            ink.darkest = std::min(ink.darkest, px.lightness());
            ink.lightest = std::max(ink.lightest, px.lightness());
        }
    return ink;
}

void checkPureInk()
{
    using mviewer::ui::menu_ink::Row;
    using mviewer::ui::menu_ink::rowInk;
    const QColor lightMenu(0xf5, 0xf5, 0xf7);
    const QColor darkMenu(0x2a, 0x2a, 0x2c);
    const QColor accent(0x00, 0x7a, 0xff);
    const QColor white(0xff, 0xff, 0xff);

    expect(rowInk(Row::Normal, lightMenu, white) == QColor(0x1d, 0x1d, 0x1f),
           "white (polluted) ink on a light menu falls back to #1d1d1f");
    expect(rowInk(Row::Normal, lightMenu, QColor(0x1d, 0x1d, 0x1f)) == QColor(0x1d, 0x1d, 0x1f),
           "light menu keeps the textPrimary token");
    expect(rowInk(Row::Selected, accent, accent) == white,
           "accent ink on the accent highlight falls back to white");
    expect(rowInk(Row::Selected, QColor(0x0a, 0x84, 0xff), white) == white,
           "dark-theme highlight keeps white ink");
    expect(rowInk(Row::Normal, darkMenu, QColor(0x1d, 0x1d, 0x1f)).lightness() > 200,
           "dark ink on a dark menu falls back to a light ink");
    expect(rowInk(Row::Normal, darkMenu, QColor(0xeb, 0xeb, 0xf0)) == QColor(0xeb, 0xeb, 0xf0),
           "dark menu keeps its light textPrimary token");
    expect(rowInk(Row::Disabled, lightMenu, lightMenu).isValid() &&
               rowInk(Row::Disabled, lightMenu, lightMenu) != lightMenu,
           "disabled ink never matches the menu background");
}

void checkRenderedRows(bool dark)
{
    using mviewer::ui::Theme;
    using mviewer::ui::ThemeMode;
    using mviewer::ui::ThemeRole;
    Theme::applyTheme(dark ? ThemeMode::Dark : ThemeMode::Light);
    const QColor menuBg(Theme::themeColor(ThemeRole::Bg1));
    const QColor accent(Theme::themeColor(ThemeRole::Accent));
    // Light: Text leaks white (the #287 pollution). Dark: Text leaks dark.
    const QPalette palette =
        pollutedPalette(menuBg, dark ? QColor(0x1d, 0x1d, 0x1f) : QColor(0xff, 0xff, 0xff));
    QMenu menu;
    const QIcon icon = mviewer::ui::toolbarIcon("open");

    struct Case
    {
        const char *name;
        QStyleOptionMenuItem::CheckType type;
        bool checked;
        bool withIcon;
    };
    const Case cases[] = {
        {"check mark", QStyleOptionMenuItem::NonExclusive, true, false},
        {"radio dot", QStyleOptionMenuItem::Exclusive, true, false},
        {"icon", QStyleOptionMenuItem::NotCheckable, false, true},
        {"checked icon", QStyleOptionMenuItem::NonExclusive, true, true},
    };
    for (const Case &c : cases)
    {
        const QIcon rowIcon = c.withIcon ? icon : QIcon();
        const ColumnInk normal =
            renderRow(menu, palette, menuBg, false, c.type, c.checked, rowIcon);
        const QByteArray what = QByteArray(dark ? "dark" : "light") + " menu " + c.name;
        expect(normal.inked, (what + " is drawn on the plain row").constData());
        if (dark)
            expect(normal.lightest > 150, (what + " uses light ink on the dark menu").constData());
        else
            expect(normal.darkest < 110, (what + " uses dark ink on the light menu").constData());

        const ColumnInk hover = renderRow(menu, palette, accent, true, c.type, c.checked, rowIcon);
        expect(hover.inked && hover.lightest > 200,
               (what + " turns white on the hover highlight").constData());
    }

    // An unchecked check box row shows nothing (macOS-style), not an empty box.
    const ColumnInk unchecked =
        renderRow(menu, palette, menuBg, false, QStyleOptionMenuItem::NonExclusive, false, QIcon());
    expect(!unchecked.inked, dark ? "dark menu unchecked row has an empty column"
                                  : "light menu unchecked row has an empty column");
}

} // namespace

int runMenuInkTests()
{
    g_menuFailures = 0;
    checkPureInk();
    checkRenderedRows(false);
    checkRenderedRows(true);
    return g_menuFailures;
}
