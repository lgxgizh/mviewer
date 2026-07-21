#include "core/export/ExporterRegistry.h"

#include <algorithm>

ExporterRegistry& ExporterRegistry::instance() {
    static ExporterRegistry reg;
    return reg;
}

void ExporterRegistry::registerExporter(std::shared_ptr<IExporter> exporter) {
    if (!exporter) return;
    m_exporters.push_back(std::move(exporter));
}

void ExporterRegistry::unregister(const std::string& id) {
    m_exporters.erase(
        std::remove_if(m_exporters.begin(), m_exporters.end(),
                       [&](const std::shared_ptr<IExporter>& e) { return e && e->name() == id; }),
        m_exporters.end());
}

std::shared_ptr<IExporter> ExporterRegistry::get(const std::string& id) const {
    for (const auto& e : m_exporters)
        if (e && e->name() == id) return e;
    return nullptr;
}

std::vector<std::string> ExporterRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_exporters.size());
    for (const auto& e : m_exporters)
        if (e) ids.push_back(e->name());
    return ids;
}

std::vector<std::string> ExporterRegistry::supportedExtensions() const {
    std::vector<std::string> exts;
    for (const auto& e : m_exporters)
        if (e)
            for (const auto& x : e->extensions()) exts.push_back(x);
    return exts;
}

bool ExporterRegistry::exportImage(const std::string& id, const ImageData& img,
                                   const std::string& outPath) const {
    auto e = get(id);
    if (!e) return false;
    return e->exportImage(img, outPath);
}
