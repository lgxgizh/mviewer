#include "core/analyzer/Analyzer.h"

AnalyzerRegistry& AnalyzerRegistry::instance()
{
    static AnalyzerRegistry inst;
    return inst;
}

void AnalyzerRegistry::registerAnalyzer(const std::string& id, AnalyzerCreator creator)
{
    m_factories[id] = creator;
}

std::unique_ptr<Analyzer> AnalyzerRegistry::create(const std::string& id) const
{
    auto it = m_factories.find(id);
    if (it == m_factories.end())
        return nullptr;
    return it->second();
}

std::vector<std::string> AnalyzerRegistry::availableAnalyzers() const
{
    std::vector<std::string> out;
    out.reserve(m_factories.size());
    for (const auto& kv : m_factories)
        out.push_back(kv.first);
    return out;
}
