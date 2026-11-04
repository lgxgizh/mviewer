#include "MenuItemInk.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyle>
#include <QStyleOption>
#include <algorithm>
#include <cmath>

namespace mviewer::ui::menu_ink
{

namespace
{

double channel(int value)
{
    const double c = value / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color)
{
    const QColor rgb = color.toRgb();
    return 0.2126 * channel(rgb.red()) + 0.7152 * channel(rgb.green()) +
           0.0722 * channel(rgb.blue());
}

// Fusion's menu metrics (qfusionstyle.cpp CE_MenuItem): item h-margin 3 plus
// frame 2, minus 1; the column is at least 21 px and 79% of the row height.
constexpr int kCheckColumnOffset = 4;
constexpr int kMinCheckColumn = 21;

} // namespace

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor rowInk(Row row, const QColor &background, const QColor &preferred)
{
    const double needed = row == Row::Disabled ? 1.6 : 3.0;
    if (preferred.isValid() && contrastRatio(preferred, background) >= needed)
        return preferred;
    if (row == Row::Disabled)
        return {0x8e, 0x8e, 0x93};
    const QColor dark(0x1d, 0x1d, 0x1f);
    const QColor light(0xff, 0xff, 0xff);
    // The accent highlight is mid-luminance (#007aff reads about 4:1 against
    // either ink); keep white there, as macOS and the QSS label color do.
    if (row == Row::Selected && contrastRatio(light, background) >= 3.0)
        return light;
    return contrastRatio(dark, background) >= contrastRatio(light, background) ? dark : light;
}

QRect indicatorColumn(const QStyleOptionMenuItem &item)
{
    const int width = std::max(static_cast<int>(item.rect.height() * 0.79),
                               std::max(item.maxIconWidth, kMinCheckColumn));
    const QRect column(item.rect.x() + kCheckColumnOffset, item.rect.y(), width,
                       item.rect.height());
    return QStyle::visualRect(item.direction, item.rect, column);
}

QPixmap inkedIcon(const QIcon &icon, QSize size, qreal dpr, bool disabled, const QColor &ink)
{
    QPixmap pixmap = icon.pixmap(size, dpr, disabled ? QIcon::Disabled : QIcon::Normal, QIcon::Off);
    if (pixmap.isNull())
        return pixmap;
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(QRect(QPoint(0, 0), pixmap.size()), ink);
    return pixmap;
}

void drawIndicator(QPainter *painter, const QStyleOptionMenuItem &item, const QColor &ink,
                   QSize iconSize)
{
    const bool checkable = item.checkType != QStyleOptionMenuItem::NotCheckable;
    const bool checked = checkable && (item.checked || item.state.testFlag(QStyle::State_On));
    const bool disabled = !item.state.testFlag(QStyle::State_Enabled);
    const QRect column = indicatorColumn(item);
    const qreal side = std::min(column.width(), column.height()) * 0.62;
    const QRectF box(column.center().x() + 0.5 - side / 2.0, column.center().y() + 0.5 - side / 2.0,
                     side, side);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!item.icon.isNull())
    {
        if (checked)
        {
            // A soft plate in the row ink marks a checked icon action; Fusion's
            // sunken button panel read as a black block on light menus.
            QColor plate = ink;
            plate.setAlphaF(0.18F);
            painter->setPen(Qt::NoPen);
            painter->setBrush(plate);
            const qreal pad = side * 0.25;
            painter->drawRoundedRect(box.adjusted(-pad, -pad, pad, pad), 4.0, 4.0);
        }
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap pixmap = inkedIcon(item.icon, iconSize, dpr, disabled, ink);
        if (!pixmap.isNull())
        {
            QRect target(QPoint(0, 0), pixmap.deviceIndependentSize().toSize());
            target.moveCenter(column.center());
            painter->drawPixmap(target.topLeft(), pixmap);
        }
    }
    else if (checked && (item.checkType == QStyleOptionMenuItem::Exclusive))
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(ink);
        const qreal inset = side * 0.24;
        painter->drawEllipse(box.adjusted(inset, inset, -inset, -inset));
    }
    else if (checked)
    {
        QPainterPath mark;
        mark.moveTo(box.left() + side * 0.14, box.top() + side * 0.54);
        mark.lineTo(box.left() + side * 0.40, box.top() + side * 0.80);
        mark.lineTo(box.left() + side * 0.88, box.top() + side * 0.22);
        QPen pen(ink, std::max<qreal>(1.6, side * 0.14));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(mark);
    }
    painter->restore();
}

} // namespace mviewer::ui::menu_ink
