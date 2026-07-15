#include "core/analyzer/Analyzer.h"

#include "core/analyzer/EntropyAnalyzer.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/SharpnessAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"

#include <algorithm>
#include <optional>

AnalyzerRegistry& AnalyzerRegistry::instance()
{
    static AnalyzerRegistry inst;
    Analyzer::registerBuiltins();
    return inst;
}

void Analyzer::registerBuiltins()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    s_registered = true;

    AnalyzerRegistry::instance().registerAnalyzer(
        "histogram",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<HistogramAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "noise",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<NoiseAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "entropy",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<EntropyAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "psnr",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<PSNRAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "rgbmean",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<RGBMeanAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "sharpness",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<SharpnessAnalyzer>(); });
    AnalyzerRegistry::instance().registerAnalyzer(
        "ssim",
        []() -> std::unique_ptr<Analyzer> { return std::make_unique<SSIMAnalyzer>(); });
}

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
