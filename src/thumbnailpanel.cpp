#include "thumbnailpanel.h"

#include "core/RatingStore.h"
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
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
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

namespace
{
bool isImageSuffix(const QString &suffix)
{
    static const QStringList exts = {"bmp", "gif", "jpg", "jpeg", "png", "tif", "tiff", "webp"};
    return exts.contains(suffix);
}

std::vector<std::string> toStdPaths(const QStringList &in)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(in.size()));
    for (const QString &s : in)
        out.push_back(s.toStdString());
    return out;
}

// P0-4: shared column geometry for the Details view so the delegate cells and
// the header row stay perfectly aligned.
constexpr int kDetailsHeaderH = 24;
struct DetailLayout
{
    QRect thumb, name, res, size, date, fmt, rate, label;
};
DetailLayout detailLayout(const QRect &row)
{
    const QRect r = row.adjusted(4, 0, -4, 0);
    const int thumbColW = 60, resW = 120, sizeW = 100, dateW = 160, fmtW = 80, rateW = 90,
              labelW = 90, gap = 12;
    const int fixed = thumbColW + resW + sizeW + dateW + fmtW + rateW + labelW + gap * 6;
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
    return L;
}

// P0-4: header strip painted above the Details list. Uses the same column
// geometry as the delegate so titles align with the values below.
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
    }
};
} // namespace

// ---- ThumbnailPanel ---------------------------------------------------------

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QListView(parent)
{
    // QListView:: prefix disambiguates from our own ThumbnailPanel::setViewMode().
    QListView::setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setUniformItemSizes(true); // all cells identical -> cheap layout for huge lists
    setSpacing(6);
    setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAttribute(Qt::WA_Hover);
    viewport()->setMouseTracking(true);

    m_model = new QStringListModel(this);
    setModel(m_model);

    m_delegate = new ThumbDelegate(this, this);
    setItemDelegate(m_delegate);

    m_compareBtn = new QPushButton("比较选中", this);
    m_compareBtn->setVisible(false);
    connect(m_compareBtn, &QPushButton::clicked, this, &ThumbnailPanel::onCompareClicked);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &ThumbnailPanel::onSelectionChanged);

    // Drive thumbnail decode priority from the viewport (P0 #②).
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);

    // Restore path-based navigation signals (used by MainWindow to open images
    // and refresh the metadata panel) from the view's built-in index signals.
    connect(this, &QAbstractItemView::clicked, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemClicked(m_paths.value(idx.row()));
            });
    connect(this, &QAbstractItemView::doubleClicked, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemDoubleClicked(m_paths.value(idx.row()));
            });
    // Keyboard parity: Enter opens the viewer (same as double-click), and
    // moving the current item with the arrow keys drives the shared selection
    // model so the preview/status bar follow without a mouse. The central
    // SelectionModel no-ops on a same-path set, so selectPath() → currentChanged
    // → itemClicked cannot loop.
    connect(this, &QAbstractItemView::activated, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemDoubleClicked(m_paths.value(idx.row()));
            });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &)
            {
                if (current.isValid())
                    emit itemClicked(m_paths.value(current.row()));
            });

    // Wire the shared pipeline ONCE. The decode step is disk-cache-aware (so a
    // previously visited folder loads instantly without re-decoding), and the
    // result callback lands the QPixmap into our ready map + repaints the cell.
    // The pipeline is a singleton used only by the gallery, so configuring it
    // here is safe.
    m_alive = std::make_shared<std::atomic<bool>>(true);
    if (!m_pipelineWired)
    {
        m_pipelineWired = true;
        ThumbnailPipeline::instance().thumbSize = m_thumbSize;
        ThumbnailPipeline::instance().setDecodeFn(
            [this](const std::string &p, int /*size*/)
            {
                QString qp = QString::fromStdString(p);
                QPixmap cached;
                if (ThumbnailCache::instance().get(qp, cached))
                    return mvcore::fromQImage(cached.toImage());
                return Decoder::decodeScaled(p, m_thumbSize);
            });
        auto alive = m_alive;
        ThumbnailPipeline::instance().setResultFn(
            [this, alive](const std::string &p, const ImageData &img)
            {
                if (!alive->load())
                    return; // panel destroyed; ignore late callback
                QImage q = mvcore::toQImage(img);
                if (q.isNull())
                {
                    // Record the failure so the delegate can paint a distinct
                    // "无法加载" placeholder instead of the generic loading grey.
                    {
                        QMutexLocker l(&m_thumbMtx);
                        m_thumbFailed.insert(QString::fromStdString(p));
                        m_thumbPending.remove(QString::fromStdString(p));
                    }
                    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
                    return;
                }
                const int s = m_thumbSize;
                QPixmap pm(s, s);
                pm.fill(Qt::transparent);
                QPixmap scaled = QPixmap::fromImage(q).scaled(s, s, Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation);
                QPainter painter(&pm);
                painter.drawPixmap((s - scaled.width()) / 2, (s - scaled.height()) / 2, scaled);
                painter.end();
                QString qp = QString::fromStdString(p);
                {
                    QMutexLocker lk(&m_thumbMtx);
                    m_thumbReady[qp] = pm;
                    m_thumbPending.remove(qp);
                }
                ThumbnailCache::instance().put(qp, pm);
                QMetaObject::invokeMethod(this, "onThumbReady", Qt::QueuedConnection,
                                          Q_ARG(QString, qp));
            });
    }
}

