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
    std::lock_guard<std::mutex> lk(m_mtx);
    ++m_gen;
    const uint64_t gen = m_gen;
    if (m_handle)
        TaskScheduler::cancel(m_handle);

    auto isCurrent = [this, gen]() -> bool
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return gen == m_gen;
    };

    m_handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, gen, paths, onEntry, onDone, isCurrent](const TaskScheduler::TaskContext &ctx)
        {
            auto deliver = [isCurrent, gen](std::function<void()> fn)
            {
                if (QCoreApplication::instance())
                {
                    QMetaObject::invokeMethod(
                        QCoreApplication::instance(),
                        [isCurrent, gen, fn]()
                        {
                            if (!isCurrent())
                                return; // superseded generation: drop stale callback
                            fn();
                        },
                        Qt::QueuedConnection);
                }
                else if (isCurrent())
                {
                    fn();
                }
            };

            for (const std::string &p : paths)
            {
                if (ctx.isCancelled())
                    return;
                if (!isCurrent())
                    return;

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
                    const mviewer::domain::ImageMetadata meta =
                        mviewer::core::MetadataReader::read(p);
                    const RawMetadata raw = mviewer::core::parseRawMetadata(p);
                    e = buildEntry(p, meta, raw);
                    {
                        std::lock_guard<std::mutex> lk(m_mtx);
                        if (gen != m_gen)
                            return;
                        m_cache[p] = e;
                        m_identity[p] = fileIdentity(p);
                    }
                }
                if (onEntry)
                {
                    const Entry copy = e;
                    deliver([onEntry, copy]() { onEntry(copy); });
                }
            }
            if (isCurrent() && onDone)
                deliver([onDone]() { onDone(); });
        });
    return gen;
}

void MetadataIndexer::cancel()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    ++m_gen;
    if (m_handle)
        TaskScheduler::cancel(m_handle);
    m_handle = nullptr;
}

const MetadataIndexer::Entry *MetadataIndexer::cached(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_cache.find(path);
    if (it == m_cache.end())
        return nullptr;
    auto idIt = m_identity.find(path);
    if (idIt == m_identity.end() || idIt->second != fileIdentity(path))
        return nullptr;
    return &it->second;
}

size_t MetadataIndexer::size() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_cache.size();
}

} // namespace mviewer::core
