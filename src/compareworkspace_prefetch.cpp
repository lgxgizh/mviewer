#include "compareworkspace_prefetch.h"
#include "compareworkspace_p.h"

#include "application/ImageLoadingService.h"

#include <utility>

void CompareWorkspace::claimPromotedPrefetch(
    const std::vector<std::string> &paths, const std::vector<int> &frameIndices,
    std::vector<mviewer::application::ImageLoadingService::AsyncRequestHandle> &out)
{
    out.clear();
    out.resize(paths.size());
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const int frameIndex = i < frameIndices.size() && frameIndices[i] > 0 ? frameIndices[i] : 0;
        if (frameIndex != 0)
            continue;
        if (m_session && m_session->framePool.tryGet(paths[i], frameIndex))
            continue;
        out[i] = takePrefetchHandle(paths[i]);
    }
    // Drop leftovers before focused panes submit Decode work. Promoted handles
    // were already taken out of m_pairPrefetch.
    cancelPairPrefetch();
}

void CompareWorkspace::cancelPairPrefetch()
{
    auto &svc = mviewer::application::ImageLoadingService::instance();
    for (auto &entry : m_pairPrefetch)
        svc.cancelAsync(entry.handle);
    m_pairPrefetch.clear();
}

mviewer::application::ImageLoadingService::AsyncRequestHandle
CompareWorkspace::takePrefetchHandle(const std::string &path)
{
    for (auto it = m_pairPrefetch.begin(); it != m_pairPrefetch.end(); ++it)
    {
        if (it->path != path)
            continue;
        auto handle = std::move(it->handle);
        m_pairPrefetch.erase(it);
        return handle;
    }
    return {};
}

void CompareWorkspace::prefetchNeighborPairs()
{
    cancelPairPrefetch();
    if (m_imagePool.isEmpty() || m_navWindow <= 0 || !m_lifetime)
        return;

    std::vector<std::string> pool;
    pool.reserve(static_cast<size_t>(m_imagePool.size()));
    for (const QString &p : m_imagePool)
        pool.push_back(p.toUtf8().toStdString());

    std::vector<std::string> paths =
        mviewer::ui::neighborPairPaths(pool, m_pairIndex, m_navWindow, +1);
    const auto prev = mviewer::ui::neighborPairPaths(pool, m_pairIndex, m_navWindow, -1);
    paths.insert(paths.end(), prev.begin(), prev.end());
    if (paths.empty())
        return;

    auto &svc = mviewer::application::ImageLoadingService::instance();
    m_pairPrefetch.reserve(paths.size());
    for (const std::string &path : paths)
    {
        if (path.empty())
            continue;
        auto handle = svc.preloadAsync(path, m_lifetime);
        if (handle)
            m_pairPrefetch.push_back(mviewer::ui::PairPrefetchEntry{path, std::move(handle)});
    }
}