ThumbnailPanel::~ThumbnailPanel()
{
    if (m_alive)
        *m_alive = false;
    // Detach from the shared pipeline so its worker thread can't call back into
    // a destroyed panel (and restore the default decode for the next panel).
    ThumbnailPipeline::instance().setResultFn([](const std::string &, const ImageData &) {});
    ThumbnailPipeline::instance().setDecodeFn([](const std::string &p, int size)
                                              { return Decoder::decodeScaled(p, size); });
}

void ThumbnailPanel::setDirectory(const QString &path)
{
    // Enumerating a large folder can take a moment; give the user immediate
    // busy feedback instead of a silently frozen cursor.
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const auto cursorGuard = qScopeGuard([] { QApplication::restoreOverrideCursor(); });

    m_currentDir = path;
    m_filterText.clear();
    m_filterRecursive = false;

    // P0-1 (perf): a new directory generation. Any in-flight background
    // dimension resolve from the previous folder is invalidated by the bump.
    ++m_dirGen;
    m_dimsResolved = false;

    QList<Entry> entries;
    QDir dir(path);
    if (dir.exists())
    {
        const QFileInfoList list = sortedEntries(dir, m_sortMode, m_sortAscending);
        for (const QFileInfo &fi : list)
        {
            // A-2.3: skip files whose extension is not in the type filter.
            if (!m_typeFilter.isEmpty())
            {
                bool keep = false;
                for (const QString &ext : m_typeFilter.split(','))
                {
                    if (ext == fi.suffix().toLower())
                    {
                        keep = true;
                        break;
                    }
                }
                if (!keep)
                    continue;
            }
            // P0-1 (perf): do NOT read pixel dimensions here. Even
            // QImageReader::size() opens the file and reads its header, so for a
            // 10k-image folder this blocked the UI thread for seconds on every
            // folder switch. Dimensions are only needed by the Details view, so
            // they are resolved lazily in the background (see ensureDimensions).
            entries.append(
                {fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0, fi.lastModified()});
        }
    }
    m_allEntries = entries;
    m_metaIndex.clear();
    applyFilter();

    // Only pay the header-read cost when the Details view actually shows the
    // resolution column.
    if (m_viewMode == Details)
        ensureDimensions();
}

