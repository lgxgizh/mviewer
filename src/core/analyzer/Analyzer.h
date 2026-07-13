#pragma once
#include "core/image/ImageFrame.h"
#include "domain/Histogram.h"
#include "domain/Selection.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

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
    virtual bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) = 0;
};

// Factory function type for analyzer plugins.
using AnalyzerCreator = std::unique_ptr<Analyzer>(*)();

// Registry for analyzer plugins.
class AnalyzerRegistry
{
public:
    static AnalyzerRegistry& instance();

    void registerAnalyzer(const std::string& id, AnalyzerCreator creator);
    std::unique_ptr<Analyzer> create(const std::string& id) const;
    std::vector<std::string> availableAnalyzers() const;

private:
    std::unordered_map<std::string, AnalyzerCreator> m_factories;
};
