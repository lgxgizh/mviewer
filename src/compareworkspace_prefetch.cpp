#include "compareworkspace_prefetch.h"
#include "compareworkspace_p.h"

#include "application/ImageLoadingService.h"

#include <utility>

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