void ThumbnailPanel::refresh()
{
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::ensureDimensions()
{
    if (m_dimsResolved || m_allEntries.isEmpty())
        return;
    m_dimsResolved = true; // mark up-front so we launch the worker only once

    const int gen = m_dirGen;
    QStringList paths;
    paths.reserve(m_allEntries.size());
    for (const Entry &e : m_allEntries)
        paths.append(e.path);

    auto alive = m_alive;
    std::thread(
        [this, gen, paths, alive]()
        {
            QVector<QSize> sizes;
            sizes.reserve(paths.size());
            for (const QString &p : paths)
            {
                QImageReader reader(p);
                reader.setAutoTransform(true);
                sizes.append(reader.size());
            }
            QMetaObject::invokeMethod(
                this,
                [this, gen, sizes, alive]()
                {
                    if (!alive || !*alive)
                        return;
                    if (gen != m_dirGen) // folder changed while resolving
                        return;
                    for (int i = 0; i < sizes.size() && i < m_allEntries.size(); ++i)
                    {
                        m_allEntries[i].width = sizes[i].width();
                        m_allEntries[i].height = sizes[i].height();
                    }
                    viewport()->update();
                },
                Qt::QueuedConnection);
        })
        .detach();
}

void ThumbnailPanel::setSortMode(SortMode mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setSortAscending(bool ascending)
{
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setTypeFilter(const QString &types)
{
    if (m_typeFilter == types)
        return;
    m_typeFilter = types;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;

    // Large folders (1000+ images) scroll noticeably smoother with a larger
    // layout batch and a persistent scrollbar (no layout jump when items appear).
    setBatchSize(256);

    // P0-4: only the Details view reserves a top margin for the column header.
    // Reset here so switching away from Details restores the full viewport.
    if (mode != Details)
    {
        setViewportMargins(0, 0, 0, 0);
        if (m_detailsHeader)
            m_detailsHeader->hide();
    }

    // P0-2: Large/Small icon modes are thumbnail grids with fixed sizes.
    if (mode == LargeIcon)
    {
        setThumbSize(240);
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
        setSpacing(8);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
        return;
    }
    if (mode == SmallIcon)
    {
        setThumbSize(64);
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 12, m_thumbSize + 30));
        setSpacing(4);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
        return;
    }

    if (mode == Details)
    {
        QListView::setViewMode(QListView::ListMode);
        setWrapping(false);
        setUniformItemSizes(false);
        setGridSize(QSize());
        setIconSize(QSize(48, 48));
        setSpacing(0);
        // Swap to the details delegate.
        if (m_delegate)
            delete m_delegate;
        m_delegate = new DetailsDelegate(this, this);
        setItemDelegate(m_delegate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Reserve space for and show the column header.
        if (!m_detailsHeader)
            m_detailsHeader = new DetailsHeader(this);
        setViewportMargins(0, kDetailsHeaderH, 0, 0);
        m_detailsHeader->show();
        positionDetailsHeader();
        // Details is the only view that shows the resolution column, so this is
        // where we pay the (deferred, background) header-read cost.
        ensureDimensions();
    }
    else if (mode == Filmstrip)
    {
        // M15: horizontal single-row strip, no wrapping
        QListView::setViewMode(QListView::IconMode);
        setWrapping(false);
        setUniformItemSizes(true);
        const int stripH = qMax(m_thumbSize, 64) + 18;
        setGridSize(QSize(stripH, stripH));
        setSpacing(4);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setResizeMode(QListView::Fixed);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
    else if (mode == Compact)
    {
        // M15: dense grid, minimised padding
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        const int compactS = qMax(m_thumbSize / 3, 32);
        setGridSize(QSize(compactS + 4, compactS + 14));
        setSpacing(2);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
    else
    {
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
        setSpacing(6);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
}

void ThumbnailPanel::setThumbSize(int size)
{
    size = qBound(kMinThumbSize, size, kMaxThumbSize);
    if (size == m_thumbSize)
        return;
    m_thumbSize = size;
    // Update pipeline to generate thumbnails at the new size.
    ThumbnailPipeline::instance().thumbSize = size;
    // Directly update gridSize instead of calling setViewMode(m_viewMode),
    // because setViewMode early-returns when the mode hasn't changed.
    if (m_viewMode == ViewMode::Compact)
        setGridSize(QSize(m_thumbSize + 16, (m_thumbSize + 34) / 3));
    else
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
    viewport()->update();
}

void ThumbnailPanel::setFilter(const QString &text, bool recursive)
{
    m_filterText = text;
    m_filterRecursive = recursive;
    applyFilter();
}

void ThumbnailPanel::setMetaSearch(bool on)
{
    m_metaSearch = on;
    if (on)
        ensureMetaIndex();
    applyFilter();
}

void ThumbnailPanel::setRatingFilter(int stars)
{
    m_ratingFilter = qBound(0, stars, 5);
    applyFilter();
}

void ThumbnailPanel::setLabelFilter(int label)
{
    m_labelFilter = qBound(0, label, 6);
    applyFilter();
}

void ThumbnailPanel::setRejectFilter(bool on)
{
    m_rejectFilter = on;
    applyFilter();
}

void ThumbnailPanel::setPickFilter(bool on)
{
    m_pickFilter = on;
    applyFilter();
}

void ThumbnailPanel::setRecentFilter(bool on)
{
    m_recentFilter = on;
    applyFilter();
}

void ThumbnailPanel::clearFlagFilters()
{
    m_labelFilter = 0;
    m_rejectFilter = false;
    m_pickFilter = false;
    m_recentFilter = false;
    applyFilter();
}

void ThumbnailPanel::invalidateRatings()
{
    viewport()->update();
}

void ThumbnailPanel::ensureMetaIndex()
{
    if (!m_metaIndex.isEmpty())
        return;
    for (const Entry &e : m_allEntries)
    {
        const mviewer::domain::ImageMetadata meta =
            mviewer::core::MetadataReader::read(e.path.toStdString());
        const mviewer::core::RawMetadata rm = mviewer::core::parseRawMetadata(e.path.toStdString());
        QStringList parts;
        parts << QString::fromStdString(meta.fileName) << QString::fromStdString(meta.filePath)
              << QString::fromStdString(meta.format);
        for (const auto &[k, v] : meta.textKeys)
        {
            parts << QString::fromStdString(k) << QString::fromStdString(v);
        }
        parts << QString::fromStdString(rm.make) << QString::fromStdString(rm.model)
              << QString::fromStdString(rm.lens);
        if (rm.iso > 0)
            parts << QString::number(rm.iso);
        m_metaIndex.insert(e.path, parts.join(' ').toLower());
    }
}

void ThumbnailPanel::applyFilter()
{
    const QString t = m_filterText.trimmed().toLower();
    if (m_metaSearch && !t.isEmpty())
        ensureMetaIndex();

    QList<Entry> src = m_allEntries;

    // Optional recursive subfolder scan (filename search only).
    if (m_filterRecursive && !t.isEmpty() && !m_metaSearch && !m_currentDir.isEmpty())
    {
        QDirIterator it(m_currentDir, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!isImageSuffix(fi.suffix().toLower()))
                continue;
            if (!fi.fileName().toLower().contains(t))
                continue;
            const QString sub = QDir(m_currentDir).relativeFilePath(fi.absolutePath());
            src.append({fi.absoluteFilePath(), fi.fileName() + " [" + sub + "]", fi.size()});
        }
    }

    QList<Entry> out;
    for (const Entry &e : src)
    {
        if (m_ratingFilter > 0 &&
            mviewer::core::RatingStore::instance().rating(e.path.toStdString()) < m_ratingFilter)
            continue;
        const std::string ep = e.path.toStdString();
        auto &rs = mviewer::core::RatingStore::instance();
        if (m_labelFilter > 0 && rs.colorLabel(ep) != m_labelFilter)
            continue;
        if (m_rejectFilter && !rs.rejected(ep))
            continue;
        if (m_pickFilter && !rs.picked(ep))
            continue;
        if (m_recentFilter)
        {
            bool inRecents = false;
            for (const auto &r : rs.recents())
                if (r == ep)
                {
                    inRecents = true;
                    break;
                }
            if (!inRecents)
                continue;
        }
        if (!t.isEmpty())
        {
            if (m_metaSearch)
            {
                if (!m_metaIndex.value(e.path).contains(t))
                    continue;
            }
            else if (!e.name.toLower().contains(t))
                continue;
        }
        out.append(e);
    }
    buildModel(out);
}

void ThumbnailPanel::buildModel(const QList<Entry> &entries)
{
    // Preserve selection and current index across model rebuild (e.g. when
    // sorting changes).  Without this, setStringList() resets the entire
    // selection model and the user's multi-select is silently lost.
    const QStringList prevSelected = selectedPaths();
    const QString prevCurrent = m_paths.isEmpty() ? QString()
                                : (currentIndex().isValid()
                                       ? m_paths.value(currentIndex().row())
                                       : QString());

    m_paths.clear();
    m_rowByPath.clear();
    m_sizeByPath.clear();
    QStringList names;
    names.reserve(entries.size());
    qint64 total = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        m_paths.append(entries.at(i).path);
        m_rowByPath.insert(entries.at(i).path, i);
        m_sizeByPath.insert(entries.at(i).path, entries.at(i).size);
        names.append(entries.at(i).name);
        total += entries.at(i).size;
    }
    m_totalBytes = total;
    m_model->setStringList(names);

    // Restore selection and current index.
    QItemSelection selToRestore;
    for (const QString &p : prevSelected)
    {
        auto it = m_rowByPath.constFind(p);
        if (it != m_rowByPath.constEnd())
            selToRestore.select(m_model->index(it.value(), 0),
                                m_model->index(it.value(), 0));
    }
    if (!selToRestore.isEmpty())
        selectionModel()->select(selToRestore, QItemSelectionModel::ClearAndSelect);
    if (!prevCurrent.isEmpty())
    {
        auto it = m_rowByPath.constFind(prevCurrent);
        if (it != m_rowByPath.constEnd())
            setCurrentIndex(m_model->index(it.value(), 0));
    }

    {
        QMutexLocker lk(&m_thumbMtx);
        m_thumbReady.clear();
        m_thumbPending.clear();
        m_thumbFailed.clear();
    }
    ThumbnailPipeline::instance().setSources(toStdPaths(m_paths));

    emit statsChanged(m_paths.size(), m_totalBytes, 0, 0);
    // Defer priority scheduling until layout/geometry is ready (avoids
    // scheduling the whole directory before the viewport is laid out).
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::updateVisibleRange()
{
    const int n = m_model->rowCount();
    if (n == 0)
        return;
    // Geometry may not be ready yet (panel not shown / zero-size). Wait for
    // showEvent / resizeEvent before scheduling.
    if (viewport()->width() < 10 || viewport()->height() < 10)
        return;

    const QModelIndex firstIdx = indexAt(QPoint(2, 2));
    const QModelIndex lastIdx = indexAt(QPoint(viewport()->width() - 2, viewport()->height() - 2));
    int first = firstIdx.isValid() ? firstIdx.row() : 0;
    int last = lastIdx.isValid() ? lastIdx.row() : n - 1;
    if (last < first)
        last = first;

    // Prime instantly-available disk-cached thumbs for the visible window so a
    // revisited folder paints at once instead of waiting for a decode.
    {
        QMutexLocker lk(&m_thumbMtx);
        for (int r = first; r <= last && r < n; ++r)
        {
            const QString &p = m_paths.at(r);
            if (m_thumbReady.contains(p))
                continue;
            QPixmap pm;
            if (ThumbnailCache::instance().get(p, pm))
            {
                m_thumbReady.insert(p, pm);
                QPointer<ThumbnailPanel> guard(this);
                QMetaObject::invokeMethod(
                    this,
                    [guard, p]()
                    {
                        if (!guard)
                            return;
                        guard->onThumbReady(p);
                    },
                    Qt::QueuedConnection);
            }
        }
    }

    // P0 #②: visible range at Thumbnail priority, then predictive neighbors.
    ThumbnailPipeline::instance().setVisibleRange(static_cast<size_t>(first),
                                                  static_cast<size_t>(last + 1));
    ThumbnailPipeline::instance().setPredictiveCount(48);
}

void ThumbnailPanel::onThumbReady(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

QPixmap ThumbnailPanel::thumbReady(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    auto it = m_thumbReady.constFind(path);
    return it == m_thumbReady.constEnd() ? QPixmap() : it.value();
}

bool ThumbnailPanel::thumbFailed(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    return m_thumbFailed.contains(path);
}

void ThumbnailPanel::onSelectionChanged()
{
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    qint64 selBytes = 0;
    for (const QModelIndex &idx : sel)
        selBytes += m_sizeByPath.value(m_paths.value(idx.row()), 0);
    m_compareBtn->setVisible(sel.size() >= 2 && sel.size() <= 8);
    emit statsChanged(m_paths.size(), m_totalBytes, sel.size(), selBytes);
}

void ThumbnailPanel::scrollToPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    setCurrentIndex(idx);
    scrollTo(idx, PositionAtCenter);
}

void ThumbnailPanel::selectPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    if (currentIndex() == idx)
        return; // already the current item — nothing to do, no scroll jank
    setCurrentIndex(idx);
    scrollTo(idx); // default EnsureVisible: only scrolls when off-screen
}

int ThumbnailPanel::scrollOffset() const
{
    return verticalScrollBar()->value();
}

QStringList ThumbnailPanel::selectedPaths() const
{
    QStringList r;
    for (const QModelIndex &idx : selectionModel()->selectedIndexes())
        r.append(m_paths.value(idx.row()));
    return r;
}

void ThumbnailPanel::renameSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString oldPath = paths.first();
    const QFileInfo fi(oldPath);
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, "重命名", "新文件名:", QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.isEmpty() || newName == fi.fileName())
        return;
    const QString newPath = fi.absolutePath() + "/" + newName;

    // A-10: reversible rename via CommandStack when available.
    if (m_cmdStack)
    {
        auto cmd = std::make_unique<FileRenameCommand>(oldPath.toStdString(), newPath.toStdString());
        m_cmdStack->execute(std::move(cmd));
    }
    else if (!QFile::rename(oldPath, newPath))
    {
        return;
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::moveToTrashSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    // Qt6 removed QStandardPaths::TrashLocation; emulate a per-user trash dir.
    const QString trashDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mviewer/trash";
    QDir().mkpath(trashDir);

    QStringList removed;
    // A-10: reversible delete via CommandStack when available.
    if (m_cmdStack)
    {
        std::vector<std::string> stdPaths;
        stdPaths.reserve(static_cast<size_t>(paths.size()));
        for (const QString &p : paths)
            stdPaths.push_back(p.toStdString());
        auto cmd = std::make_unique<FileDeleteCommand>(std::move(stdPaths), trashDir.toStdString());
        // Capture moved paths before ownership transfers.
        m_cmdStack->execute(std::move(cmd));
        // After execute, files are gone — treat all selected as removed.
        removed = paths;
    }
    else
    {
        for (const QString &p : paths)
        {
            if (QFile::rename(p, trashDir + "/" + QFileInfo(p).fileName()))
                removed.append(p);
            else if (QFile::remove(p))
                removed.append(p);
        }
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
    // Let the host advance the viewer if the current image was just deleted.
    if (!removed.isEmpty())
        emit pathsRemoved(removed);
}

void ThumbnailPanel::copySelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "复制到...");
    if (dir.isEmpty())
        return;
    for (const QString &p : paths)
        QFile::copy(p, dir + "/" + QFileInfo(p).fileName());
}

void ThumbnailPanel::moveSelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "移动到...");
    if (dir.isEmpty())
        return;

    // A-10: reversible move via CommandStack when available.
    if (m_cmdStack)
    {
        std::vector<std::string> stdPaths;
        stdPaths.reserve(static_cast<size_t>(paths.size()));
        for (const QString &p : paths)
            stdPaths.push_back(p.toStdString());
        auto cmd = std::make_unique<FileMoveCommand>(std::move(stdPaths), dir.toStdString());
        m_cmdStack->execute(std::move(cmd));
    }
    else
    {
        for (const QString &p : paths)
            QFile::rename(p, dir + "/" + QFileInfo(p).fileName());
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::revealSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString p = QDir::toNativeSeparators(paths.first());
#ifdef Q_OS_WIN
    QProcess::execute("explorer.exe", QStringList() << "/select," << p);
#else
    QProcess::execute("xdg-open", QStringList() << QFileInfo(paths.first()).absolutePath());
#endif
}

