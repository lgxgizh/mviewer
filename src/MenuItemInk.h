#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QRect>
#include <QSize>

class QPainter;
class QStyleOptionMenuItem;

// Popup menu rows: one contrast-checked ink for the label, the check/radio
// indicator and the icon. QSS leaves QPalette::Text white on light menus
// (the #287 pollution), and Fusion inks its check box and radio dot from that
// palette and tints icons Accent on the accent highlight, so on light menus
// the indicators only showed on the blue hover row. The ink is chosen against
// the color actually painted under the row instead.
namespace mviewer::ui::menu_ink
{

enum class Row
{
    Normal,
    Selected,
    Disabled,
};

// WCAG contrast ratio between two opaque colors (1.0 .. 21.0).
double contrastRatio(const QColor &a, const QColor &b);

// `preferred` (a theme token) when it reads on `background`, otherwise the
// dark or light fallback that does: #1d1d1f on light menus, white on the
// accent highlight and on dark menus, mid grey for disabled rows.
QColor rowInk(Row row, const QColor &background, const QColor &preferred);

// The check/icon column Fusion reserves at the leading edge of a row.
QRect indicatorColumn(const QStyleOptionMenuItem &item);

// Monochrome menu icon recolored to `ink` (menus only use outline masks).
QPixmap inkedIcon(const QIcon &icon, QSize size, qreal dpr, bool disabled, const QColor &ink);

// Paints the check mark, radio dot or icon (with a checked plate) for `item`
// in `ink`. Call after the base style drew the row without them.
void drawIndicator(QPainter *painter, const QStyleOptionMenuItem &item, const QColor &ink,
                   QSize iconSize);

} // namespace mviewer::ui::menu_ink
