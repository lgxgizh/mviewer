#include "core/export/ExporterRegistry.h"

#include <algorithm>
#include <mutex>

ExporterRegistry &ExporterRegistry::instance()
{
    static ExporterRegistry reg;
    return reg;
}

void ExporterRegistry::registerExporter(std::shared_ptr<IExporter> exporter)
{
    if (!exporter)
        return;
    // Batch export runs on a worker thread and walks the registry while the UI
    // thread loads/unloads plugins; without this lock a rehash could destroy the
    // vector being iterated.
    std::lock_guard<std::mutex> lk(m_mutex);
    m_exporters.push_back(std::move(exporter));
}

void ExporterRegistry::unregister(const std::string &id)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_exporters.erase(std::remove_if(m_exporters.begin(), m_exporters.end(),
                                     [&](const std::shared_ptr<IExporter> &e)
                                     { return e && e->name() == id; }),
                      m_exporters.end());
}

std::shared_ptr<IExporter> ExporterRegistry::get(const std::string &id) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto &e : m_exporters)
        if (e && e->name() == id)
            return e; // copy out under the lock; callers use it outside
    return nullptr;
}

std::vector<std::string> ExporterRegistry::available() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<std::string> ids;
    ids.reserve(m_exporters.size());
    for (const auto &e : m_exporters)
        if (e)
            ids.push_back(e->name());
    return ids;
}

std::vector<std::string> ExporterRegistry::supportedExtensions() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<std::string> exts;
    for (const auto &e : m_exporters)
        if (e)
            for (const auto &x : e->extensions())
                exts.push_back(x);
    return exts;
}

bool ExporterRegistry::exportImage(const std::string &id, const ImageData &img,
                                   const std::string &outPath) const
{
    auto e = get(id);
    if (!e)
        return false;
    return e->exportImage(img, outPath);
}
