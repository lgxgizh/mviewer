#pragma once

// Finder-style thumbnail card pieces (UI batch C2): a pre-rendered soft
// shadow, the selection ring, the filename pill and theme-aware badge colors.
// Everything expensive is rendered once per card size and theme and cached,
// so a grid paint only blits pixmaps.

#include <QColor>
#include <QPalette>
#include <QPixmap>
#include <QRect>
#include <QString>

class QPainter;

namespace thumb_card
{
constexpr int kCardRadius = 8;
constexpr int kRingWidth = 3;

// True when the surface (Base) is dark. Uses Base lightness only — QSS can
// leave Text white even in light theme, which used to flip this the wrong way.
bool isDarkPalette(const QPalette &palette);

// Ink for browse list / details / thumbnail filenames. Selected rows use
// HighlightedText (white on accent). Unselected rows force a dark ink on light
// surfaces and a light ink on dark surfaces when option.palette Text has been
// polluted by QSS (common for QListView custom delegates).
QColor itemTextColor(bool selected, const QPalette &palette);

// Soft drop shadow for a card of cardSize. The returned pixmap is larger than
// the card by shadowMargin() on each side and is cached per size and theme.
QPixmap cardShadow(const QSize &cardSize, bool dark);
int shadowMargin();
void drawCardShadow(QPainter *painter, const QRect &card, bool dark);

// Accent ring drawn just outside the card (inside the item rect).
void drawSelectionRing(QPainter *painter, const QRect &card, int outset, const QColor &accent);

// Filename text; when selected it sits on an accent pill with white text.
void drawNameText(QPainter *painter, const QRect &rect, const QString &name, bool selected,
                  const QPalette &palette);

QColor badgeBackground(const QPalette &palette);
QColor badgeText(const QPalette &palette);

int shadowCacheSizeForTest();
} // namespace thumb_card