void ThumbnailPanel::batchAnalyzeExport()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString out =
        QFileDialog::getSaveFileName(this, "导出分析结果", "", "CSV (*.csv);;JSON (*.json)");
    if (out.isEmpty())
        return;

    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    const std::vector<std::string> ids = reg.availableAnalyzers();

    QStringList headers = {"path"};
    for (const auto &id : ids)
        headers.append(QString::fromStdString(id));
    QStringList rows;
    rows.append(headers.join(","));

    for (const QString &p : paths)
    {
        auto res = ImageRepository::instance().load(p.toStdString());
        if (!res.frame)
            continue;
        const std::unordered_map<std::string, std::string> texts = reg.runAnalyzer(*res.frame);
        QStringList cells = {p};
        for (const auto &id : ids)
        {
            auto it = texts.find(id);
            cells.append(it == texts.end() ? QString() : QString::fromStdString(it->second));
        }
        rows.append(cells.join(","));
    }
    QFile f(out);
    if (f.open(QIODevice::WriteOnly))
        f.write(rows.join("\n").toUtf8());
}

void ThumbnailPanel::onCompareClicked()
{
    const QStringList sel = selectedPaths();
    if (sel.size() >= 2 && sel.size() <= 8)
        emit compareRequested(sel);
}

void ThumbnailPanel::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;
    const QString path = m_paths.value(idx.row());
    if (!selectionModel()->isSelected(idx))
        selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Clear);

    QMenu menu(this);
    QAction *aOpen = menu.addAction("打开");
    aOpen->setShortcut(QKeySequence(Qt::Key_Return));
    QAction *aRename = menu.addAction("重命名");
    aRename->setShortcut(QKeySequence(Qt::Key_F2));
    QAction *aCopy = menu.addAction("复制...");
    aCopy->setShortcut(QKeySequence("Ctrl+C"));
    QAction *aMove = menu.addAction("移动...");
    aMove->setShortcut(QKeySequence("Ctrl+M"));
    QAction *aTrash = menu.addAction("移到回收站");
    aTrash->setShortcut(QKeySequence(Qt::Key_Delete));
    QAction *aReveal = menu.addAction("在资源管理器中显示");
    aReveal->setShortcut(QKeySequence("Ctrl+E"));
    QAction *aCopyPath = menu.addAction("复制路径");
    aCopyPath->setShortcut(QKeySequence("Ctrl+Shift+C"));
    QAction *aCompare = menu.addAction("比较");
    QAction *aAnalyze = menu.addAction("批量分析导出");
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == aOpen)
        emit itemDoubleClicked(path);
    else if (chosen == aRename)
        renameSelected();
    else if (chosen == aCopy)
        copySelectedTo();
    else if (chosen == aMove)
        moveSelectedTo();
    else if (chosen == aTrash)
        moveToTrashSelected();
    else if (chosen == aReveal)
        revealSelected();
    else if (chosen == aCopyPath)
        QApplication::clipboard()->setText(path);
    else if (chosen == aCompare)
        onCompareClicked();
    else if (chosen == aAnalyze)
        batchAnalyzeExport();
}

