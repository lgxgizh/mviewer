#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/image/FrameSequence.h"
#include "core/image/MetadataReader.h"

#include <algorithm>

ImageRepository::Result ImageRepository::loadFrame(const std::string &filePath, int frameIndex,
                                                   const LoadOptions &opts)
{
    Result result;
    if (filePath.empty() || frameIndex < 0)
    {
        result.error = "invalid frame request";
        return result;
    }

    const int decodeVariant = std::max(0, opts.frameMaxEdge);
    const auto sequence = mviewer::core::FrameSequenceReader::probeSequence(filePath);
    if (!sequence.valid || sequence.kind == mviewer::core::FrameSequenceKind::Static)
    {
        if (frameIndex != 0)
        {
            result.error = "frame index out of range";
            return result;
        }
        result = load(filePath, opts);
        if (result.frame)
        {
            auto staticSequence = sequence;
            staticSequence.valid = true;
            staticSequence.countKnown = true;
            staticSequence.frameCount = 1;
            staticSequence.kind = mviewer::core::FrameSequenceKind::Static;
            staticSequence.animated = false;
            staticSequence.loopCount = -1;
            const mviewer::core::FrameIdentity identity{makeKey(filePath), 0, decodeVariant};
            result.frame->setSequenceIdentity(staticSequence, identity);
        }
        return result;
    }

    const std::string key = makeFrameKey(filePath, frameIndex, decodeVariant);
    ImageData pixels;
    if (CacheManager::instance().getMemory(CacheLevel::FullImage, key, pixels))
    {
        mviewer::domain::ImageMetadata meta;
        if (CacheManager::instance().getMetadata(key, meta))
        {
            auto frame = std::make_shared<ImageFrame>(meta, pixels);
            mviewer::core::FrameIdentity identity{makeKey(filePath), frameIndex, decodeVariant};
            frame->setSequenceIdentity(sequence, identity);
            if (opts.generateHistogram)
                frame->computeHistogram();
            frame->setDecodeState(DecodeState::Decoded);
            frame->setCacheState(CacheState::Memory);
            result.frame = std::move(frame);
            result.fromCache = true;
            return result;
        }
    }

    const auto decoded = mviewer::core::FrameSequenceReader::decode(filePath, frameIndex,
                                                                      decodeVariant);
    if (!decoded.ok)
    {
        result.error = decoded.error.empty() ? "frame decode failed" : decoded.error;
        return result;
    }

    auto frame = std::make_shared<ImageFrame>(decoded.metadata, decoded.pixels);
    const mviewer::core::FrameIdentity identity{makeKey(filePath), frameIndex, decodeVariant};
    frame->setSequenceIdentity(decoded.sequence, identity);
    if (opts.generateHistogram)
        frame->computeHistogram();
    frame->setDecodeState(DecodeState::Decoded);
    frame->setCacheState(CacheState::None);
    CacheManager::instance().putMemory(CacheLevel::FullImage, key, decoded.pixels);
    CacheManager::instance().putMetadata(key, frame->metadata());
    result.frame = std::move(frame);
    return result;
}

std::string ImageRepository::makeFrameKey(const std::string &filePath, int frameIndex,
                                          int decodeVariant) const
{
    const mviewer::core::FrameIdentity identity{makeKey(filePath), frameIndex,
                                                 std::max(0, decodeVariant)};
    return identity.cacheKey();
}
