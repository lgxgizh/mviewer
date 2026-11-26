// M46 cooperative dimension probe (viewport-first).
//
// Reads each entry's real size/frame-count off the thread pool and publishes it in
// batches, so a huge folder does not block on a synchronous header read per file.
// This used to be the tail of thumbnailpanel_async.cpp (860 lines); it is a
// self-contained subsystem with its own state transitions, so it now has its own
// responsibility TU.
#include "thumbnailpanel_p.h"

#include "core/image/ImageFormats.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"
#include "core/metadata/MetadataIndexer.h"

#include <QImageReader>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include <atomic>
#include <functional>
#include <memory>

// ---- M46: cooperative dimension probe (viewport-first) ----------------------
namespace
{
struct DimProbeResult
{
    QSize size;
    int frameCount = 1;
    bool animated = false;
};

DimProbeResult probeEntryDimensions(const QString &path)
{
    DimProbeResult out;
    const auto sequence = mviewer::core::FrameSequenceReader::probe(path.toUtf8().toStdString());
    if (sequence.valid)
    {
        const auto frame = mviewer::core::FrameSequenceReader::frameInfo(
            path.toUtf8().toStdString(), sequence.defaultFrame);
        out.size = QSize(frame.width, frame.height);
        out.frameCount = qMax(1, sequence.frameCount);
        out.animated = sequence.animated;
    }
    if (!out.size.isValid())
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        out.size = mvcore::readerRasterSize(reader);
        if (mviewer::core::qtmetadata::transformSwapsDimensions(reader.transformation()))
            out.size.transpose();
    }
    return out;
}

using DimBatchPublish = std::function<void(const QVector<int> &, const QVector<QSize> &,
                                           const QVector<int> &, const QVector<bool> &, bool)>;

void runDimensionProbeLoop(const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                           const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                           const QStringList &paths, const QVector<int> &order,
                           const std::shared_ptr<const std::function<void()>> &probe,
                           const DimBatchPublish &publish)
{
    QVector<QSize> sizes(paths.size());
    QVector<int> frameCounts(paths.size(), 1);
    QVector<bool> animatedFlags(paths.size(), false);
    QVector<int> batchIdx;
    batchIdx.reserve(32);

    auto flush = [&](bool finalBatch)
    {
        if (batchIdx.isEmpty() && !finalBatch)
            return;
        QVector<QSize> sizeCopy;
        QVector<int> frameCopy;
        QVector<bool> animCopy;
        sizeCopy.reserve(batchIdx.size());
        frameCopy.reserve(batchIdx.size());
        animCopy.reserve(batchIdx.size());
        for (int i : batchIdx)
        {
            sizeCopy.append(sizes.at(i));
            frameCopy.append(frameCounts.at(i));
            animCopy.append(animatedFlags.at(i));
        }
        publish(batchIdx, sizeCopy, frameCopy, animCopy, finalBatch);
        batchIdx.clear();
    };

    for (int oi = 0; oi < order.size(); ++oi)
    {
        if (!alive->load() ||
            genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
            return;
        if (probe)
        {
            try
            {
                (*probe)();
            }
            catch (...)
            {
            }
        }
        const int i = order.at(oi);
        if (i < 0 || i >= paths.size())
            continue;
        const DimProbeResult probed = probeEntryDimensions(paths.at(i));
        sizes[i] = probed.size;
        frameCounts[i] = probed.frameCount;
        animatedFlags[i] = probed.animated;
        batchIdx.append(i);
        if (batchIdx.size() >= ((oi < 64) ? 16 : 48))
            flush(false);
    }
    flush(true);
}
} // namespace

QVector<int> ThumbnailPanel::dimensionProbeOrder() const
{
    QVector<int> order;
    order.reserve(m_allEntries.size());
    QSet<int> seen;
    const int visibleRows = m_paths.size();
    if (visibleRows > 0 && viewport()->height() >= 10)
    {
        int first = 0;
        int last = 0;
        if (m_viewMode == ViewMode::Details)
        {
            const int offset = qMax(0, verticalScrollBar()->value());
            const int visible = qMax(
                1, (offset % kDetailsItemHeight + viewport()->height() + kDetailsItemHeight - 1) /
                       kDetailsItemHeight);
            first = offset / kDetailsItemHeight;
            last = first + visible - 1;
        }
        else
        {
            const QSize cell = gridSize();
            const int cellW = qMax(1, cell.width());
            const int cellH = qMax(1, cell.height());
            const int cols = qMax(1, viewport()->width() / cellW);
            const int firstRow = verticalScrollBar()->value() / cellH;
            first = firstRow * cols;
            last = first + cols * qMax(1, viewport()->height() / cellH);
        }
        first = qBound(0, first, visibleRows - 1);
        last = qBound(first, last, visibleRows - 1);
        for (int row = first; row <= last; ++row)
        {
            const int src = m_sourceRowByPath.value(galleryPathKey(m_paths.at(row)), -1);
            if (src >= 0 && !seen.contains(src))
            {
                order.append(src);
                seen.insert(src);
            }
        }
    }
    for (int i = 0; i < m_allEntries.size(); ++i)
    {
        if (!seen.contains(i))
            order.append(i);
    }
    return order;
}

