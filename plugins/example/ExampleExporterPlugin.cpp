// Example Exporter plugin (M14.3).
//
// Demonstrates the Exporter plugin contract introduced in M14.3: implement
// IExporter, export the frozen ABI triple plus create*/destroy*/pluginName,
// and PluginManager registers the instance into ExporterRegistry. This example
// writes PNG (and BMP) using Qt's QImage, showing how a plugin turns a decoded
// ImageData back into a file -- the "export" half of the analysis workflow.

#include "core/export/IExporter.h"
#include "core/image/ImageBuffer.h"
#include "core/plugin/PluginABI.h"
#include "domain/Image.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <string>
#include <vector>

// Plugin export macro (must be defined before the C exports below; mirrors
// ExampleAnalyzerPlugin.cpp). Not provided by PluginABI.h so each plugin is
// self-contained.
#ifndef MVIEWER_PLUGIN_EXPORT
#ifdef _WIN32
#define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MVIEWER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#endif

class PngExporter : public IExporter
{
  public:
    std::string name() const override
    {
        return "png-exporter";
    }

    std::string description() const override
    {
        return "Example PNG/BMP exporter (Qt QImage backend)";
    }

    std::vector<std::string> extensions() const override
    {
        return {"png", "bmp"};
    }

    bool exportImage(const ImageData &img, const std::string &outPath) override
    {
        if (!img.buffer)
            return false;

        QImage::Format fmt = QImage::Format_RGB888;
        int pxSize = 3;
        bool needSwap = false;
        switch (img.format)
        {
        case PixelFormat::RGB24:
            fmt = QImage::Format_RGB888;
            pxSize = 3;
            break;
        case PixelFormat::RGBA32:
            fmt = QImage::Format_RGBA8888;
            pxSize = 4;
            break;
        case PixelFormat::BGR24:
            // QImage has no BGR888; copy with channels swapped.
            fmt = QImage::Format_RGB888;
            pxSize = 3;
            needSwap = true;
            break;
        default:
            return false;
        }

        QImage qimg(static_cast<int>(img.width), static_cast<int>(img.height), fmt);
        if (qimg.isNull())
            return false;

        const uint8_t *src = img.buffer->data();
        for (int y = 0; y < qimg.height(); ++y)
        {
            uint8_t *dst = qimg.scanLine(y);
            const uint8_t *row = src + static_cast<size_t>(y) * img.width * pxSize;
            if (needSwap)
            {
                for (int x = 0; x < qimg.width(); ++x)
                {
                    dst[x * 3 + 0] = row[x * 3 + 2];
                    dst[x * 3 + 1] = row[x * 3 + 1];
                    dst[x * 3 + 2] = row[x * 3 + 0];
                }
            }
            else
            {
                std::copy_n(row, static_cast<size_t>(img.width) * pxSize, dst);
            }
        }

        return qimg.save(QString::fromStdString(outPath));
    }
};

// ── Plugin entry points (unified contract: Analyzer / Decoder / Exporter) ────

extern "C" MVIEWER_PLUGIN_EXPORT IExporter *createExporter()
{
    return new PngExporter();
}

extern "C" MVIEWER_PLUGIN_EXPORT void destroyExporter(IExporter *p)
{
    delete p;
}

extern "C" MVIEWER_PLUGIN_EXPORT const char *pluginName()
{
    return "Example PNG Exporter";
}

extern "C" MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
{
    static const PluginABI abi; // defaults to {api=1, abi=1, sdk=10000}
    return &abi;
}

extern "C" MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version()
{
    return MVIEWER_API_VERSION;
}
