#pragma once

#include "ImageBuffer.h"

#include <string>
#include <utility>
#include <vector>

// Image encoder: wraps QImageWriter and supports
//  - JPEG (adjustable quality)
//  - PNG (adjustable compression level)
//  - BMP (uncompressed)
//  - WebP (adjustable quality)
// The interface layer exposes only std types; QImage is only an internal implementation detail.
class Encoder
{
  public:
    struct Params
    {
        int quality;
        int pngCompression;
        std::vector<uint8_t> iccProfile;
        std::vector<std::pair<std::string, std::string>> textMetadata;
        Params() : quality(90), pngCompression(6)
        {
        }
        Params(int q) : quality(q), pngCompression(6)
        {
        }
        Params(int q, int p) : quality(q), pngCompression(p)
        {
        }
    };

    // Default encoding parameters (clang-cl requires named default arguments; inline definitions avoid cross-DLL export issues)
    static inline const Params kDefaultParams{90, 6};

    // Encode to file
    static bool encode(const ImageData &img, const std::string &path,
                       const Params &params = kDefaultParams);

    // Encode into a memory buffer
    static std::vector<uint8_t> encodeToBuffer(const ImageData &img, const std::string &format,
                                               const Params &params = kDefaultParams);

    // Infer the format from the file extension
    static std::string formatForExtension(const std::string &ext);

    // Supported output formats
    static std::vector<std::string> supportedOutputFormats();
};
