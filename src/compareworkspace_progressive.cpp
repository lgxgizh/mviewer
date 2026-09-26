#include "compareworkspace_caption.h"
#include "compareworkspace_p.h"
#include "compareworkspace_prefetch.h"

#include "application/ImageLoadingService.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "previewpanel.h"

#include <QFileInfo>
#include <QTimer>

#include <algorithm>

namespace
{
int countKeptFrames(const std::vector<std::shared_ptr<ImageFrame>> &frames)
{
    int n = 0;
    for (const auto &f : frames)
    {
        if (!f)
            continue;
        if (!f->pixels().isNull() || !f->metadata().filePath.empty())
            ++n;
    }
    return n;
}

QString basenameOfPath(const std::string &path)
{
    return QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).fileName();
}
} // namespace

void CompareWorkspace::noteCompareInteraction()
{
    m_interactionBusy = true;
    ++m_interactionGen;
    const uint64_t gen = m_interactionGen;
    QPointer<CompareWorkspace> guard(this);
    QTimer::singleShot(140, this,
                       [guard, gen]()
                       {
                           CompareWorkspace *ws = guard.data();
                           if (!ws || ws->m_interactionGen != gen)
                               return;
                           ws->m_interactionBusy = false;
                           ws->flushDeferredCompareAnalysis();
                       });
}

