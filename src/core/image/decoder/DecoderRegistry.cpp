#include "core/image/decoder/DecoderRegistry.h"

#include <algorithm>

#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/image/decoder/RawDecoder.h"

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
    // P6: RAW preview decoder gets first pick for RAW extensions. It returns an
    // empty ImageData when no embedded JPEG preview is found, so non-RAW and
    // preview-less RAW fall through to the Qt decoders below.
    registerDecoder(std::make_shared<RawDecoder>());
    registerDecoder(std::make_shared<QtDecoder>());
    // The fallback must remain LAST so specific decoders get first pick.
    registerDecoder(std::make_shared<QtFallbackDecoder>());
}

void DecoderRegistry::registerDecoder(std::shared_ptr<IDecoder> decoder)
{
    if (decoder)
        m_decoders.push_back(std::move(decoder));
}

void DecoderRegistry::unregister(const std::string &id)
{
    m_decoders.erase(std::remove_if(m_decoders.begin(), m_decoders.end(),
                                    [&](const std::shared_ptr<IDecoder> &d)
                                    { return d && d->name() == id; }),
                     m_decoders.end());
}

std::shared_ptr<IDecoder> DecoderRegistry::get(const std::string &id) const
{
    for (const auto &d : m_decoders)
        if (d && d->name() == id)
            return d;
    return nullptr;
}

std::vector<std::string> DecoderRegistry::available() const
{
    std::vector<std::string> ids;
    ids.reserve(m_decoders.size());
    for (const auto &d : m_decoders)
        if (d)
            ids.push_back(d->name());
    return ids;
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
