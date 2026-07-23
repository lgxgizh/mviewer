// Example analyzer plugin for MViewer (RFC-013 Plugin Registry E2E proof).
//
// Demonstrates the full plugin ABI:
//   extern "C" Analyzer* createAnalyzer();
//   extern "C" void     destroyAnalyzer(Analyzer*);
//   extern "C" const char* pluginName();
//
// This plugin is built into example_analyzer.{dll,so,dylib} and loaded at
// runtime by test_pluginregistry, which then creates an instance through
// AnalyzerRegistry and runs an analysis. It computes the mean luminance of a
// frame (or a rectangular Selection region) and reports it as a scalar.

#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerCapability.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/PluginABI.h"
#include "domain/Selection.h"

#include <cmath>
#include <cstdint>
#include <string>

// Plugin export macro (mirrors what a real plugin host would provide).
#if defined(_WIN32)
#define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MVIEWER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

class MeanLuminanceAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "example.mean_luminance";
    }
    std::string description() const override
    {
        return "Example plugin: mean luminance of a frame or region";
    }

    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }

    bool analyze(const ImageFrame &frame) override
    {
        m_mean = computeMean(frame.pixels(), 0, 0, frame.width(), frame.height());
        m_lastRegion = "full";
        return !frame.pixels().isNull();
    }

    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override
    {
        if (region.isEmpty())
            return analyze(frame);
        const int x0 = std::max(0, region.x);
        const int y0 = std::max(0, region.y);
        const int w = std::max(0, std::min(frame.width(), region.x + region.width) - x0);
        const int h = std::max(0, std::min(frame.height(), region.y + region.height) - y0);
        m_mean = computeMean(frame.pixels(), x0, y0, w, h);
        m_lastRegion = "region";
        return !frame.pixels().isNull() && w > 0 && h > 0;
    }

    std::string resultText() const override
    {
        return m_lastRegion + " mean luminance = " + std::to_string(m_mean);
    }

  private:
    static double computeMean(const ImageData &img, int x0, int y0, int w, int h)
    {
        if (img.isNull() || w <= 0 || h <= 0)
            return 0.0;
        const ImageBuffer v = img.view();
        const int cpp = v.channelsPerPixel();
        long long sum = 0;
        long long n = 0;
        for (int y = y0; y < y0 + h; ++y)
        {
            const uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x0 + w; ++x)
            {
                const uint8_t *p = row + static_cast<size_t>(x) * cpp;
                if (cpp >= 3)
                    sum += luminance(p[0], p[1], p[2]);
                else
                    sum += p[0];
                ++n;
            }
        }
        return n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0;
    }

    double m_mean = 0.0;
    std::string m_lastRegion = "none";
};

} // namespace

extern "C"
{
    MVIEWER_PLUGIN_EXPORT Analyzer *createAnalyzer()
    {
        return new MeanLuminanceAnalyzer();
    }

    MVIEWER_PLUGIN_EXPORT void destroyAnalyzer(Analyzer *a)
    {
        delete a;
    }

    MVIEWER_PLUGIN_EXPORT const char *pluginName()
    {
        return "example.mean_luminance";
    }

    // M14.2: declare the frozen ABI triple so the host can verify compatibility.
    MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
    {
        static const PluginABI abi;
        return &abi;
    }

    // Legacy single-version export (kept for backward compatibility).
    MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version()
    {
        return MVIEWER_API_VERSION;
    }
}