void CompareWorkspace::flushDeferredCompareAnalysis()
{
    if (m_interactionBusy || (m_loadInFlight && m_softPairReload))
        return;
    const bool wantDiff = m_deferredDiffRefresh;
    const bool wantHist = m_deferredHistRefresh;
    m_deferredDiffRefresh = false;
    m_deferredHistRefresh = false;
    if (wantDiff)
        refreshAllDiffOverlays();
    if (wantHist && m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
}

bool CompareWorkspace::shouldDeferHeavyCompareWork() const
{
    return m_interactionBusy || (m_loadInFlight && m_softPairReload);
}

void CompareWorkspace::applySoftReloadPlaceholders(const std::vector<std::string> &paths)
{
    const int n = std::min(static_cast<int>(paths.size()), static_cast<int>(m_cellViews.size()));
    auto &svc = mviewer::application::ImageLoadingService::instance();
    for (int i = 0; i < n; ++i)
    {
        RawImageView *view = m_cellViews[i];
        if (!view)
            continue;

        const std::string &path = paths[static_cast<size_t>(i)];
        const bool samePath = i < static_cast<int>(m_comparePaths.size()) &&
                              m_comparePaths[static_cast<size_t>(i)] == path;

        if (i < m_cellLabels.size())
            setComparePaneCaptionText(m_cellLabels[i], basenameOfPath(path));

        view->setSoftLoading(true);
        if (samePath)
            continue;

        // Fast first paint: warm Browse/Preview thumbnail when available.
        ImageData preview;
        const std::string key = PreviewPanel::previewCacheKey(path);
        if (!svc.getPreviewCache(key, preview) || preview.isNull())
            continue;
        const QImage q = mvcore::toQImage(preview);
        if (q.isNull())
            continue;
        const QSize srcSize = view->sourceSize().isValid() ? view->sourceSize() : q.size();
        const double oldScale = view->scale();
        const QPointF oldOffset = view->offset();
        const QSize oldSource = view->sourceSize();
        view->setImage(q, srcSize, QRect(QPoint(0, 0), srcSize));
        if (oldSource.isValid() && oldSource == srcSize)
            view->setTransform(oldScale, oldOffset);
    }
    update();
}

void CompareWorkspace::clearSoftLoadingIndicators()
{
    for (RawImageView *view : m_cellViews)
    {
        if (view)
            view->setSoftLoading(false);
    }
}

void CompareWorkspace::applyFramesToExistingPanes()
{
    const int n = m_engine.imageCount();
    for (int i = 0; i < n && i < m_cellViews.size(); ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        if (i < m_cellLabels.size() && img)
        {
            setComparePaneCaptionText(
                m_cellLabels[i],
                QString::fromUtf8(img->metadata().fileName.data(),
                                  static_cast<int>(img->metadata().fileName.size())));
        }
        if (RawImageView *view = m_cellViews[i])
        {
            view->setSoftLoading(false);
            view->setPaneTag(QString(QChar('A' + i)));
            if (img && m_filenameOverlay)
                view->setFilenameOverlay(QString::fromStdString(img->metadata().fileName), true);
        }
    }

    std::vector<int> all;
    all.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        all.push_back(i);
    scheduleDisplayMaterialization(all);
    refreshAllDiffOverlays();
}

bool CompareWorkspace::finishLoadInPlaceIfPossible(
    const std::vector<std::shared_ptr<ImageFrame>> &frames)
{
    const int prevPanes = m_cellViews.size();
    const int kept = countKeptFrames(frames);
    if (!mviewer::ui::canReuseComparePanes(prevPanes, kept))
        return false;

    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    if (blinkActive)
        return false;

    // Engine already holds the new frames; panes stay alive.
    if (m_engine.imageCount() != prevPanes)
        return false;

    applyFramesToExistingPanes();
    return true;
}

void CompareWorkspace::seedWarmDisplay(const QString &path, const QImage &image,
                                       const QSize &sourceSize, const QRect &sourceRect)
{
    if (path.isEmpty() || image.isNull() || !sourceSize.isValid() || sourceSize.width() <= 0 ||
        sourceSize.height() <= 0)
        return;
    mviewer::ui::CompareWarmSeed seed;
    seed.path = path.toUtf8().toStdString();
    seed.image = image;
    seed.sourceSize = sourceSize;
    seed.sourceRect = sourceRect.isValid() ? sourceRect : QRect(QPoint(0, 0), sourceSize);
    // Latest seed for a path wins.
    for (auto &existing : m_pendingWarmSeeds)
    {
        if (existing.path == seed.path)
        {
            existing = std::move(seed);
            applyPendingWarmSeeds();
            return;
        }
    }
    m_pendingWarmSeeds.push_back(std::move(seed));
    applyPendingWarmSeeds();
}

void CompareWorkspace::applyPendingWarmSeeds()
{
    if (m_pendingWarmSeeds.empty() || m_cellViews.isEmpty())
        return;
    for (const auto &seed : m_pendingWarmSeeds)
    {
        for (int i = 0; i < static_cast<int>(m_comparePaths.size()) && i < m_cellViews.size(); ++i)
        {
            if (m_comparePaths[static_cast<size_t>(i)] != seed.path)
                continue;
            RawImageView *view = m_cellViews[i];
            if (!view)
                continue;
            const double oldScale = view->scale();
            const QPointF oldOffset = view->offset();
            const QSize oldSource = view->sourceSize();
            // QImage is implicit-shared — no forced deep copy. Keep softLoading
            // so cheap→full / pyramid upgrade still runs after the warm paint.
            view->setSoftLoading(true);
            view->setImage(seed.image, seed.sourceSize, seed.sourceRect);
            if (oldSource.isValid() && oldSource == seed.sourceSize)
                view->setTransform(oldScale, oldOffset);
        }
    }
    // Consumed once panes matched; keep unmatched for a later layout.
    std::vector<mviewer::ui::CompareWarmSeed> leftover;
    leftover.reserve(m_pendingWarmSeeds.size());
    for (const auto &seed : m_pendingWarmSeeds)
    {
        bool matched = false;
        for (const auto &path : m_comparePaths)
        {
            if (path == seed.path)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
            leftover.push_back(seed);
    }
    m_pendingWarmSeeds = std::move(leftover);
    update();
}

CompareWorkspace::DisplayRequest CompareWorkspace::buildPaneDisplayRequest(int pane,
                                                                           bool preferCheap) const
{
    if (!preferCheap)
        return sourceDisplayRequest(pane);
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
        return sourceDisplayRequest(pane);
    RawImageView *view = m_cellViews[pane];
    const ImageFrame *img = pane < m_engine.imageCount() ? m_engine.imageAt(pane) : nullptr;
    mviewer::ui::CompareDisplayPlanningInput input;
    input.pane = pane;
    QSize sourceSize = view->sourceSize();
    if (!sourceSize.isValid() && img)
        sourceSize = QSize(img->metadata().width, img->metadata().height);
    if (!sourceSize.isValid() && pane < static_cast<int>(m_comparePaths.size()))
    {
        // Still unknown — fall back to full planner via sourceDisplayRequest.
        return sourceDisplayRequest(pane);
    }
    input.sourceWidth = sourceSize.width();
    input.sourceHeight = sourceSize.height();
    input.viewportWidth = view->width();
    input.viewportHeight = view->height();
    input.devicePixelRatio = view->devicePixelRatioF();
    input.hasWidgetSourceSize = view->sourceSize().isValid();
    input.currentScale = view->scale();
    input.paneScale = input.currentScale;
    const auto cheap = mviewer::ui::planCompareDisplayCheap(input);
    if (!cheap.isValid())
        return sourceDisplayRequest(pane);
    DisplayRequest req;
    req.target = QSize(cheap.targetWidth, cheap.targetHeight);
    req.sourceRect = QRect(cheap.sourceRect.x, cheap.sourceRect.y, cheap.sourceRect.width,
                           cheap.sourceRect.height);
    req.region = cheap.region;
    req.provisional = true;
    return req;
}