void ThumbnailPanel::applyDimensionBatch(const QVector<int> &idx, const QStringList &batchPaths,
                                         const QVector<QSize> &sizes, const QVector<int> &frames,
                                         const QVector<bool> &animated)
{
    for (int b = 0; b < idx.size(); ++b)
    {
        // The probe indexes the listing snapshot it started from. A type-filter
        // change or live delta may since have re-partitioned m_allEntries, so
        // re-resolve by path whenever the row no longer holds the probed file.
        int i = idx.at(b);
        const QString probed = batchPaths.value(b);
        if (i < 0 || i >= m_allEntries.size() || m_allEntries.at(i).path != probed)
            i = m_sourceRowByPath.value(galleryPathKey(probed), -1);
        if (i < 0 || i >= m_allEntries.size())
            continue;
        m_allEntries[i].width = sizes.at(b).width();
        m_allEntries[i].height = sizes.at(b).height();
        m_allEntries[i].frameCount = frames.at(b);
        m_allEntries[i].animated = animated.at(b);
        const QString &path = m_allEntries.at(i).path;
        const int drow = m_displayEntryRow.value(galleryPathKey(path), -1);
        if (drow >= 0 && drow < m_displayEntries.size())
        {
            m_displayEntries[drow].width = sizes.at(b).width();
            m_displayEntries[drow].height = sizes.at(b).height();
            m_displayEntries[drow].frameCount = frames.at(b);
            m_displayEntries[drow].animated = animated.at(b);
        }
    }
}

void ThumbnailPanel::publishDimensionBatch(const QPointer<ThumbnailPanel> &self,
                                           const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                                           const QVector<int> &idx, const QStringList &batchPaths,
                                           const QVector<QSize> &sizes, const QVector<int> &frames,
                                           const QVector<bool> &animated, bool finalBatch,
                                           bool resortWhenDone)
{
    QMetaObject::invokeMethod(
        qApp,
        [self, alive, gen, idx, batchPaths, sizes, frames, animated, finalBatch, resortWhenDone]()
        {
            if (!alive->load() || !self)
                return;
            ThumbnailPanel *panel = self.data();
            if (gen != panel->m_dirGen)
                return;
            panel->applyDimensionBatch(idx, batchPaths, sizes, frames, animated);
            if (finalBatch && resortWhenDone)
                panel->scheduleFilter(false);
            else
                panel->viewport()->update();
        });
}

void ThumbnailPanel::dimensionProbeTask(const QPointer<ThumbnailPanel> &self,
                                        const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                                        const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                                        const QStringList &paths, const QVector<int> &order,
                                        bool resortWhenDone)
{
    const auto probe = ThumbnailPanel::scanIterationProbeSnapshot();
    runDimensionProbeLoop(
        alive, gen, genToken, paths, order, probe,
        [self, alive, gen, resortWhenDone,
         paths](const QVector<int> &idx, const QVector<QSize> &sizes, const QVector<int> &frames,
                const QVector<bool> &animated, bool finalBatch)
        {
            QStringList batchPaths;
            batchPaths.reserve(idx.size());
            for (int i : idx)
                batchPaths.append(paths.value(i));
            ThumbnailPanel::publishDimensionBatch(self, alive, gen, idx, batchPaths, sizes, frames,
                                                  animated, finalBatch, resortWhenDone);
        });
}

void ThumbnailPanel::refreshEntryDimensions(const QStringList &paths)
{
    if (paths.isEmpty() || m_allEntries.isEmpty())
        return;
    // Probe only the named files: the full-directory probe is wasteful for the
    // one or two entries a rotate/flip just rewrote.
    QStringList probePaths;
    probePaths.reserve(paths.size());
    QVector<int> order;
    order.reserve(paths.size());
    for (const QString &path : paths)
    {
        const int row = m_sourceRowByPath.value(galleryPathKey(path), -1);
        if (row < 0)
            continue;
        order.append(probePaths.size());
        probePaths.append(m_allEntries.at(row).path);
    }
    if (order.isEmpty())
        return;

    const int gen = m_dirGen;
    auto alive = m_alive;
    auto genToken = m_scanGenToken;
    const QPointer<ThumbnailPanel> self(this);
    const bool resortWhenDone = (m_sortMode == SortResolution);
    (void)QtConcurrent::run(&m_scanPool,
                            [self, alive, gen, genToken, probePaths, order, resortWhenDone]()
                            {
                                ThumbnailPanel::dimensionProbeTask(
                                    self, alive, gen, genToken, probePaths, order, resortWhenDone);
                            });
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
    const QVector<int> order = dimensionProbeOrder();
    auto alive = m_alive;
    auto genToken = m_scanGenToken;
    const QPointer<ThumbnailPanel> self(this);
    const bool resortWhenDone = (m_sortMode == SortResolution);
    (void)QtConcurrent::run(&m_scanPool,
                            [self, alive, gen, genToken, paths, order, resortWhenDone]()
                            {
                                ThumbnailPanel::dimensionProbeTask(self, alive, gen, genToken,
                                                                   paths, order, resortWhenDone);
                            });
}
