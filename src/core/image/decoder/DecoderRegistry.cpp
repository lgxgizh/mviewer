#include "core/image/decoder/DecoderRegistry.h"

#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"

DecoderRegistry &DecoderRegistry::instance()
{
    static DecoderRegistry inst;
    return inst;
}

DecoderRegistry::DecoderRegistry()
{
    resetToDefaults();
}

void DecoderRegistry::resetToDefaults()
{
    m_decoders.clear();
    registerDecoder(std::make_shared<QtDecoder>());
    // TODO(M7): RAW — add a RawDecoder here once libraw integration lands.
    // The fallback must remain LAST so specific decoders get first pick.
    registerDecoder(std::make_shared<QtFallbackDecoder>());
}

void DecoderRegistry::registerDecoder(std::shared_ptr<IDecoder> decoder)
{
    if (decoder)
        m_decoders.push_back(std::move(decoder));
}

ImageData DecoderRegistry::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData DecoderRegistry::decodeFull(const std::string &path,
                                      mviewer::domain::ImageMetadata &outMeta) const
{
    for (const auto &d : m_decoders)
    {
        if (d->canDecode(path))
        {
            ImageData out = d->decodeFull(path, outMeta);
            if (!out.isNull())
                return out;
        }
    }
    // No decoder claimed the file (or all failed). Graceful: empty ImageData.
    // TODO(M7): RAW — return a decode-failure marker for unsupported formats.
    return ImageData();
}

ImageData DecoderRegistry::decodeScaled(const std::string &path, int maxEdge) const
{
    for (const auto &d : m_decoders)
    {
        if (d->canDecode(path))
        {
            ImageData out = d->decodeScaled(path, maxEdge);
            if (!out.isNull())
                return out;
        }
    }
    return ImageData();
}

std::vector<std::string> DecoderRegistry::supportedExtensions() const
{
    std::vector<std::string> all;
    for (const auto &d : m_decoders)
    {
        const auto exts = d->extensions();
        all.insert(all.end(), exts.begin(), exts.end());
    }
    return all;
}
