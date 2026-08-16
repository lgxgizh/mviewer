// ThumbnailPanel item delegates: thumbnail grid, details row, list row (M20 P0#3).
#include "thumbnailpanel_p.h"

namespace
{
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

QColor blended(const QColor &base, const QColor &accent, int alpha)
{
    QColor result = base;
    result.setRed((base.red() * (255 - alpha) + accent.red() * alpha) / 255);
    result.setGreen((base.green() * (255 - alpha) + accent.green() * alpha) / 255);
    result.setBlue((base.blue() * (255 - alpha) + accent.blue() * alpha) / 255);
    return result;
}

// M46: paint-path helpers are STRICTLY lexical. No QFileInfo is constructed in
// any delegate paint call, so scrolling/repaint can never stall on disk, NAS,
// file locks or antivirus metadata queries. Size/mtime come from the
// scan-cached Entry; the suffix/fileName helpers below only split the path
// string (QFileInfo::fileName()/suffix() are lexical, but keeping them out of
// the hot path makes the "paint does no filesystem work" property structural).

QString fileSuffixFromPath(const QString &path)
{
    const int slash = qMax(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    const QString base = slash >= 0 ? path.mid(slash + 1) : path;
    const int dot = base.lastIndexOf('.');
    if (dot <= 0 || dot == base.size() - 1)
        return QString();
    return base.mid(dot + 1);
}

QString fileNameFromPath(const QString &path)
{
    const int slash = qMax(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QString thumbInfoText(const ThumbnailPanel::Entry *entry, const QString &suffix)
{
    const QString format = suffix.isEmpty() ? QStringLiteral("IMAGE") : suffix.toUpper();
    if (entry && entry->width > 0 && entry->height > 0)
        return QStringLiteral("%1 × %2 · %3")
            .arg(entry->width)
            .arg(entry->height)
            .arg(format);

    // M46: byte count comes from the scan-cached Entry; a missing entry shows
    // "0 B" instead of stat()ing the file during paint.
    const qint64 bytes = entry ? entry->size : 0;
    return format + QStringLiteral(" · ") + formatFileSize(bytes);
}
} // namespace

void ThumbnailPanel::ThumbDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                          const QModelIndex &index) const
{
    const QStringList &paths = m_panel->pathList();
    if (index.row() < 0 || index.row() >= paths.size() || option.rect.isEmpty())
        return;

    const QString path = paths.at(index.row());
    const QString name = index.data(Qt::DisplayRole).toString();
    // M46: paint reads ONLY scan-cached entry data — no QFileInfo here.
    const ThumbnailPanel::Entry *entry = m_panel->entryForPath(path);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const ThumbnailPanel::ViewMode mode = m_panel->viewMode();
    const bool richFooter = mode == ThumbnailPanel::Thumbnail || mode == ThumbnailPanel::LargeIcon;
    const bool nameFooter = richFooter || mode == ThumbnailPanel::SmallIcon;
    const bool compactImageOnly = mode == ThumbnailPanel::Compact ||
                                  mode == ThumbnailPanel::Filmstrip;

    painter->save();
    painter->setClipRect(option.rect);

    const QColor base = option.palette.color(QPalette::Base);
    const QColor accent = option.palette.color(QPalette::Highlight);
    const QColor cardBg = selected ? blended(base, accent, 30)
                                   : (hovered ? blended(base, accent, 12) : base);
    const QRect card = option.rect.adjusted(compactImageOnly ? 1 : 3, compactImageOnly ? 1 : 3,
                                            compactImageOnly ? -1 : -3,
                                            compactImageOnly ? -1 : -3);
    painter->fillRect(option.rect, base);
    painter->setPen(Qt::NoPen);
    painter->setBrush(cardBg);
    painter->drawRoundedRect(card, 4, 4);

    const int footerHeight = richFooter ? 42 : (nameFooter ? 20 : 0);
    const int imagePadding = compactImageOnly ? 4 : 7;
    const int availableImage = qMax(1, card.height() - footerHeight - imagePadding - 4);
    const int imageSize = qMax(1, qMin(thumbSize(), qMin(card.width() - imagePadding * 2,
                                                         availableImage)));
    const QRect thumbRect(card.x() + (card.width() - imageSize) / 2, card.y() + imagePadding,
                          imageSize, imageSize);

    // A palette-derived well makes portrait and transparent images legible on
    // both themes, while the border keeps the image boundary visible in a
    // sparse FastStone-like gallery.
    const QColor well = option.palette.color(QPalette::AlternateBase);
    painter->fillRect(thumbRect, well);
    painter->setPen(option.palette.color(QPalette::Mid));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));

