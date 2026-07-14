#include "core/analyzer/Analyzer.h"

#include "core/analyzer/HistogramAnalyzer.h"

#include <algorithm>
#include <optional>

AnalyzerRegistry& AnalyzerRegistry::instance()
{
    static AnalyzerRegistry inst;
    return inst;
}

namespace
{
bool registerBuiltins()
{
    AnalyzerRegistry::instance().registerAnalyzer("histogram",
        []() -> std::unique_ptr<Analyzer> {
            struct Concrete : public HistogramAnalyzer {
                AnalyzerInfo info() const override {
                    return AnalyzerInfo{
                        .id = "histogram",
                        .name = name(),
                        .description = description(),
                        .version = "0.1.0",
                        .capabilities = capabilities(),
                        .outputFields = {"histogramLuminance", "histogramRGB", "lumMean", "rgbMeans"}
                    };
                }
            };
            return std::make_unique<Concrete>();
        });
    return true;
}
[[maybe_unused]] const bool kRegistered = registerBuiltins();
} // namespace

void AnalyzerRegistry::registerAnalyzer(const std::string& id, AnalyzerCreator creator)
{
    m_factories[id] = std::move(creator);
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

AnalyzerCapability AnalyzerRegistry::capabilitiesOf(const std::string& id) const
{
    auto it = m_factories.find(id);
    if (it == m_factories.end())
        return AnalyzerCapability::None;
    auto instance = it->second();
    return instance->capabilities();
}

std::optional<AnalyzerInfo> AnalyzerRegistry::infoFor(const std::string& id) const
{
    auto it = m_factories.find(id);
    if (it == m_factories.end())
        return std::nullopt;
    auto instance = it->second();
    return instance->info();
}

std::vector<std::string> AnalyzerRegistry::queryByCapability(AnalyzerCapability required) const
{
    std::vector<std::string> result;
    for (const auto& [id, factory] : m_factories) {
        auto instance = factory();
        if (required == AnalyzerCapability::None ||
            hasCapability(instance->capabilities(), required)) {
            result.push_back(id);
        }
    }
    return result;
}
