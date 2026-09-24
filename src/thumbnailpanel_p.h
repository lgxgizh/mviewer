// M20 P0#3: shared private header for the ThumbnailPanel implementation,
// which is split across several translation units by responsibility. Line counts
// are physical lines and the budget is enforced by scripts/complexity_gate.ps1
// (thumbnailpanel.cpp fails above 800; every thumbnailpanel_*.cpp warns above
// 800):
//   thumbnailpanel.cpp             core view / model / directory scan      730
//   thumbnailpanel_async.cpp       scan + dimension-probe workers          482
//   thumbnailpanel_delegates.cpp   thumb / details / list item delegates   739
//   thumbnailpanel_fileops.cpp     rename / trash / copy / move / batch export 765
//   thumbnailpanel_filters.cpp     filtering / sorting / metadata index    722
//   thumbnailpanel_live.cpp        incremental live-folder delta apply     492
//   thumbnailpanel_pipeline.cpp    visible-range scheduling / delivery     219
//   thumbnailpanel_selection.cpp   selection / path navigation             274
//   thumbnailpanel_viewmode.cpp    view-mode configuration + Details header 362
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
#include "core/command/FileSystemAdapter.h"
#include "core/export/ExportJob.h"
#include "core/image/Decoder.h"
#include "core/image/FrameSequence.h"
#include "core/image/ImageFormats.h"
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
#include <QCursor>
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
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QProcess>
#include <QProgressDialog>
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

struct ScrollPos
{
    int v = 0;
    int h = 0;
};

inline ScrollPos captureScroll(const QAbstractItemView *view)
{
    ScrollPos s;
    if (auto *vb = view ? view->verticalScrollBar() : nullptr)
        s.v = vb->value();
    if (auto *hb = view ? view->horizontalScrollBar() : nullptr)
        s.h = hb->value();
    return s;
}

inline void restoreScroll(QAbstractItemView *view, const ScrollPos &s)
{
    if (auto *vb = view ? view->verticalScrollBar() : nullptr)
    {
        if (vb->maximum() >= s.v)
            vb->setValue(s.v);
    }
    if (auto *hb = view ? view->horizontalScrollBar() : nullptr)
    {
        if (hb->maximum() >= s.h)
            hb->setValue(s.h);
    }
}

// M25: the shipped-format SSOT (decoder registry) decides what is an image —
// RAW/WebP/GIF are listed exactly like the historical six formats.
inline bool isImageSuffix(const QString &suffix)
{
    if (suffix.isEmpty())
        return false;
    return mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString());
}

inline std::vector<std::string> toStdPaths(const QStringList &in)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(in.size()));
    for (const QString &s : in)
        out.push_back(s.toUtf8().toStdString());
    return out;
}

// P0-4: shared column geometry for the Details view so the delegate cells and
// the header row stay perfectly aligned. Column widths are user-resizable via
// DetailsHeader (Explorer-style drag on separators); name still absorbs any
// leftover width when the row is wider than the preferred total.
inline constexpr int kDetailsHeaderH = 24;
inline constexpr int kListItemWidth = 220;
inline constexpr int kListItemHeight = 22;
inline constexpr int kDetailsItemHeight = 52;
inline constexpr int kDetailGap = 12;
inline constexpr int kDetailSidePad = 4;
inline constexpr int kDetailSepHitSlop = 4;

inline constexpr int kDetailDefaultW[ThumbnailPanel::DetailColCount] = {60, 140, 120, 100, 160, 80,
                                                                        90, 90,  160, 240, 80};
inline constexpr int kDetailMinW[ThumbnailPanel::DetailColCount] = {48, 80, 64, 56, 100, 48,
                                                                    56, 56, 80, 80, 40};

struct DetailLayout
{
    QRect thumb, name, res, size, date, fmt, rate, label, camera, lens, iso;
};

// Gaps sit between name..iso (9 gaps). Thumb and name are adjacent.
inline int detailGapsWidth()
{
    return kDetailGap * (ThumbnailPanel::DetailColCount - 2);
}

inline int detailTotalWidth(const int *colW)
{
    int sum = 2 * kDetailSidePad + detailGapsWidth();
    for (int i = 0; i < ThumbnailPanel::DetailColCount; ++i)
        sum += colW[i];
    return sum;
}

inline DetailLayout detailLayout(const QRect &row, const int *colW)
{
    const QRect r = row.adjusted(kDetailSidePad, 0, -kDetailSidePad, 0);
    int fixedWithoutName = detailGapsWidth();
    for (int i = 0; i < ThumbnailPanel::DetailColCount; ++i)
    {
        if (i == ThumbnailPanel::DetailColName)
            continue;
        fixedWithoutName += colW[i];
    }
    const int nameW = qMax(colW[ThumbnailPanel::DetailColName], r.width() - fixedWithoutName);
    DetailLayout L;
    int x = r.x();
    L.thumb = QRect(x, r.y(), colW[ThumbnailPanel::DetailColThumb], r.height());
    x += colW[ThumbnailPanel::DetailColThumb];
    L.name = QRect(x, r.y(), nameW, r.height());
    x += nameW + kDetailGap;
    L.res = QRect(x, r.y(), colW[ThumbnailPanel::DetailColRes], r.height());
    x += colW[ThumbnailPanel::DetailColRes] + kDetailGap;
    L.size = QRect(x, r.y(), colW[ThumbnailPanel::DetailColSize], r.height());
    x += colW[ThumbnailPanel::DetailColSize] + kDetailGap;
    L.date = QRect(x, r.y(), colW[ThumbnailPanel::DetailColDate], r.height());
    x += colW[ThumbnailPanel::DetailColDate] + kDetailGap;
    L.fmt = QRect(x, r.y(), colW[ThumbnailPanel::DetailColFmt], r.height());
    x += colW[ThumbnailPanel::DetailColFmt] + kDetailGap;
    L.rate = QRect(x, r.y(), colW[ThumbnailPanel::DetailColRate], r.height());
    x += colW[ThumbnailPanel::DetailColRate] + kDetailGap;
    L.label = QRect(x, r.y(), colW[ThumbnailPanel::DetailColLabel], r.height());
    x += colW[ThumbnailPanel::DetailColLabel] + kDetailGap;
    L.camera = QRect(x, r.y(), colW[ThumbnailPanel::DetailColCamera], r.height());
    x += colW[ThumbnailPanel::DetailColCamera] + kDetailGap;
    L.lens = QRect(x, r.y(), colW[ThumbnailPanel::DetailColLens], r.height());
    x += colW[ThumbnailPanel::DetailColLens] + kDetailGap;
    L.iso = QRect(x, r.y(), colW[ThumbnailPanel::DetailColIso], r.height());
    return L;
}

// Interactive column-title strip for Details. Mouse drag on separators resizes
// columns; paint shares detailLayout() with DetailsDelegate so header/cells stay
// aligned (including horizontal scroll offset).
class DetailsHeader : public QWidget
{
  public:
    explicit DetailsHeader(ThumbnailPanel *panel);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

  private:
    int separatorAt(int x) const;
    int columnAt(int x) const;
    QRect contentRect() const;

    ThumbnailPanel *m_panel = nullptr;
    int m_dragCol = -1;
    int m_pressedCol = -1;
    int m_dragOriginX = 0;
    int m_dragOriginW = 0;
};
