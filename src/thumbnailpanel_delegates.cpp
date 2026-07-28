// ThumbnailPanel item delegates: thumbnail grid, details row, list row (M20 P0#3).
#include "thumbnailpanel_p.h"

void ThumbnailPanel::ThumbDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                          const QModelIndex &index) const
{
    const QStringList &paths = m_panel->pathList();
    if (index.row() < 0 || index.row() >= paths.size())
        return;
    const QString path = paths.at(index.row());
    const QString name = index.data(Qt::DisplayRole).toString();

    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.color(QPalette::Highlight));
    else if (option.state & QStyle::State_MouseOver)
        painter->fillRect(option.rect, option.palette.color(QPalette::Midlight));
    else
        painter->fillRect(option.rect, option.palette.color(QPalette::Base));

    const int s = thumbSize();
    const QRect thumbRect(option.rect.x() + (option.rect.width() - s) / 2, option.rect.y() + 6, s,
                          s);
    const QPixmap pm = m_panel->thumbReady(path);
    if (!pm.isNull())
    {
        const QPixmap scaled =
            pm.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter->drawPixmap(thumbRect.x() + (thumbRect.width() - scaled.width()) / 2,
                            thumbRect.y() + (thumbRect.height() - scaled.height()) / 2, scaled);
    }
    else
    {
        // Distinguish "failed to decode" (darker grey + hint text) from
        // "still loading" (light grey, no text).
        if (m_panel->thumbFailed(path))
        {
            painter->fillRect(thumbRect, QColor(200, 180, 180));
            painter->setPen(QColor(150, 100, 100));
            QFont f = painter->font();
            f.setPointSize(qMax(7, f.pointSize() - 1));
            painter->setFont(f);
            const QString elidedName =
                painter->fontMetrics().elidedText(name, Qt::ElideMiddle, thumbRect.width() - 8);
            painter->drawText(thumbRect, Qt::AlignCenter, "无法加载\n" + elidedName);
        }
        else
        {
            painter->fillRect(thumbRect, QColor(228, 228, 228));
        }
    }

    QRect textRect(option.rect.x() + 4, thumbRect.bottom() + 4, option.rect.width() - 8,
                   option.rect.height() - thumbRect.height() - 8);
    painter->setPen(option.state & QStyle::State_Selected
                        ? option.palette.color(QPalette::HighlightedText)
                        : option.palette.color(QPalette::Text));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::ElideRight, name);

    // P1: rating stars overlay (top-left corner).
    const int stars = mviewer::core::RatingStore::instance().rating(path.toStdString());
    if (stars > 0)
    {
        QFont sf = painter->font();
        sf.setPixelSize(15);
        painter->setFont(sf);
        painter->setPen(QColor(255, 215, 0));
        QString starStr;
        starStr.reserve(5);
        for (int s = 0; s < 5; ++s)
            starStr += (s < stars ? "★" : "☆");
        painter->drawText(option.rect.x() + 4, option.rect.y() + 16, starStr);
    }

    // P3 tail: color label bar, reject overlay and pick marker.
    const auto &rs = mviewer::core::RatingStore::instance();
    const std::string ep = path.toStdString();
    const int label = rs.colorLabel(ep);
    if (label > 0)
    {
        static const QColor kColors[7] = {QColor(),
                                          QColor(229, 57, 53),
                                          QColor(251, 140, 0),
                                          QColor(249, 215, 41),
                                          QColor(67, 160, 71),
                                          QColor(30, 136, 229),
                                          QColor(142, 36, 170)};
        painter->fillRect(option.rect.x(), option.rect.y(), 4, option.rect.height(),
                          kColors[label]);
    }
    if (rs.rejected(ep))
    {
        painter->fillRect(option.rect, QColor(200, 30, 30, 90));
        QFont rf = painter->font();
        rf.setPixelSize(22);
        rf.setBold(true);
        painter->setFont(rf);
        painter->setPen(QColor(255, 255, 255));
        painter->drawText(option.rect, Qt::AlignCenter, "✕");
    }
    if (rs.picked(ep))
    {
        QFont pf = painter->font();
        pf.setPixelSize(15);
        painter->setFont(pf);
        painter->setPen(QColor(255, 215, 0));
        painter->drawText(option.rect.x() + option.rect.width() - 18, option.rect.y() + 16, "⚑");
    }
}

QSize ThumbnailPanel::ThumbDelegate::sizeHint(const QStyleOptionViewItem &,
                                              const QModelIndex &) const
{
    return QSize(thumbSize() + 16, thumbSize() + 34);
}

int ThumbnailPanel::ThumbDelegate::thumbSize() const
{
    return m_panel->thumbSize();
}

// ---- DetailsDelegate ---------------------------------------------------------

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

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
    const QFileInfo fi(path);

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

    // Column 3: resolution — read from pre-populated Entry data. Look the path up
    // via rowForPath(): index.row() is the *filtered* model row, but m_allEntries
    // is the unfiltered source, so indexing it by row shows the wrong image after
    // a filter/sort (H1).
    painter->setFont(option.font);
    QString resStr = "-";
    const int allRow = m_panel->rowForPath(path);
    const QList<Entry> &all = m_panel->entries();
    if (allRow >= 0 && allRow < all.size())
    {
        const auto &e = all.at(allRow);
        if (e.width > 0 && e.height > 0)
            resStr = QString("%1×%2").arg(e.width).arg(e.height);
    }
    painter->drawText(L.res, Qt::AlignVCenter | Qt::TextSingleLine, resStr);

    // Column 4: file size
    painter->drawText(L.size, Qt::AlignVCenter | Qt::TextSingleLine, formatFileSize(fi.size()));

    // Column 5: modified date
    painter->drawText(L.date, Qt::AlignVCenter | Qt::TextSingleLine,
                      fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));

    // Column 6: format
    painter->drawText(L.fmt, Qt::AlignVCenter | Qt::TextSingleLine, fi.suffix().toUpper());

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
    return QSize(w, 52);
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
    const QString name = QFileInfo(path).fileName();

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
    // Fixed 220px width so items wrap into columns; single 22px row height.
    return QSize(220, 22);
}
