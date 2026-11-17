// Progressive scan publication helpers (generation gate, streamable sorts,
// incremental pipeline inserts). Split from thumbnailpanel_async.cpp.
#include "thumbnailpanel_p.h"

#include "core/thumbnail/ThumbnailPipeline.h"

#include <QDir>

namespace
{

std::shared_ptr<const std::function<void()>> &scanPreExistsProbeRef()
{
    static std::shared_ptr<const std::function<void()>> probe;
    return probe;
}

} // namespace

void ThumbnailPanel::setScanPreExistsProbe(const std::function<void()> &probe)
{
    std::shared_ptr<const std::function<void()>> next;
    if (probe)
        next = std::make_shared<const std::function<void()>>(probe);
    std::atomic_store_explicit(&scanPreExistsProbeRef(), std::move(next),
                               std::memory_order_release);
}

uint64_t ThumbnailPanel::stdPathConversionCount()
{
    return stdPathConversionCounter().load(std::memory_order_relaxed);
}

void ThumbnailPanel::resetStdPathConversionCount()
{
    stdPathConversionCounter().store(0, std::memory_order_relaxed);
}

std::atomic<uint64_t> &scanExistsCounter()
{
    static std::atomic<uint64_t> count{0};
    return count;
}

uint64_t ThumbnailPanel::scanExistsCount()
{
    return scanExistsCounter().load(std::memory_order_relaxed);
}

void ThumbnailPanel::resetScanExistsCount()
{
    scanExistsCounter().store(0, std::memory_order_relaxed);
}

bool scanDirectoryMissing(const QString &path)
{
    scanExistsCounter().fetch_add(1, std::memory_order_relaxed);
    return !QDir(path).exists();
}

void invokeScanPreExistsProbe()
{
    const auto probe =
        std::atomic_load_explicit(&scanPreExistsProbeRef(), std::memory_order_acquire);
    if (!probe)
        return;
    try
    {
        (*probe)();
    }
    catch (...)
    {
    }
}

bool scanStillCurrent(const std::shared_ptr<std::atomic<bool>> &alive,
                      const std::shared_ptr<std::atomic<uint64_t>> &genToken, int gen)
{
    return alive && genToken && alive->load() &&
           genToken->load(std::memory_order_acquire) == static_cast<uint64_t>(gen);
}

bool streamableBrowseSort(ThumbnailPanel::SortMode mode)
{
    return mode == ThumbnailPanel::SortName || mode == ThumbnailPanel::SortDate ||
           mode == ThumbnailPanel::SortSize || mode == ThumbnailPanel::SortType;
}

bool hasMetadataBrowseFilter(const QString &filterText, bool recentFilter, const QString &camera,
                             const QString &lens, int iso)
{
    return !filterText.trimmed().isEmpty() || recentFilter || !camera.isEmpty() ||
           !lens.isEmpty() || iso > 0;
}

bool entryMatchesTypeFilter(const QString &path, const QString &typeFilter)
{
    if (typeFilter.isEmpty())
        return true;
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot + 1 >= path.size())
        return false;
    const QByteArray suffix = path.mid(dot + 1).toUtf8();
    return mviewer::core::suffixMatchesTypeFilter(
        std::string_view(suffix.constData(), static_cast<size_t>(suffix.size())),
        typeFilter.toStdString());
}

void publishIncrementalSources(int consumerId, const QList<QPair<int, QStringList>> &runs)
{
    auto &pipe = ThumbnailPipeline::instance();
    for (const auto &run : runs)
    {
        if (run.second.isEmpty())
            continue;
        pipe.insertSourcesForConsumer(consumerId, static_cast<size_t>(run.first),
                                      toStdPaths(run.second));
    }
}