void ThumbnailPanel::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    if (m_compareBtn)
        m_compareBtn->move(viewport()->width() - m_compareBtn->width() - 8, 8);
    positionDetailsHeader();
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::wheelEvent(QWheelEvent *event)
{
    // Ctrl+wheel resizes thumbnails (Windows Explorer / FastStone parity);
    // plain wheel scrolls as usual.
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
        {
            const int step = (delta > 0 ? 1 : -1) * 16;
            setThumbSize(qBound(kMinThumbSize, m_thumbSize + step, kMaxThumbSize));
            event->accept();
            return;
        }
    }
    QListView::wheelEvent(event);
}

void ThumbnailPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QListView::dragEnterEvent(event);
}

void ThumbnailPanel::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QListView::dragMoveEvent(event);
}

void ThumbnailPanel::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
    {
        QListView::dropEvent(event);
        return;
    }
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
    {
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            paths.append(local);
    }
    if (paths.isEmpty())
    {
        QListView::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    emit filesDropped(paths);
}

void ThumbnailPanel::positionDetailsHeader()
{
    if (!m_detailsHeader || m_viewMode != Details)
        return;
    const QRect vp = viewport()->geometry();
    m_detailsHeader->setGeometry(vp.x(), vp.y() - kDetailsHeaderH, vp.width(), kDetailsHeaderH);
    m_detailsHeader->raise();
}

