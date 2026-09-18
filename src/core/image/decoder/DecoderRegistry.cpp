#include "core/image/decoder/DecoderRegistry.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/image/decoder/RawDecoder.h"
#include <cstring>

DecoderRegistry &DecoderRegistry::instance()
{
    static DecoderRegistry inst;
    return inst;
}

DecoderRegistry::DecoderRegistry()
{
    resetToDefaults();
}

std::vector<std::shared_ptr<IDecoder>> DecoderRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_decoders;
}

void DecoderRegistry::resetToDefaults()
{
    // Build the default line-up unlocked, then publish it in one step (the
    // locked registerDecoder() must not be called while holding m_mutex).
    std::vector<std::shared_ptr<IDecoder>> defaults;
    // P6: RAW preview decoder gets first pick for RAW extensions. It returns an
    // empty ImageData when no embedded JPEG preview is found, so non-RAW and
    // preview-less RAW fall through to the Qt decoders below.
    defaults.push_back(std::make_shared<RawDecoder>());
    defaults.push_back(std::make_shared<QtDecoder>());
    // The fallback must remain LAST so specific decoders get first pick.
    defaults.push_back(std::make_shared<QtFallbackDecoder>());

    std::lock_guard<std::mutex> lk(m_mutex);
    m_decoders = std::move(defaults);
}

void DecoderRegistry::registerDecoder(std::shared_ptr<IDecoder> decoder)
{
    if (!decoder)
        return;
    std::lock_guard<std::mutex> lk(m_mutex);
    auto fallbackIt =
        std::find_if(m_decoders.begin(), m_decoders.end(), [](const std::shared_ptr<IDecoder> &d)
                     { return d && std::strcmp(d->name(), "QtFallbackDecoder") == 0; });
    if (fallbackIt != m_decoders.end())
        m_decoders.insert(fallbackIt, std::move(decoder));
    else
        m_decoders.push_back(std::move(decoder));
}

void DecoderRegistry::unregister(const std::string &id)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_decoders.erase(std::remove_if(m_decoders.begin(), m_decoders.end(),
                                    [&](const std::shared_ptr<IDecoder> &d)
                                    { return d && d->name() == id; }),
                     m_decoders.end());
}

std::shared_ptr<IDecoder> DecoderRegistry::get(const std::string &id) const
{
    const auto decoders = snapshot();
    for (const auto &d : decoders)
        if (d && d->name() == id)
            return d;
    return nullptr;
}

std::vector<std::string> DecoderRegistry::available() const
{
    const auto decoders = snapshot();
    std::vector<std::string> ids;
    ids.reserve(decoders.size());
    for (const auto &d : decoders)
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
    // Iterate a snapshot: a plugin registering/unregistering a decoder from the
    // UI thread must not reallocate the vector under this loop, and the
    // shared_ptr copies keep each decoder alive for the whole decode.
    for (const auto &d : snapshot())
    {
        if (d && d->canDecode(path))
        {
            ImageData out = d->decodeFull(path, outMeta);
            if (!out.isNull())
                return out;
        }
    }
    // No decoder claimed the file (or all failed). Graceful: empty ImageData.
    // (RAW preview support is handled by RawDecoder; see DecoderRegistry.h.)
    return ImageData();
}

ImageData DecoderRegistry::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

ImageData DecoderRegistry::decodeScaled(const std::string &path, int maxEdge,
                                        mviewer::domain::ImageMetadata &outMeta) const
{
    for (const auto &d : snapshot())
    {
        if (d && d->canDecode(path))
        {
            ImageData out = d->decodeScaled(path, maxEdge, outMeta);
            if (!out.isNull())
                return out;
        }
    }
    return ImageData();
}

std::vector<std::string> DecoderRegistry::supportedExtensions() const
{
    std::vector<std::string> all;
    for (const auto &d : snapshot())
    {
        if (!d)
            continue;
        const auto exts = d->extensions();
        all.insert(all.end(), exts.begin(), exts.end());
    }
    return all;
}