    const QPixmap pm = m_panel->thumbReady(path);
    if (!pm.isNull())
    {
        const QPixmap scaled =
            pm.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter->drawPixmap(thumbRect.x() + (thumbRect.width() - scaled.width()) / 2,
                            thumbRect.y() + (thumbRect.height() - scaled.height()) / 2, scaled);
    }
    else if (m_panel->thumbFailed(path))
    {
        painter->fillRect(thumbRect, blended(well, option.palette.color(QPalette::Mid), 35));
        painter->setPen(option.palette.color(QPalette::Text));
        QFont f = painter->font();
        f.setPointSize(qMax(7, f.pointSize() - 1));
        painter->setFont(f);
        painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("无法加载"));
    }

    if (richFooter)
    {
        const QRect footer(card.x() + 7, thumbRect.bottom() + 3, qMax(1, card.width() - 14),
                           qMax(1, card.bottom() - thumbRect.bottom() - 6));
        const int infoHeight = qMin(17, footer.height());
        const QRect infoRect(footer.x(), footer.y(), footer.width(), infoHeight);
        const QRect nameRect(footer.x(), infoRect.bottom() + 1, footer.width(),
                             qMax(1, footer.bottom() - infoRect.bottom() - 1));
        QFont infoFont = painter->font();
        infoFont.setPointSize(qMax(7, infoFont.pointSize() - 1));
        painter->setFont(infoFont);
        painter->setPen(option.palette.color(QPalette::Text));
        const QString info = thumbInfoText(entry, fileSuffixFromPath(path));
        const QString infoText =
            painter->fontMetrics().elidedText(info, Qt::ElideRight, infoRect.width());
        painter->drawText(infoRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                          infoText);

        QFont nameFont = painter->font();
        nameFont.setPointSize(qMax(8, nameFont.pointSize()));
        painter->setFont(nameFont);
        const QString nameText =
            painter->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
                          nameText);
    }
    else if (nameFooter)
    {
        const QRect nameRect(card.x() + 5, thumbRect.bottom() + 3, qMax(1, card.width() - 10),
                             qMax(1, card.bottom() - thumbRect.bottom() - 5));
        painter->setPen(option.palette.color(QPalette::Text));
        const QString nameText =
            painter->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
                          nameText);
    }

    // Preserve the rating, label, reject and pick overlays, but keep every
    // mark inside the cell so no overlay can eat the filename's left edge.
    const auto &rs = mviewer::core::RatingStore::instance();
    const std::string ep = path.toStdString();
    const QRect overlayRect = card;
    const int stars = rs.rating(ep);
    if (stars > 0)
    {
        QFont sf = painter->font();
        sf.setPixelSize(15);
        painter->setFont(sf);
        painter->setPen(QColor(255, 215, 0));
        QString starStr;
        starStr.reserve(5);
        for (int s = 0; s < 5; ++s)
            starStr += (s < stars ? QStringLiteral("★") : QStringLiteral("☆"));
        painter->drawText(overlayRect.adjusted(4, 2, -4, -2),
                          Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, starStr);
    }

    static const QColor kColors[7] = {QColor(),
                                      QColor(229, 57, 53),
                                      QColor(251, 140, 0),
                                      QColor(249, 215, 41),
                                      QColor(67, 160, 71),
                                      QColor(30, 136, 229),
                                      QColor(142, 36, 170)};
    const int label = rs.colorLabel(ep);
    if (label > 0)
        painter->fillRect(QRect(overlayRect.left(), overlayRect.top(), 4, overlayRect.height()),
                          kColors[label]);
    if (rs.rejected(ep))
    {
        painter->fillRect(overlayRect, QColor(200, 30, 30, 90));
        QFont rf = painter->font();
        rf.setPixelSize(22);
        rf.setBold(true);
        painter->setFont(rf);
        painter->setPen(QColor(255, 255, 255));
        painter->drawText(overlayRect, Qt::AlignCenter, QStringLiteral("✕"));
    }
    if (rs.picked(ep))
    {
        QFont pf = painter->font();
        pf.setPixelSize(15);
        painter->setFont(pf);
        painter->setPen(QColor(255, 215, 0));
        painter->drawText(overlayRect.adjusted(4, 2, -4, -2),
                          Qt::AlignRight | Qt::AlignTop | Qt::TextSingleLine,
                          QStringLiteral("⚑"));
    }

    if (selected)
    {
        QPen border(accent);
        border.setWidth(2);
        painter->setPen(border);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 4, 4);
    }
    else if (hovered)
    {
        QPen border(option.palette.color(QPalette::Midlight));
        border.setWidth(1);
        painter->setPen(border);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 4, 4);
    }
    painter->restore();
}

