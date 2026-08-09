// M20 P0#3: shared private header for the ThumbnailPanel implementation,
// which is split across several translation units by responsibility:
//   thumbnailpanel.cpp           core view / model / directory scan
//   thumbnailpanel_filters.cpp   filtering / sorting / metadata index
//   thumbnailpanel_fileops.cpp   rename / trash / copy / move / batch export
//   thumbnailpanel_delegates.cpp thumb / details / list item delegates
//   thumbnailpanel_viewmode.cpp  view-mode configuration
//   thumbnailpanel_selection.cpp selection / path navigation
//   thumbnailpanel_pipeline.cpp  visible-range scheduling / thumbnail delivery
// Only ThumbnailPanel TUs may include this header.
#pragma once

#include "thumbnailpanel.h"

#include "core/RatingStore.h"
#include "core/analysis/ExportReport.h"
#include "core/analyzer/Analyzer.h"
#include "core/command/CommandStack.h"
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileMoveCommand.h"
#include "core/command/FileRenameCommand.h"
#include "core/image/Decoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/RawMetadata.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "domain/Image.h"
#include "thumbnailcache.h"

#include <memory>

#include <QPointer>
#include <algorithm>
#include <limits>
#include <unordered_map>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QScrollBar>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <thread>

inline bool isImageSuffix(const QString &suffix)
{
    static const QStringList exts = {"bmp", "gif", "jpg", "jpeg", "png", "tif", "tiff", "webp"};
    return exts.contains(suffix);
}

inline std::vector<std::string> toStdPaths(const QStringList &in)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(in.size()));
    for (const QString &s : in)
        out.push_back(s.toStdString());
    return out;
}

// P0-4: shared column geometry for the Details view so the delegate cells and
// the header row stay perfectly aligned.
inline constexpr int kDetailsHeaderH = 24;
inline constexpr int kListItemWidth = 220;
inline constexpr int kListItemHeight = 22;
inline constexpr int kDetailsItemHeight = 52;
struct DetailLayout
{
    QRect thumb, name, res, size, date, fmt, rate, label, camera, lens, iso;
};
inline DetailLayout detailLayout(const QRect &row)
{
    const QRect r = row.adjusted(4, 0, -4, 0);
    const int thumbColW = 60, resW = 120, sizeW = 100, dateW = 160, fmtW = 80, rateW = 90,
              labelW = 90, camW = 160, lensW = 240, isoW = 80, gap = 12;
    const int fixed =
        thumbColW + resW + sizeW + dateW + fmtW + rateW + labelW + camW + lensW + isoW + gap * 10;
    const int nameW = qMax(140, r.width() - fixed);
    DetailLayout L;
    int x = r.x();
    L.thumb = QRect(x, r.y(), thumbColW, r.height());
    x += thumbColW;
    L.name = QRect(x, r.y(), nameW, r.height());
    x += nameW + gap;
    L.res = QRect(x, r.y(), resW, r.height());
    x += resW + gap;
    L.size = QRect(x, r.y(), sizeW, r.height());
    x += sizeW + gap;
    L.date = QRect(x, r.y(), dateW, r.height());
    x += dateW + gap;
    L.fmt = QRect(x, r.y(), fmtW, r.height());
    x += fmtW + gap;
    L.rate = QRect(x, r.y(), rateW, r.height());
    x += rateW + gap;
    L.label = QRect(x, r.y(), labelW, r.height());
    x += labelW + gap;
    L.camera = QRect(x, r.y(), camW, r.height());
    x += camW + gap;
    L.lens = QRect(x, r.y(), lensW, r.height());
    x += lensW + gap;
    L.iso = QRect(x, r.y(), isoW, r.height());
    return L;
}

// P0 #①: minimum content width for the Details row (every column + gaps + the
// 140px minimum name column). Used by the delegate sizeHint so the row is always
// wide enough to show all columns without overlap; the Details view scrolls
// horizontally if the viewport is narrower (see setViewMode Details branch).
inline int detailTotalWidth()
{
    const int thumbColW = 60, resW = 120, sizeW = 100, dateW = 160, fmtW = 80, rateW = 90,
              labelW = 90, camW = 160, lensW = 240, isoW = 80, gap = 12;
    const int fixed =
        thumbColW + resW + sizeW + dateW + fmtW + rateW + labelW + camW + lensW + isoW + gap * 10;
    return fixed + 140;
}

// P0#3: column-title strip painted above the Details list. Lives here (not in
// thumbnailpanel.cpp) so it shares the DetailLayout geometry with the delegate
// cells and the header titles stay perfectly aligned with the values below.
class DetailsHeader : public QWidget
{
  public:
    explicit DetailsHeader(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect full(0, 0, width(), height());
        p.fillRect(full, palette().color(QPalette::Button));
        p.setPen(palette().color(QPalette::Mid));
        p.drawLine(full.bottomLeft(), full.bottomRight());

        const DetailLayout L = detailLayout(full);
        p.setPen(palette().color(QPalette::ButtonText));
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        const int flags = Qt::AlignVCenter | Qt::TextSingleLine;
        p.drawText(L.name, flags, QStringLiteral("名称"));
        p.drawText(L.res, flags, QStringLiteral("分辨率"));
        p.drawText(L.size, flags, QStringLiteral("大小"));
        p.drawText(L.date, flags, QStringLiteral("修改日期"));
        p.drawText(L.fmt, flags, QStringLiteral("格式"));
        p.drawText(L.rate, flags, QStringLiteral("评分"));
        p.drawText(L.label, flags, QStringLiteral("标签"));
        p.drawText(L.camera, flags, QStringLiteral("相机"));
        p.drawText(L.lens, flags, QStringLiteral("镜头"));
        p.drawText(L.iso, flags, QStringLiteral("ISO"));
    }
};
