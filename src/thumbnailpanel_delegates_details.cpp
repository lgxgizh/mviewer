// ThumbnailPanel details-row delegate. Split from thumbnailpanel_delegates.cpp
// so each translation unit stays under the 800-line cap.
#include "thumbnailpanel_p.h"

#include <QFontMetrics>

namespace
{
void drawDetailsThumb(QPainter *painter, const QRect &thumbR, const QString &path,
                      const ThumbnailPanel *panel)
{
    const QPixmap pm = panel->thumbReady(path);
    if (!pm.isNull())
    {
        const QPixmap scaled = thumb_delegate::cachedScaledPixmap(pm, thumbR.size());
        painter->drawPixmap(thumbR.x() + (48 - scaled.width()) / 2,
                            thumbR.y() + (48 - scaled.height()) / 2, scaled);
    }
    else if (panel->thumbFailed(path))
    {
        painter->fillRect(thumbR, QColor(200, 200, 200));
        painter->setPen(QColor(150, 150, 150));
        QFont f = painter->font();
        f.setPointSize(qMax(7, f.pointSize() - 1));
        painter->setFont(f);
        painter->drawText(thumbR, Qt::AlignCenter, "无法\n加载");
    }
    else
    {
        painter->fillRect(thumbR, QColor(228, 228, 228));
    }
}

void drawDetailsRating(QPainter *painter, const QRect &rateR, const QString &path, bool sel,
                       const QColor &textColor)
{
    const auto &rs = mviewer::core::RatingStore::instance();
    const std::string ep = path.toStdString();
    const int stars = rs.rating(ep);
    if (stars > 0)
    {
        QString starStr;
        starStr.reserve(5);
        for (int s = 0; s < 5; ++s)
            starStr += (s < stars ? QStringLiteral("★") : QStringLiteral("☆"));
        painter->save();
        painter->setPen(sel ? textColor : QColor(255, 179, 0));
        painter->drawText(rateR, Qt::AlignVCenter | Qt::TextSingleLine, starStr);
        painter->restore();
    }
    else
    {
        painter->drawText(rateR, Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("-"));
    }
}

void drawDetailsLabel(QPainter *painter, const QRect &labelR, const QString &path)
{
    const auto &rs = mviewer::core::RatingStore::instance();
    const std::string ep = path.toStdString();
    const int label = rs.colorLabel(ep);
    if (label > 0)
    {
        static const QColor kLabelColors[7] = {QColor(),
                                               QColor(229, 57, 53),
                                               QColor(251, 140, 0),
                                               QColor(249, 215, 41),
                                               QColor(67, 160, 71),
                                               QColor(30, 136, 229),
                                               QColor(142, 36, 170)};
        static const char *kLabelNames[7] = {"", "红", "橙", "黄", "绿", "蓝", "紫"};
        const QColor chip = kLabelColors[label];
        const int cs = 12;
        const QRect chipR(labelR.x(), labelR.y() + (labelR.height() - cs) / 2, cs, cs);
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(chip);
        painter->drawRoundedRect(chipR, 3, 3);
        painter->restore();
        const QRect chipTextR(labelR.x() + cs + 6, labelR.y(), labelR.width() - cs - 6,
                              labelR.height());
        painter->drawText(chipTextR, Qt::AlignVCenter | Qt::TextSingleLine,
                          QString::fromUtf8(kLabelNames[label]));
    }
    else
    {
        painter->drawText(labelR, Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("-"));
    }
}

void drawDetailsExif(QPainter *painter, const DetailLayout &L, const QString &path, bool sel,
                     const QStyleOptionViewItem &option, const ThumbnailPanel *panel)
{
    const QString cam = panel->metaCameraForPath(path).trimmed();
    const QString lens = panel->metaLensForPath(path).trimmed();
    const int iso = panel->metaIsoForPath(path);
    painter->setFont(option.font);
    painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                        : option.palette.color(QPalette::Text));
    painter->drawText(L.camera, Qt::AlignVCenter | Qt::TextSingleLine,
                      cam.isEmpty() ? QStringLiteral("-") : cam);
    painter->drawText(L.lens, Qt::AlignVCenter | Qt::TextSingleLine,
                      lens.isEmpty() ? QStringLiteral("-") : lens);
    painter->drawText(L.iso, Qt::AlignVCenter | Qt::TextSingleLine,
                      iso > 0 ? QString("ISO %1").arg(iso) : QStringLiteral("-"));
}
} // namespace

// ---- DetailsDelegate ---------------------------------------------------------

