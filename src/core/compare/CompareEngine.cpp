#include "core/compare/CompareEngine.h"

#include "core/EventBus.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/job/Job.h"

#include <algorithm>
#include <cassert>
#include <mutex>

// ─── CompareEngine (facade) ─────────────────────────────────────────────────

CompareEngine::CompareEngine() : m_layout(CompareLayout::forCount(0)), m_blink(imageCount())
{
}

void CompareEngine::setImages(const std::vector<std::string> &paths)
{
    m_images.clear();
    m_images.reserve(paths.size());
    for (const auto &p : paths)
    {
        auto r = ImageRepository::instance().load(p);
        if (r.success())
            m_images.push_back(std::move(r.frame));
    }
    rebuildLayout();
    m_blink.clearBlink();
}

void CompareEngine::addImage(const std::string &path)
{
    auto r = ImageRepository::instance().load(path);
    if (r.success())
        m_images.push_back(std::move(r.frame));
    rebuildLayout();
}

void CompareEngine::removeImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size()))
        return;
    m_images.erase(m_images.begin() + index);
    rebuildLayout();
}

void CompareEngine::clear()
{
    m_images.clear();
    rebuildLayout();
    m_blink.clearBlink();
}

void CompareEngine::swapFrames(int a, int b)
{
    const int n = static_cast<int>(m_images.size());
    if (a < 0 || a >= n || b < 0 || b >= n || a == b)
        return;
    std::swap(m_images[a], m_images[b]);
}

const ImageFrame *CompareEngine::imageAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_images.size()))
        return nullptr;
    return m_images[index].get();
}

void CompareEngine::rebuildLayout()
{
    const int n = imageCount();
    if (m_forcedCols > 0)
    {
        const int cols = m_forcedCols;
        const int rows = n > 0 ? (n + cols - 1) / cols : 0;
        m_layout = CompareLayout(cols, rows, n);
        m_viewport.setGrid(cols, rows);
    }
    else
    {
        const CompareLayout l = CompareLayout::forCount(n);
        m_layout = l;
        m_viewport.setGrid(l.cols, l.rows);
    }
    m_sync.setCellCount(n);
    m_blink.setImageCount(n);
}

void CompareEngine::setColumns(int cols)
{
    m_forcedCols = cols;
    rebuildLayout();
}

mviewer::domain::CompareSession CompareEngine::session() const
{
    mviewer::domain::CompareSession s;
    s.imageIds.reserve(imageCount());
    for (int i = 0; i < imageCount(); ++i)
        s.imageIds.push_back(m_images[i]->metadata().filePath);
    const std::vector<CellState> &cells = m_sync.cells();
    s.cells.resize(cells.size());
    for (size_t i = 0; i < cells.size(); ++i)
    {
        s.cells[i].scale = cells[i].scale;
        s.cells[i].offsetX = cells[i].offset.x;
        s.cells[i].offsetY = cells[i].offset.y;
    }
    s.syncMode = m_sync.enabled() ? mviewer::domain::SyncMode::All : mviewer::domain::SyncMode::Off;
    s.blinkIndex = m_blink.blinkIndex();
    s.sharedScale = m_sync.scale();
    s.sharedOffsetX = m_sync.offset().x;
    s.sharedOffsetY = m_sync.offset().y;
    s.cols = m_layout.cols;
    s.rows = m_layout.rows;
    // M15 P0#1 (review fix): previously the ROI/selection was NEVER captured
    // here, so it was serialized as [0,0,0,0] and could never be restored.
    // Capture the engine's current synchronized selection (empty if none).
    const auto &sel = m_selection.selection();
    s.selection = {sel.x,
                   sel.y,
                   sel.width,
                   sel.height,
                   (sel.width > 0 && sel.height > 0),
                   m_selection.synced()};
    return s;
}

ImageData CompareEngine::differenceMap(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount())
        return ImageData();
    if (baseIndex < 0 || baseIndex >= imageCount())
        return ImageData();
    return DifferenceEngine::differenceMap(m_images[baseIndex]->pixels(),
                                           m_images[index]->pixels());
}

bool CompareEngine::requestDiff(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount())
        return false;
    if (baseIndex < 0 || baseIndex >= imageCount())
        return false;

    // Capture the two frames' pixel buffers by value (shared_ptr keeps the
    // underlying pixels alive on the worker thread without a copy).
    const ImageData a = m_images[baseIndex]->pixels();
    const ImageData b = m_images[index]->pixels();
    const int idx = index, base = baseIndex;

    mviewer::core::Job job;
    job.name = "CompareEngine.diff";
    // Analysis pool keeps diff work off the UI/decode paths. submit() uses
    // job.priority to route; pool is advisory for submitOnPool callers.
    job.pool = TaskScheduler::PoolType::AnalysisPool;
    job.priority = TaskScheduler::Priority::Analysis;

    job.work = [a, b, idx, base, this](const TaskScheduler::TaskContext &)
    {
        ImageData diff = DifferenceEngine::differenceMap(a, b);
        DiffResult res;
        res.index = idx;
        res.baseIndex = base;
        res.valid = !diff.isNull();
        std::lock_guard<std::mutex> lock(m_diffMtx);
        m_lastDiff = res;
        m_lastDiffImage = diff;
    };
    job.done = [this]()
    {
        // Publish on the EventBus (scope Application) so subscribers (UI) learn
        // the diff is ready. This runs on the worker thread; subscribers that
        // touch widgets must hop to the UI thread themselves (see
        // CompareWorkspace).
        EventBus::instance().publish("CompareEngine.DiffResult", static_cast<void *>(this));
    };

    return mviewer::core::JobSystem::instance().submit(job) != nullptr;
}

DiffResult CompareEngine::lastDiff() const
{
    std::lock_guard<std::mutex> lock(m_diffMtx);
    return m_lastDiff;
}

ImageData CompareEngine::lastDiffImage() const
{
    std::lock_guard<std::mutex> lock(m_diffMtx);
    return m_lastDiffImage;
}

void CompareEngine::applySelectionToAll(const mviewer::domain::Selection &sel)
{
    m_selection.setSelection(sel);
    for (auto &frame : m_images)
    {
        if (frame)
            frame->setSelection(sel);
    }
}

PixelController::ProbeResult CompareEngine::inspectPixel(int imgX, int imgY, int baseIndex) const
{
    std::vector<ImageData> frames;
    frames.reserve(m_images.size());
    for (const auto &f : m_images)
        frames.push_back(f ? f->pixels() : ImageData{});
    return m_pixel.inspect(frames, imgX, imgY, baseIndex);
}