QSize ThumbnailPanel::ThumbDelegate::sizeHint(const QStyleOptionViewItem &,
                                              const QModelIndex &) const
{
    switch (m_panel->viewMode())
    {
    case ThumbnailPanel::Thumbnail:
    case ThumbnailPanel::LargeIcon:
        return QSize(thumbSize() + 24, thumbSize() + 62);
    case ThumbnailPanel::SmallIcon:
        return QSize(thumbSize() + 12, thumbSize() + 30);
    case ThumbnailPanel::Filmstrip:
    {
        const int stripH = qMax(thumbSize(), 64) + 18;
        return QSize(stripH, stripH);
    }
    case ThumbnailPanel::Compact:
    {
        const int compactS = qMax(thumbSize() / 3, 32);
        return QSize(compactS + 4, compactS + 14);
    }
    default:
        return QSize(thumbSize() + 16, thumbSize() + 34);
    }
}

int ThumbnailPanel::ThumbDelegate::thumbSize() const
{
    return m_panel->thumbSize();
}

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
    const QString cachedSuffix = fileSuffixFromPath(path);

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
    const DetailLayout L = detailLayout(option.rect.adjusted(0, 2, 0, -2));

    // Column 1: small thumbnail (48×48)
    const QRect thumbR(L.thumb.x(), r.y() + (r.height() - 48) / 2, 48, 48);
    QPixmap pm = m_panel->thumbReady(path);
    if (!pm.isNull())
    {
        const QPixmap scaled =
            pm.scaled(thumbR.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter->drawPixmap(thumbR.x() + (48 - scaled.width()) / 2,
                            thumbR.y() + (48 - scaled.height()) / 2, scaled);
    }
    else
    {
        if (m_panel->thumbFailed(path))
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

    const QColor textColor = sel ? option.palette.color(QPalette::HighlightedText)
                                 : option.palette.color(QPalette::Text);
    painter->setPen(textColor);

    QFont nameFont = painter->font();
    nameFont.setBold(true);
    painter->setFont(nameFont);

    // Column 2: filename
    painter->drawText(L.name, Qt::AlignVCenter | Qt::TextSingleLine, name);

    // Column 3: resolution — resolve by source path because index.row() belongs
    // to the filtered model and cannot index m_allEntries.
    painter->setFont(option.font);
    QString resStr = "-";
    if (entry && entry->width > 0 && entry->height > 0)
        resStr = QString("%1×%2").arg(entry->width).arg(entry->height);
    painter->drawText(L.res, Qt::AlignVCenter | Qt::TextSingleLine, resStr);

    // Column 4: file size (scan-cached; "-" when the entry is unknown).
    painter->drawText(L.size, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedSize >= 0 ? formatFileSize(cachedSize) : QStringLiteral("-"));

    // Column 5: modified date (scan-cached).
    painter->drawText(L.date, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedMtime.isValid()
                          ? cachedMtime.toString("yyyy-MM-dd hh:mm:ss")
                          : QStringLiteral("-"));

    // Column 6: format (lexical suffix, never a filesystem query).
    painter->drawText(L.fmt, Qt::AlignVCenter | Qt::TextSingleLine,
                      cachedSuffix.isEmpty() ? QStringLiteral("IMAGE") : cachedSuffix.toUpper());

    // Column 7: rating (P0-4). Draw filled/empty stars from the RatingStore.
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
        painter->drawText(L.rate, Qt::AlignVCenter | Qt::TextSingleLine, starStr);
        painter->restore();
    }
    else
    {
        painter->drawText(L.rate, Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("-"));
    }

    // Column 8: color label (P0-4). Draw a small colored chip + name.
    const int label = rs.colorLabel(ep);
    const QRect labelR = L.label;
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
        QRect chipR(labelR.x(), labelR.y() + (labelR.height() - cs) / 2, cs, cs);
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(chip);
        painter->drawRoundedRect(chipR, 3, 3);
        painter->restore();
        QRect chipTextR(labelR.x() + cs + 6, labelR.y(), labelR.width() - cs - 6, labelR.height());
        painter->drawText(chipTextR, Qt::AlignVCenter | Qt::TextSingleLine,
                          QString::fromUtf8(kLabelNames[label]));
    }
    else
    {
        painter->drawText(labelR, Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("-"));
    }

    // Columns 9-11: EXIF (camera / lens / ISO) — P0 #① professional columns for
    // image algorithm engineers. Backed by the metadata index (ensureMetaIndex).
    const QString cam = m_panel->metaCameraForPath(path).trimmed();
    const QString lens = m_panel->metaLensForPath(path).trimmed();
    const int iso = m_panel->metaIsoForPath(path);
    painter->setFont(option.font);
    painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                        : option.palette.color(QPalette::Text));
    painter->drawText(L.camera, Qt::AlignVCenter | Qt::TextSingleLine,
                      cam.isEmpty() ? QStringLiteral("-") : cam);
    painter->drawText(L.lens, Qt::AlignVCenter | Qt::TextSingleLine,
                      lens.isEmpty() ? QStringLiteral("-") : lens);
    painter->drawText(L.iso, Qt::AlignVCenter | Qt::TextSingleLine,
                      iso > 0 ? QString("ISO %1").arg(iso) : QStringLiteral("-"));

    painter->restore();
}

