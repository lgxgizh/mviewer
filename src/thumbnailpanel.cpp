#include "thumbnailpanel.h"

#include "core/analyzer/Analyzer.h"
#include "core/RatingStore.h"
#include "core/image/Decoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/RawMetadata.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "domain/Image.h"
#include "thumbnailcache.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

#include <QAbstractItemView>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QAction>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QScrollBar>
#include <QShowEvent>
#include <QStringListModel>
#include <QTimer>

namespace
{
bool isImageSuffix(const QString &suffix)
{
    static const QStringList exts = {"bmp", "gif",  "jpg", "jpeg", "png",
                                     "tif", "tiff", "webp"};
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
} // namespace

// ---- ThumbnailPanel ---------------------------------------------------------

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setUniformItemSizes(true); // all cells identical -> cheap layout for huge lists
    setSpacing(6);
    setGridSize(QSize(kThumbSize + 16, kThumbSize + 34));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_model = new QStringListModel(this);
    setModel(m_model);

    m_delegate = new ThumbDelegate(kThumbSize, this, this);
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
            { if (idx.isValid()) emit itemClicked(m_paths.value(idx.row())); });
    connect(this, &QAbstractItemView::doubleClicked, this,
            [this](const QModelIndex &idx)
            { if (idx.isValid()) emit itemDoubleClicked(m_paths.value(idx.row())); });

    // Wire the shared pipeline ONCE. The decode step is disk-cache-aware (so a
    // previously visited folder loads instantly without re-decoding), and the
    // result callback lands the QPixmap into our ready map + repaints the cell.
    // The pipeline is a singleton used only by the gallery, so configuring it
    // here is safe.
    m_alive = std::make_shared<std::atomic<bool>>(true);
    if (!m_pipelineWired)
    {
        m_pipelineWired = true;
        ThumbnailPipeline::instance().thumbSize = kThumbSize;
        ThumbnailPipeline::instance().setDecodeFn(
            [this](const std::string &p, int /*size*/)
            {
                QString qp = QString::fromStdString(p);
                QPixmap cached;
                if (ThumbnailCache::instance().get(qp, cached))
                    return mvcore::fromQImage(cached.toImage());
                return Decoder::decodeScaled(p, kThumbSize);
            });
        auto alive = m_alive;
        ThumbnailPipeline::instance().setResultFn(
            [this, alive](const std::string &p, const ImageData &img)
            {
                if (!alive->load())
                    return; // panel destroyed; ignore late callback
                QImage q = mvcore::toQImage(img);
                if (q.isNull())
                    return;
                const int s = kThumbSize;
                QPixmap pm(s, s);
                pm.fill(Qt::transparent);
                QPixmap scaled = QPixmap::fromImage(q).scaled(
                    s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPainter painter(&pm);
                painter.drawPixmap((s - scaled.width()) / 2, (s - scaled.height()) / 2,
                                   scaled);
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
    ThumbnailPipeline::instance().setResultFn(
        [](const std::string &, const ImageData &) {});
    ThumbnailPipeline::instance().setDecodeFn(
        [](const std::string &p, int size) { return Decoder::decodeScaled(p, size); });
}

void ThumbnailPanel::setDirectory(const QString &path)
{
    m_currentDir = path;
    m_filterText.clear();
    m_filterRecursive = false;

    QList<Entry> entries;
    QDir dir(path);
    if (dir.exists())
    {
        const QFileInfoList list = sortedEntries(dir, m_sortMode);
        for (const QFileInfo &fi : list)
            entries.append({fi.absoluteFilePath(), fi.fileName(), fi.size()});
    }
    m_allEntries = entries;
    m_metaIndex.clear();
    applyFilter();
}

void ThumbnailPanel::setSortMode(SortMode mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
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
        const mviewer::core::RawMetadata rm =
            mviewer::core::parseRawMetadata(e.path.toStdString());
        QStringList parts;
        parts << QString::fromStdString(meta.fileName)
              << QString::fromStdString(meta.filePath)
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
        QDirIterator it(m_currentDir,
                        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
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
            src.append(
                {fi.absoluteFilePath(), fi.fileName() + " [" + sub + "]", fi.size()});
        }
    }

    QList<Entry> out;
    for (const Entry &e : src)
    {
        if (m_ratingFilter > 0 &&
            mviewer::core::RatingStore::instance().rating(e.path.toStdString()) <
                m_ratingFilter)
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
                if (r == ep) { inRecents = true; break; }
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

    {
        QMutexLocker lk(&m_thumbMtx);
        m_thumbReady.clear();
        m_thumbPending.clear();
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
    const QModelIndex lastIdx =
        indexAt(QPoint(viewport()->width() - 2, viewport()->height() - 2));
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
                QMetaObject::invokeMethod(this, "onThumbReady", Qt::QueuedConnection,
                                          Q_ARG(QString, p));
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
    const QString newName = QInputDialog::getText(this, "重命名", "新文件名:",
                                                  QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.isEmpty())
        return;
    const QString newPath = fi.absolutePath() + "/" + newName;
    if (QFile::rename(oldPath, newPath) && !m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::moveToTrashSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    // Qt6 removed QStandardPaths::TrashLocation; emulate a per-user trash dir.
    const QString trashDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        "/mviewer/trash";
    QDir().mkpath(trashDir);
    for (const QString &p : paths)
    {
        if (QFile::rename(p, trashDir + "/" + QFileInfo(p).fileName()))
            continue;
        QFile::remove(p);
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
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
    for (const QString &p : paths)
        QFile::rename(p, dir + "/" + QFileInfo(p).fileName());
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
    const QString out = QFileDialog::getSaveFileName(this, "导出分析结果", "",
                                                     "CSV (*.csv);;JSON (*.json)");
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
    QAction *aRename = menu.addAction("重命名");
    QAction *aCopy = menu.addAction("复制...");
    QAction *aMove = menu.addAction("移动...");
    QAction *aTrash = menu.addAction("移到回收站");
    QAction *aReveal = menu.addAction("在资源管理器中显示");
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
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::stopThumbnailWorker()
{
    QMutexLocker lk(&m_thumbMtx);
    m_thumbReady.clear();
    m_thumbPending.clear();
}

// static
QFileInfoList ThumbnailPanel::sortedEntries(const QDir &dir, SortMode mode)
{
    QStringList imgExts = {"bmp", "gif", "jpg", "jpeg", "png", "tif", "tiff", "webp"};
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    QFileInfoList out;
    for (const QFileInfo &fi : files)
        if (imgExts.contains(fi.suffix().toLower()))
            out.append(fi);

    switch (mode)
    {
    case SortName:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
        break;
    case SortDate:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  { return a.lastModified() > b.lastModified(); });
        break;
    case SortSize:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  { return a.size() > b.size(); });
        break;
    case SortResolution:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      const auto ra =
                          ImageRepository::instance().metadata(a.absoluteFilePath().toStdString());
                      const auto rb =
                          ImageRepository::instance().metadata(b.absoluteFilePath().toStdString());
                      const qint64 pa = static_cast<qint64>(ra.width) * ra.height;
                      const qint64 pb = static_cast<qint64>(rb.width) * rb.height;
                      return pa > pb;
                  });
        break;
    }
    return out;
}

// ---- ThumbDelegate ----------------------------------------------------------

void ThumbnailPanel::ThumbDelegate::paint(QPainter *painter,
                                          const QStyleOptionViewItem &option,
                                          const QModelIndex &index) const
{
    const QStringList &paths = m_panel->pathList();
    if (index.row() < 0 || index.row() >= paths.size())
        return;
    const QString path = paths.at(index.row());
    const QString name = index.data(Qt::DisplayRole).toString();

    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.color(QPalette::Highlight));
    else
        painter->fillRect(option.rect, option.palette.color(QPalette::Base));

    const int s = m_thumbSize;
    const QRect thumbRect(option.rect.x() + (option.rect.width() - s) / 2,
                          option.rect.y() + 6, s, s);
    const QPixmap pm = m_panel->thumbReady(path);
    if (!pm.isNull())
    {
        const QPixmap scaled = pm.scaled(thumbRect.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        painter->drawPixmap(thumbRect.x() + (thumbRect.width() - scaled.width()) / 2,
                            thumbRect.y() + (thumbRect.height() - scaled.height()) / 2, scaled);
    }
    else
    {
        painter->fillRect(thumbRect, QColor(228, 228, 228));
    }

    QRect textRect(option.rect.x() + 4, thumbRect.bottom() + 4, option.rect.width() - 8,
                   option.rect.height() - thumbRect.height() - 8);
    painter->setPen(option.state & QStyle::State_Selected
                        ? option.palette.color(QPalette::HighlightedText)
                        : option.palette.color(QPalette::Text));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::ElideRight, name);

    // P1: rating stars overlay (top-left corner).
    const int stars =
        mviewer::core::RatingStore::instance().rating(path.toStdString());
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
        static const QColor kColors[7] = {
            QColor(), QColor(229, 57, 53), QColor(251, 140, 0), QColor(249, 215, 41),
            QColor(67, 160, 71), QColor(30, 136, 229), QColor(142, 36, 170)};
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
        painter->drawText(option.rect.x() + option.rect.width() - 18,
                          option.rect.y() + 16, "⚑");
    }
}

QSize ThumbnailPanel::ThumbDelegate::sizeHint(const QStyleOptionViewItem &,
                                             const QModelIndex &) const
{
    return QSize(m_thumbSize + 16, m_thumbSize + 34);
}