void ThumbnailPanel::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::mousePressEvent(QMouseEvent *event)
{
    // P0: Enforce single-selection on plain left-click (no modifier keys).
    // ExtendedSelection normally handles this, but in IconMode with certain Qt
    // builds the selection is not reliably cleared. We make it explicit.
    if (event->button() == Qt::LeftButton &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid())
        {
            selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
            event->accept();
            return;
        }
        // Click on empty area: deselect everything.
        selectionModel()->clearSelection();
        event->accept();
        return;
    }
    QListView::mousePressEvent(event);
}

void ThumbnailPanel::stopThumbnailWorker()
{
    QMutexLocker lk(&m_thumbMtx);
    m_thumbReady.clear();
    m_thumbPending.clear();
    m_thumbFailed.clear();
}

// static
QFileInfoList ThumbnailPanel::sortedEntries(const QDir &dir, SortMode mode, bool ascending)
{
    QStringList imgExts = {"bmp", "gif", "jpg", "jpeg", "png", "tif", "tiff", "webp",
                           "cr2", "cr3", "nef", "nrw", "arw", "dng", "orf", "rw2", "pef",
                           "raf"};
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    QFileInfoList out;
    for (const QFileInfo &fi : files)
        if (imgExts.contains(fi.suffix().toLower()))
            out.append(fi);

    switch (mode)
    {
    case SortName:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
        break;
    case SortDate:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.lastModified() > b.lastModified(); });
        break;
    case SortSize:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b) { return a.size() > b.size(); });
        break;
    case SortResolution:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      auto readSize = [](const QString &path) -> qint64
                      {
                          QImageReader reader(path);
                          reader.setAutoTransform(true);
                          const QSize s = reader.size();
                          return static_cast<qint64>(s.width()) * s.height();
                      };
                      return readSize(a.absoluteFilePath()) > readSize(b.absoluteFilePath());
                  });
        break;
    case SortType:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.suffix().compare(b.suffix(), Qt::CaseInsensitive) < 0; });
        break;
    case SortRating:
        // Rating is stored in core::RatingStore; we use a simple heuristic here.
        // Full rating-based sorting requires per-image query which is handled by
        // the filter system — this branch keeps the interface consistent.
        break;
    }

    // A-2.2: apply sort direction (reverse if descending).
    if (!ascending)
        std::reverse(out.begin(), out.end());
    return out;
}

// ---- ThumbDelegate ----------------------------------------------------------

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

    // Column 3: resolution — read from pre-populated Entry data.
    painter->setFont(option.font);
    QString resStr = "-";
    const QList<Entry> &all = m_panel->entries();
    if (index.row() < all.size())
    {
        const auto &e = all.at(index.row());
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

    painter->restore();
}

QSize ThumbnailPanel::DetailsDelegate::sizeHint(const QStyleOptionViewItem &,
                                                const QModelIndex &) const
{
    // Match the viewport width exactly so no horizontal scrollbar appears and
    // the header row (painted in the top margin) stays aligned with the cells.
    // detailLayout() clamps the name column so all columns always fit.
    const int w = qMax(320, m_panel->viewport()->width());
    return QSize(w, 52);
}
