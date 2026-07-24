#include "core/import/ImporterRegistry.h"

#include <algorithm>

ImporterRegistry &ImporterRegistry::instance()
{
    static ImporterRegistry inst;
    return inst;
}

void ImporterRegistry::registerImporter(std::shared_ptr<IImporter> importer)
{
    if (!importer)
        return;
    const std::string id = importer->name();
    for (auto &e : m_importers)
    {
        if (e && e->name() == id)
        {
            e = std::move(importer);
            return;
        }
    }
    m_importers.push_back(std::move(importer));
}

void ImporterRegistry::unregister(const std::string &id)
{
    m_importers.erase(std::remove_if(m_importers.begin(), m_importers.end(),
                                     [&](const std::shared_ptr<IImporter> &e)
                                     { return e && e->name() == id; }),
                      m_importers.end());
}

std::shared_ptr<IImporter> ImporterRegistry::get(const std::string &id) const
{
    for (const auto &e : m_importers)
        if (e && e->name() == id)
            return e;
    return nullptr;
}

std::vector<std::string> ImporterRegistry::available() const
{
    std::vector<std::string> out;
    out.reserve(m_importers.size());
    for (const auto &e : m_importers)
        if (e)
            out.push_back(e->name());
    return out;
}

std::shared_ptr<IImporter> ImporterRegistry::findFor(const std::string &path) const
{
    for (const auto &e : m_importers)
        if (e && e->canImport(path))
            return e;
    return nullptr;
}

mviewer::domain::Workspace ImporterRegistry::importWorkspace(const std::string &path,
                                                             const std::string &id) const
{
    std::shared_ptr<IImporter> imp;
    if (!id.empty())
        imp = get(id);
    else
        imp = findFor(path);
    if (!imp)
        return {};
    return imp->importWorkspace(path);
}
