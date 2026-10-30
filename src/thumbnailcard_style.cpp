// Finder-style thumbnail card pieces (UI batch C2). See thumbnailcard_style.h.
#include "thumbnailcard_style.h"

#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <vector>

namespace thumb_card
{
namespace
{
constexpr int kShadowMargin = 3;
constexpr size_t kMaxShadowCache = 8;

struct ShadowEntry
{
    int w = 0;
    int h = 0;
    bool dark = false;
    QPixmap pixmap;
};

std::vector<ShadowEntry> g_shadowCache;

// A few stacked translucent rounded rects approximate a 3 px blur without a
// per-pixel filter. Built once per size and theme.
QPixmap renderShadow(const QSize &cardSize, bool dark)
{
    const QSize full = cardSize + QSize(kShadowMargin * 2, kShadowMargin * 2);
    QImage image(full, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    const int baseAlpha = dark ? 30 : 16;
    for (int i = kShadowMargin; i >= 1; --i)
    {
        const QRectF r(kShadowMargin - i, kShadowMargin - i + 1, cardSize.width() + i * 2,
                       cardSize.height() + i * 2 - 1);
        p.setBrush(QColor(0, 0, 0, baseAlpha / i));
        p.drawRoundedRect(r, kCardRadius + i, kCardRadius + i);
    }
    p.end();
    return QPixmap::fromImage(image);
}

const std::array<QColor, 7> &labelColors(bool dark)
{
    static const std::array<QColor, 7> kDark = {QColor(),
                                                QColor(0xff, 0x45, 0x3a),
                                                QColor(0xff, 0x9f, 0x0a),
                                                QColor(0xff, 0xd6, 0x0a),
                                                QColor(0x30, 0xd1, 0x58),
                                                QColor(0x0a, 0x84, 0xff),
                                                QColor(0xbf, 0x5a, 0xf2)};
    static const std::array<QColor, 7> kLight = {QColor(),
                                                 QColor(0xff, 0x3b, 0x30),
                                                 QColor(0xff, 0x95, 0x00),
                                                 QColor(0xff, 0xcc, 0x00),
                                                 QColor(0x34, 0xc7, 0x59),
                                                 QColor(0x00, 0x7a, 0xff),
                                                 QColor(0xaf, 0x52, 0xde)};
    return dark ? kDark : kLight;
}
} // namespace

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Text).lightness() > palette.color(QPalette::Base).lightness();
}

int shadowMargin()
{
    return kShadowMargin;
}

QPixmap cardShadow(const QSize &cardSize, bool dark)
{
    if (cardSize.isEmpty())
        return QPixmap();
    for (size_t i = 0; i < g_shadowCache.size(); ++i)
    {
        const ShadowEntry &e = g_shadowCache[i];
        if (e.w == cardSize.width() && e.h == cardSize.height() && e.dark == dark)
        {
            if (i != 0)
                std::swap(g_shadowCache[i], g_shadowCache[0]);
            return g_shadowCache[0].pixmap;
        }
    }
    ShadowEntry entry{cardSize.width(), cardSize.height(), dark, renderShadow(cardSize, dark)};
    if (g_shadowCache.size() >= kMaxShadowCache)
        g_shadowCache.pop_back();
    g_shadowCache.insert(g_shadowCache.begin(), entry);
    return g_shadowCache[0].pixmap;
}

void drawCardShadow(QPainter *painter, const QRect &card, bool dark)
{
    const QPixmap shadow = cardShadow(card.size(), dark);
    if (!shadow.isNull())
        painter->drawPixmap(card.x() - kShadowMargin, card.y() - kShadowMargin, shadow);
}

void drawSelectionRing(QPainter *painter, const QRect &card, int outset, const QColor &accent)
{
    const int width = std::clamp(outset, 1, kRingWidth);
    const qreal half = width / 2.0;
    // Centre the stroke in the band just outside the card so it hugs the edge.
    const QRectF ring = QRectF(card).adjusted(-half, -half, half, half);
    QPen pen(accent);
    pen.setWidthF(width);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(ring, kCardRadius + half, kCardRadius + half);
    painter->restore();
}

void drawNameText(QPainter *painter, const QRect &rect, const QString &name, bool selected,
                  const QPalette &palette)
{
    const QFontMetrics fm = painter->fontMetrics();
    const int padX = 6;
    const QString text = fm.elidedText(name, Qt::ElideRight, qMax(1, rect.width() - padX * 2));
    if (!selected)
    {
        painter->setPen(palette.color(QPalette::Text));
        painter->drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, text);
        return;
    }
    const int textW = fm.horizontalAdvance(text);
    const int pillH = qMin(rect.height(), fm.height() + 2);
    const int pillW = qMin(rect.width(), textW + padX * 2);
    const QRect pill(rect.x() + (rect.width() - pillW) / 2, rect.y() + (rect.height() - pillH) / 2,
                     pillW, pillH);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette.color(QPalette::Highlight));
    painter->drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
    painter->restore();
    painter->setPen(palette.color(QPalette::HighlightedText));
    painter->drawText(pill, Qt::AlignCenter | Qt::TextSingleLine, text);
}

QColor labelColor(int index, bool dark)
{
    if (index < 1 || index > 6)
        return QColor();
    return labelColors(dark)[static_cast<size_t>(index)];
}

QColor ratingColor(bool dark)
{
    return labelColor(3, dark);
}

QColor rejectTint(const QPalette &palette)
{
    QColor c = labelColor(1, isDarkPalette(palette));
    c.setAlpha(90);
    return c;
}

QColor badgeBackground(const QPalette &palette)
{
    QColor c = palette.color(QPalette::Window);
    c.setAlpha(215);
    return c;
}

QColor badgeText(const QPalette &palette)
{
    return palette.color(QPalette::Text);
}

int shadowCacheSizeForTest()
{
    return static_cast<int>(g_shadowCache.size());
}
} // namespace thumb_card