QSize ThumbnailPanel::DetailsDelegate::sizeHint(const QStyleOptionViewItem &,
                                                const QModelIndex &) const
{
    // Wide enough to show every column without overlap; the Details view scrolls
    // horizontally when the viewport is narrower (see setViewMode Details branch).
    const int w = qMax(detailTotalWidth(), m_panel->viewport()->width());
    return QSize(w, kDetailsItemHeight);
}

// ---- ListDelegate ----------------------------------------------------------
void ThumbnailPanel::ListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    if (!index.isValid())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const QStringList &paths = m_panel->pathList();
    if (index.row() < 0 || index.row() >= paths.size())
        return;
    const QString path = paths.at(index.row());
    // M46: lexical fileName only — no QFileInfo on the paint path.
    const QString name = fileNameFromPath(path);

    const bool sel = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    QColor bg = sel ? option.palette.color(QPalette::Highlight)
                    : (hover ? option.palette.color(QPalette::Midlight)
                             : option.palette.color(index.row() & 1 ? QPalette::AlternateBase
                                                                    : QPalette::Base));
    painter->fillRect(option.rect, bg);

    const QRect r = option.rect.adjusted(4, 0, -4, 0);
    const int icon = 16;
    const QRect iconR(r.x(), r.y() + (r.height() - icon) / 2, icon, icon);
    QPixmap pm = m_panel->thumbReady(path);
    if (!pm.isNull())
        painter->drawPixmap(iconR,
                            pm.scaled(icon, icon, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else if (m_panel->thumbFailed(path))
        painter->fillRect(iconR, QColor(200, 200, 200));
    else
        painter->fillRect(iconR, QColor(228, 228, 228));

    const QRect textR(iconR.right() + 6, r.y(), r.right() - iconR.right() - 6, r.height());
    painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                        : option.palette.color(QPalette::Text));
    painter->drawText(textR, Qt::AlignVCenter | Qt::TextSingleLine, name);
}

QSize ThumbnailPanel::ListDelegate::sizeHint(const QStyleOptionViewItem &,
                                             const QModelIndex &) const
{
    // Fixed width so items wrap into columns; single compact row height.
    return QSize(kListItemWidth, kListItemHeight);
}
