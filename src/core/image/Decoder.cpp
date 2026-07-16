#include "core/image/Decoder.h"

#include "core/image/decoder/DecoderRegistry.h"

ImageData Decoder::decodeFull(const std::string& path)
{
    return DecoderRegistry::instance().decodeFull(path);
}

ImageData Decoder::decodeFull(const std::string& path,
                                       mviewer::domain::ImageMetadata& outMeta)
{
    return DecoderRegistry::instance().decodeFull(path, outMeta);
}

ImageData Decoder::decodeScaled(const std::string& path, int maxEdge)
{
    return DecoderRegistry::instance().decodeScaled(path, maxEdge);
}

std::vector<std::string> Decoder::supportedExtensions()
{
    // Preserve the legacy "*.ext" wildcard form used by the UI filter and the
    // pipeline acceptance test. The registry stores plain extensions.
    std::vector<std::string> out;
    for (const auto& e : DecoderRegistry::instance().supportedExtensions())
        out.push_back("*." + e);
    return out;
}
