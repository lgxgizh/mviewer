#include "core/metadata/MetadataIndexer.h"

#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/search/MetadataFilter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QString>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mviewer::core
{

namespace
{

using Entry = MetadataIndexer::Entry;

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Mirrors SearchIndex::buildBlob so gallery search, panel filters and the
// search panel agree on the searchable text of one file.
std::string buildSearchBlob(const mviewer::domain::ImageMetadata &meta,
                            const RawMetadata &raw)
{
    std::ostringstream oss;
    oss << meta.fileName << " " << meta.filePath << " " << meta.format << " ";
    for (const auto &[k, v] : meta.textKeys)
        oss << k << " " << v << " ";
    oss << raw.make << " " << raw.model << " " << raw.lens << " ";
    const int iso = metadata::isoOf(raw);
    if (iso > 0)
        oss << "ISO" << iso << " ";
    if (raw.focalLength > 0)
        oss << raw.focalLength << "mm ";
    if (raw.exposureSec > 0.0)
        oss << raw.exposureSec << "s ";
    if (raw.fNumber > 0.0)
        oss << "f/" << raw.fNumber << " ";
    if (raw.width > 0)
        oss << raw.width << "x" << raw.height << " ";
    return lower(oss.str());
}

Entry buildEntry(const std::string &path, const mviewer::domain::ImageMetadata &meta,
                 const RawMetadata &raw)
{
    Entry e;
    e.path = path;
    e.searchBlob = buildSearchBlob(meta, raw);
    std::string cam = raw.make;
    if (!raw.model.empty())
    {
        if (!cam.empty())
            cam += " ";
        cam += raw.model;
    }
    e.camera = cam;
    e.lens = raw.lens;
    e.iso = metadata::isoOf(raw);
    e.width = meta.width;
    e.height = meta.height;
    return e;
}

} // namespace

MetadataIndexer &MetadataIndexer::instance()
{
    static MetadataIndexer inst;
    return inst;
}

std::string MetadataIndexer::fileIdentity(const std::string &path)
{
    const QFileInfo fi(QString::fromStdString(path));
    return std::to_string(fi.lastModified().toSecsSinceEpoch()) + "|" +
           std::to_string(fi.size());
}

uint64_t MetadataIndexer::index(const std::vector<std::string> &paths, EntryCallback onEntry,
                                DoneCallback onDone)
{
    auto cancelToken = std::make_shared<std::atomic<bool>>(false);
    uint64_t requestId = 0;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        requestId = ++m_nextRequestId;
        m_requestCancel[requestId] = cancelToken;
    }

    // Marshals `fn` to the main thread; delivers only while THIS request is
    // still alive (cancelled requests drop their remaining deliveries).
    auto deliver = [this, requestId, cancelToken](std::function<void()> fn)
    {
        if (cancelToken->load())
            return;
        if (QCoreApplication::instance())
        {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [requestId, cancelToken, fn]()
                {
                    if (cancelToken->load())
                        return; // superseded request: drop stale callback
                    fn();
                },
                Qt::QueuedConnection);
        }
        else if (!cancelToken->load())
        {
            fn();
        }
    };

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, requestId, cancelToken, paths, onEntry, onDone,
         deliver](const TaskScheduler::TaskContext &ctx)
        {
            for (const std::string &p : paths)
            {
                if (cancelToken->load() || ctx.isCancelled())
                    break;

                Entry e;
                bool fromCache = false;
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    auto it = m_cache.find(p);
                    if (it != m_cache.end() && m_identity.count(p) &&
                        m_identity.at(p) == fileIdentity(p))
                    {
                        e = it->second;
                        fromCache = true;
                    }
                }
                if (!fromCache)
                {
                    if (cancelToken->load() || ctx.isCancelled())
                        break;
                    const mviewer::domain::ImageMetadata meta =
                        mviewer::core::MetadataReader::read(p);
                    const RawMetadata raw = mviewer::core::parseRawMetadata(p);
                    e = buildEntry(p, meta, raw);
                    {
                        std::lock_guard<std::mutex> lk(m_mtx);
                        if (cancelToken->load() || ctx.isCancelled())
                            break;
                        const bool wasAbsent = m_cache.find(p) == m_cache.end();
                        m_cache[p] = e;
                        m_identity[p] = fileIdentity(p);
                        if (wasAbsent)
                            m_cacheOrder.push_back(p);
                        // Bounded cache: evict the oldest indexed paths once
                        // the limit is exceeded (FIFO keeps the current and
                        // recently visited directories, never the whole
                        // session history).
                        while (m_cache.size() > m_cacheLimit && !m_cacheOrder.empty())
                        {
                            const std::string oldest = m_cacheOrder.front();
                            m_cacheOrder.pop_front();
                            m_cache.erase(oldest);
                            m_identity.erase(oldest);
                        }
                    }
                }
                if (onEntry && !cancelToken->load() && !ctx.isCancelled())
                {
                    const Entry copy = e;
                    deliver([onEntry, copy]() { onEntry(copy); });
                }
            }
            if (!cancelToken->load() && !ctx.isCancelled() && onDone)
                deliver([onDone]() { onDone(); });
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                eraseRequestLocked(requestId);
            }
        });
    if (!handle)
    {
        // Scheduler rejected the submission (paused / saturated): the caller
        // must not wait for callbacks. Drop the request and report failure.
        std::lock_guard<std::mutex> lk(m_mtx);
        eraseRequestLocked(requestId);
        return 0;
    }
    return requestId;
}

void MetadataIndexer::eraseRequestLocked(uint64_t requestId)
{
    m_requestCancel.erase(requestId);
}

void MetadataIndexer::cancelRequest(uint64_t requestId)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_requestCancel.find(requestId);
    if (it == m_requestCancel.end())
        return; // already finished or unknown
    it->second->store(true);
    m_requestCancel.erase(it);
}

void MetadataIndexer::cancel()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    for (auto &[id, tok] : m_requestCancel)
        tok->store(true);
    m_requestCancel.clear();
}

std::optional<MetadataIndexer::Entry> MetadataIndexer::cached(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_cache.find(path);
    if (it == m_cache.end())
        return std::nullopt;
    auto idIt = m_identity.find(path);
    if (idIt == m_identity.end() || idIt->second != fileIdentity(path))
        return std::nullopt;
    return it->second; // value copy: safe against concurrent rehash
}

size_t MetadataIndexer::size() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_cache.size();
}

size_t MetadataIndexer::cacheLimit() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_cacheLimit;
}

void MetadataIndexer::setCacheLimit(size_t n)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_cacheLimit = n;
    while (m_cache.size() > m_cacheLimit && !m_cacheOrder.empty())
    {
        const std::string oldest = m_cacheOrder.front();
        m_cacheOrder.pop_front();
        m_cache.erase(oldest);
        m_identity.erase(oldest);
    }
}

} // namespace mviewer::core
