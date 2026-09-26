#include "compareworkspace_prefetch.h"
#include "compareworkspace_p.h"

#include "application/ImageLoadingService.h"

void CompareWorkspace::cancelPairPrefetch()
{
    auto &svc = mviewer::application::ImageLoadingService::instance();
    for (auto &handle : m_pairPrefetch)
        svc.cancelAsync(handle);
    m_pairPrefetch.clear();
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
            m_pairPrefetch.push_back(std::move(handle));
    }
}
