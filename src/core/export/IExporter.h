#pragma once

#include <string>
#include <vector>

struct ImageData; // defined in core/image/ImageBuffer.h (global scope)

// Core export interface (Qt-free header). Mirrors IDecoder as a
// plugin-discoverable extension point so third-party exporters can register
// into ExporterRegistry through the same PluginLoader contract used by
// Analyzer and Decoder plugins. Concrete exporters implement this interface in
// plugin modules.
class IExporter
{
  public:
    virtual ~IExporter() = default;

    // Stable, unique id used for registration and lookup (e.g. "png-exporter").
    virtual std::string name() const = 0;
    // Human-readable description shown in UI.
    virtual std::string description() const = 0;
    // Output extensions this exporter can write, lowercased without dot
    // (e.g. {"png", "bmp"}). Used to match output paths / file dialog filters.
    virtual std::vector<std::string> extensions() const = 0;

    // Write the given decoded image to outPath. Returns true on success.
    virtual bool exportImage(const ImageData &img, const std::string &outPath) = 0;
};
