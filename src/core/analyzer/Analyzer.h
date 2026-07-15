#pragma once

#include "core/analyzer/AnalyzerCapability.h"
#include "core/image/ImageFrame.h"
#include "domain/Histogram.h"
#include "domain/Selection.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Base class for all analysis algorithms (plugin interface).
// Header is Qt-free; concrete analyzers may use Qt internally (.cpp).
class Analyzer
{
public:
    virtual ~Analyzer() = default;

    // Register all built-in analyzers (histogram, noise, entropy, psnr,
    // rgbmean, sharpness, ssim). Idempotent; called automatically by
    // AnalyzerRegistry::instance() so registration does not depend on
    // static-library object-file pruning.
    static void registerBuiltins();
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // Analyze full image or a rectangular region.
    virtual bool analyze(const ImageFrame& frame) = 0;
    virtual bool analyzeRegion(const ImageFrame& frame,
        const mviewer::domain::Selection& region) = 0;

    // Capability flags: agents query this automatically to know which analyzer
    // fits a task. Override with a bitwise-or of AnalyzerCapability values.
    virtual AnalyzerCapability capabilities() const { return AnalyzerCapability::SingleImage; }

    // Static metadata about this analyzer type. Override in subclasses to
    // provide self-describing info for UI and agents.
    virtual AnalyzerInfo info() const
    {
        return AnalyzerInfo{
            .id = "unknown",
            .name = name(),
            .description = description(),
            .version = "0.1.0",
            .capabilities = capabilities(),
            .outputFields = {}
        };
    }
};

// Factory: callable that returns a new analyzer instance.
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

    // Get full info for a registered analyzer.
    std::optional<AnalyzerInfo> infoFor(const std::string& id) const;

    // Query analyzers by capability — returns IDs of all registered analyzers
    // that have ALL of the specified capabilities.
    std::vector<std::string> queryByCapability(AnalyzerCapability required) const;

private:
    std::unordered_map<std::string, AnalyzerCreator> m_factories;
};
