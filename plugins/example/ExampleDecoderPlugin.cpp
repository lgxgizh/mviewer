// Example Decoder plugin (M14.3).
//
// Demonstrates the minimum a third-party image decoder needs to integrate with
// MViewer through the unified plugin loader: implement IDecoder, export the
// frozen ABI triple (mviewer_plugin_abi) plus the create*/destroy*/pluginName
// contract, and PluginManager registers the instance into DecoderRegistry.
//
// This example decodes the uncompressed PPM (P6 binary) format, a tiny,
// well-documented format ideal for teaching the decoder contract.

#include "core/image/ImageBuffer.h"
#include "core/image/decoder/IDecoder.h"
#include "core/plugin/PluginABI.h"
#include "domain/Image.h"

#include <algorithm>
#include <cctype>
#include <fstream>
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

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Skip PPM whitespace and # comments, returning the next non-space byte.
static bool nextToken(std::istream &in, std::string &tok)
{
    tok.clear();
    int c = in.get();
    while (c != EOF && std::isspace(c))
        c = in.get();
    if (c == '#')
    {
        while (c != EOF && c != '\n')
            c = in.get();
        return nextToken(in, tok);
    }
    if (c == EOF)
        return false;
    tok.push_back(static_cast<char>(c));
    while ((c = in.get()) != EOF && !std::isspace(c) && c != '#')
        tok.push_back(static_cast<char>(c));
    return !tok.empty();
}

class PPMDecoder : public IDecoder
{
  public:
    const char *name() const override
    {
        return "ppm-decoder";
    }

    std::vector<std::string> extensions() const override
    {
        return {"ppm", "pnm"};
    }

    bool canDecode(const std::string &path) const override
    {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return false;
        std::string ext = toLower(path.substr(dot + 1));
        return ext == "ppm" || ext == "pnm";
    }

    // Nearest-neighbour downscale so decodeScaled honours maxEdge honestly.
    static ImageData downscale(const ImageData &src, int maxEdge)
    {
        int sw = static_cast<int>(src.width);
        int sh = static_cast<int>(src.height);
        int longest = std::max(sw, sh);
        if (maxEdge <= 0 || longest <= maxEdge)
            return src;
        int dw = sw, dh = sh;
        if (sw >= sh)
            dh = std::max(1, (sh * maxEdge) / sw);
        else
            dw = std::max(1, (sw * maxEdge) / sh);
        ImageData out =
            makeImageData(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh), src.format);
        const int pxSize = (src.format == PixelFormat::RGBA32) ? 4 : 3;
        for (int y = 0; y < dh; ++y)
        {
            int sy = (y * sh) / dh;
            for (int x = 0; x < dw; ++x)
            {
                int sx = (x * sw) / dw;
                const uint8_t *s =
                    src.buffer->data() + (static_cast<size_t>(sy) * sw + sx) * pxSize;
                uint8_t *d = out.buffer->data() + (static_cast<size_t>(y) * dw + x) * pxSize;
                for (int k = 0; k < pxSize; ++k)
                    d[k] = s[k];
            }
        }
        return out;
    }

    ImageData decodeFull(const std::string &path) const override
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};

        std::string magic;
        if (!nextToken(f, magic) || magic != "P6")
            return {};

        std::string wTok, hTok, maxTok;
        if (!nextToken(f, wTok) || !nextToken(f, hTok) || !nextToken(f, maxTok))
            return {};
        int w = std::stoi(wTok), h = std::stoi(hTok), maxVal = std::stoi(maxTok);
        if (w <= 0 || h <= 0 || maxVal <= 0)
            return {};

        ImageData img =
            makeImageData(static_cast<uint32_t>(w), static_cast<uint32_t>(h), PixelFormat::RGB24);
        const size_t bytes = static_cast<size_t>(w) * h * 3;
        if (maxVal <= 255)
        {
            f.read(reinterpret_cast<char *>(img.buffer->data()),
                   static_cast<std::streamsize>(bytes));
            if (static_cast<size_t>(f.gcount()) != bytes)
                return {};
        }
        else
        {
            // 16-bit samples: downshift to 8-bit for the RGB24 buffer.
            std::vector<uint16_t> buf(bytes);
            f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(bytes * 2));
            if (static_cast<size_t>(f.gcount()) != bytes * 2)
                return {};
            for (size_t i = 0; i < bytes; ++i)
                img.buffer->data()[i] = static_cast<uint8_t>(buf[i] >> 8);
        }
        return img;
    }

    ImageData decodeScaled(const std::string &path, int maxEdge) const override
    {
        ImageData full = decodeFull(path);
        if (!full.buffer)
            return full;
        return downscale(full, maxEdge);
    }

    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override
    {
        ImageData img = decodeFull(path);
        if (img.buffer)
        {
            outMeta.filePath = path;
            outMeta.width = static_cast<int>(img.width);
            outMeta.height = static_cast<int>(img.height);
            outMeta.format = "PPM";
            outMeta.bitDepth = 8;
            outMeta.channels = 3;
        }
        return img;
    }
};

// ── Plugin entry points (same contract as Analyzer plugins) ─────────────────

extern "C" MVIEWER_PLUGIN_EXPORT IDecoder *createDecoder()
{
    return new PPMDecoder();
}

extern "C" MVIEWER_PLUGIN_EXPORT void destroyDecoder(IDecoder *p)
{
    delete p;
}

extern "C" MVIEWER_PLUGIN_EXPORT const char *pluginName()
{
    return "Example PPM Decoder";
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