void ThumbnailPanel::DetailsDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    // QStyledItemDelegate may call paint() with an invalid index (e.g. empty
    // view, filter cleared, or during layout).  Fall back to the default
    // rendering so the viewport background is still drawn.
    if (!index.isValid())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const QStringList &paths = m_panel->pathList();
    if (index.row() < 0 || index.row() >= paths.size())
        return;
    const QString path = paths.at(index.row());
    const QString name = index.data(Qt::DisplayRole).toString();
    // M46: the Details row paints EXCLUSIVELY from scan-cached Entry data.
    // Constructing QFileInfo here and calling size()/lastModified() performed
    // two filesystem stats per row per repaint (stalls on NAS/network/locked
    // files); size/mtime are now captured once by the directory scan and the
    // suffix is parsed lexically.
    const ThumbnailPanel::Entry *entry = m_panel->entryForPath(path);
    const qint64 cachedSize = entry ? entry->size : -1;
    const QDateTime cachedMtime = entry ? entry->date : QDateTime();
    const QString cachedSuffix = thumb_delegate::fileSuffixFromPath(path);

    const bool sel = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    QColor bg;
    if (sel)
        bg = option.palette.color(QPalette::Highlight);
    else if (hover)
        bg = option.palette.color(QPalette::Midlight);
    else
        bg = option.palette.color(index.row() & 1 ? QPalette::AlternateBase : QPalette::Base);
    painter->fillRect(option.rect, bg);

    painter->save();
    const QRect r = option.rect.adjusted(4, 2, -4, -2);
    const DetailLayout L =
        detailLayout(option.rect.adjusted(0, 2, 0, -2), m_panel->detailColWidths());

    // Column 1: small thumbnail (48×48)
    const QRect thumbR(L.thumb.x(), r.y() + (r.height() - 48) / 2, 48, 48);
    drawDetailsThumb(painter, thumbR, path, m_panel);

    const QColor textColor = sel ? option.palette.color(QPalette::HighlightedText)
                                 : option.palette.color(QPalette::Text);
    painter->setPen(textColor);

    QFont nameFont = painter->font();
    nameFont.setBold(true);
    painter->setFont(nameFont);

    // Column 2: filename (elide within the current column width)
    const QString elidedName =
        QFontMetrics(nameFont).elidedText(name, Qt::ElideMiddle, L.name.width());
    painter->drawText(L.name, Qt::AlignVCenter | Qt::TextSingleLine, elidedName);

    // Column 3: resolution — resolve by source path because index.row() belongs
    // to the filtered model and cannot index m_allEntries.
    painter->setFont(option.font);
    QString resStr = "-";
    if (entry && entry->width > 0 && entry->height > 0)
        resStr = QString("%1×%2").arg(entry->width).arg(entry->height);
    painter->drawText(L.res, Qt::AlignVCenter | Qt::TextSingleLine, resStr);

    // Column 4: file size (scan-cached; "-" when the entry is unknown).
    painter->drawText(L.size, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedSize >= 0 ? thumb_delegate::formatFileSize(cachedSize)
                                      : QStringLiteral("-"));

    // Column 5: modified date (scan-cached).
    painter->drawText(L.date, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedMtime.isValid() ? cachedMtime.toString("yyyy-MM-dd hh:mm:ss")
                                            : QStringLiteral("-"));

    // Column 6: format (lexical suffix, never a filesystem query).
    painter->drawText(L.fmt, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedSuffix.isEmpty() ? QStringLiteral("IMAGE") : cachedSuffix.toUpper());

    // Column 7: rating (P0-4). Draw filled/empty stars from the RatingStore.
    drawDetailsRating(painter, L.rate, path, sel, textColor);

    // Column 8: color label (P0-4). Draw a small colored chip + name.
    drawDetailsLabel(painter, L.label, path);

    // Columns 9-11: EXIF (camera / lens / ISO) — P0 #① professional columns for
    // image algorithm engineers. Backed by the metadata index (ensureMetaIndex).
    drawDetailsExif(painter, L, path, sel, option, m_panel);

    painter->restore();
}

QSize ThumbnailPanel::DetailsDelegate::sizeHint(const QStyleOptionViewItem &,
                                                const QModelIndex &) const
{
    // Wide enough to show every column without overlap; the Details view scrolls
    // horizontally when the viewport is narrower (see setViewMode Details branch).
    const int w = qMax(detailTotalWidth(m_panel->detailColWidths()), m_panel->viewport()->width());
    return QSize(w, kDetailsItemHeight);
}

bool ThumbnailPanel::DetailsDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                                const QStyleOptionViewItem &,
                                                const QModelIndex &index)
{
    return thumb_delegate::showThumbnailTooltip(event, view, index, m_panel);
}
