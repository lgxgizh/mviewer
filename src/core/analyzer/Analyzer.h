#pragma once
#include "core/image/ImageFrame.h"
#include "domain/Histogram.h"
#include "domain/Selection.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Capability flags for analyzers — agents query automatically.
// Defined in same header as Analyzer for convenience (Qt-free).
enum class AnalyzerCapability : uint32_t
{
    None = 0,
    SingleImage = 1 << 0,
    MultiImage = 1 << 1,
    RegionOfInterest = 1 << 2,
    Streaming = 1 << 3,
    GPU = 1 << 4,
};

constexpr AnalyzerCapability operator|(AnalyzerCapability a, AnalyzerCapability b)
{
    return static_cast<AnalyzerCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasCapability(AnalyzerCapability flags, AnalyzerCapability c)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(c)) != 0;
}

// Base class for all analysis algorithms (plugin interface).
// Header is Qt-free; concrete analyzers may use Qt internally (.cpp).
class Analyzer
{
public:
    virtual ~Analyzer() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // Analyze full image or a rectangular region.
    virtual bool analyze(const ImageFrame& frame) = 0;
    virtual bool analyzeRegion(const ImageFrame& frame,
        const mviewer::domain::Selection& region) = 0;

    // Capability flags: agents query this automatically to know which analyzer
    // fits a task. Override with a bitwise-or of AnalyzerCapability values.
    virtual AnalyzerCapability capabilities() const { return AnalyzerCapability::SingleImage; }
};

// Factory: callable that returns a new analyzer instance.
// Defined as std::function so plugin C exports (raw pointers),
// capturing lambdas, and callable objects all register uniformly.
using AnalyzerCreator = std::function<std::unique_ptr<Analyzer>()>;

// Registry for analyzer plugins.
class AnalyzerRegistry
{
public:
    static AnalyzerRegistry& instance();

    void registerAnalyzer(const std::string& id, AnalyzerCreator creator);
    std::unique_ptr<Analyzer> create(const std::string& id) const;
    std::vector<std::string> availableAnalyzers() const;

    // Query capabilities of a registered analyzer without creating a shared copy.
    AnalyzerCapability capabilitiesOf(const std::string& id) const;

private:
    std::unordered_map<std::string, AnalyzerCreator> m_factories;
};
