#include "core/image/decoder/PlainRawDecoder.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawRender.h"
#include "core/render/DisplayResampleDetail.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace
{

namespace rawplain = mviewer::core::rawplain;

// Bands of fewer rows are not worth a pool hop.
constexpr int kMinRowsPerCall = 64;

thread_local bool t_inBand = false;

void runPlainRawRows(int rowCount, const std::function<void(int, int)> &fn)
{
    // A band that renders again (nested) must not wait on the same pool.
    if (t_inBand)
    {
        fn(0, rowCount);
        return;
    }
    mviewer::core::resample_detail::parallelForRows(rowCount, kMinRowsPerCall, nullptr,
                                                    [&fn](int y0, int y1)
                                                    {
                                                        t_inBand = true;
                                                        fn(y0, y1);
                                                        t_inBand = false;
                                                    });
}

[[maybe_unused]] const bool g_installed = []
{
    rawplain::setPlainRawRowsRunner(&runPlainRawRows);
    return true;
}();

mviewer::domain::DecodeError makeError(mviewer::domain::DecodeErrorCode code, std::string message,
                                       const std::string &path)
{
    mviewer::domain::DecodeError err;
    err.code = code;
    err.message = std::move(message);
    err.path = path;
    return err;
}

// False on a missing file or a path that cannot be stated. count is unchanged
// when this returns false (callers seed it with 0).
bool fileMtimeCount(const std::string &path, std::int64_t &count)
{
    try
    {
        std::error_code ec;
        const auto native = mviewer::core::pathFromUtf8(path);
        const auto stamp = std::filesystem::last_write_time(native, ec);
        if (ec)
            return false;
        count = static_cast<std::int64_t>(stamp.time_since_epoch().count());
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// path + plainRawCacheKeySuffix(path) + file mtime. The '|' keeps a numeric
// parameter tail from merging with the mtime.
std::string fullFrameCacheKey(const std::string &path)
{
    std::int64_t mtime = 0;
    (void)fileMtimeCount(path, mtime);
    std::string key = path;
    key += rawplain::plainRawCacheKeySuffix(path);
    key.push_back('|');
    key += std::to_string(mtime);
    return key;
}

// One full frame. ImageData already shares its pixel buffer.
struct FullFrameCache
{
    std::mutex mutex;
    std::string key;
    ImageData image;
    rawplain::PlainRawFileInfo info;
};

FullFrameCache &fullFrameCache()
{
    static FullFrameCache cache;
    return cache;
}

bool copyCachedFrame(const std::string &key, ImageData &image, rawplain::PlainRawFileInfo &info)
{
    FullFrameCache &cache = fullFrameCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (cache.key != key || cache.image.isNull())
        return false;
    image = cache.image;
    info = cache.info;
    return true;
}

void storeCachedFrame(const std::string &key, const ImageData &image,
                      const rawplain::PlainRawFileInfo &info)
{
    FullFrameCache &cache = fullFrameCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.key = key;
    cache.image = image;
    cache.info = info;
}

bool loadCachedFull(const std::string &path, ImageData &image, rawplain::PlainRawFileInfo &info)
{
    if (copyCachedFrame(fullFrameCacheKey(path), image, info))
        return true;
    image = rawplain::decodePlainRawFile(path, 0, &info, nullptr);
    if (image.isNull())
        return false;
    // Key again so a rewrite during the read is stored under the new mtime.
    storeCachedFrame(fullFrameCacheKey(path), image, info);
    return true;
}

bool clampedSelection(const ImageData &src, int x, int y, int w, int h,
                      mviewer::domain::Selection &sel)
{
    if (src.isNull() || w <= 0 || h <= 0)
        return false;
    const long long left = (std::max)(0LL, static_cast<long long>(x));
    const long long top = (std::max)(0LL, static_cast<long long>(y));
    const long long right =
        (std::min)(static_cast<long long>(src.width), static_cast<long long>(x) + w);
    const long long bottom =
        (std::min)(static_cast<long long>(src.height), static_cast<long long>(y) + h);
    if (right <= left || bottom <= top)
        return false;
    sel.x = static_cast<int>(left);
    sel.y = static_cast<int>(top);
    sel.width = static_cast<int>(right - left);
    sel.height = static_cast<int>(bottom - top);
    return true;
}

// Nearest-neighbor scale. Same mapping as the no-capability path in SourceImage.
ImageData scaleNearest(const ImageData &src, int targetW, int targetH)
{
    if (src.isNull() || targetW <= 0 || targetH <= 0)
        return ImageData{};
    if (src.width == targetW && src.height == targetH)
        return src;
    const int cpp = src.channelsPerPixel();
    ImageData dst = makeImageData(targetW, targetH, src.format);
    if (dst.isNull())
        return ImageData{};
    const ConstImageView v = src.constView();
    const ImageBuffer dv = dst.view();
    for (int y = 0; y < targetH; ++y)
    {
        const int sy = std::min(src.height - 1, (y * src.height) / targetH);
        const uint8_t *sp = v.data + static_cast<size_t>(sy) * v.stride();
        uint8_t *dp = dv.data + static_cast<size_t>(y) * dv.stride();
        for (int x = 0; x < targetW; ++x)
        {
            const int sx = std::min(src.width - 1, (x * src.width) / targetW);
            const uint8_t *p = sp + static_cast<size_t>(sx) * cpp;
            uint8_t *q = dp + static_cast<size_t>(x) * cpp;
            for (int c = 0; c < cpp; ++c)
                q[c] = p[c];
        }
    }
    return dst;
}

ImageData cropScaleNearest(const ImageData &src, int x, int y, int w, int h, int targetW,
                           int targetH)
{
    mviewer::domain::Selection sel;
    if (!clampedSelection(src, x, y, w, h, sel))
        return ImageData{};
    ImageData crop = cropRegion(src, sel);
    if (crop.isNull())
        return ImageData{};
    return scaleNearest(crop, targetW, targetH);
}

bool decodeCachedRegion(const std::string &path, int x, int y, int w, int h, int targetW,
                        int targetH, mviewer::domain::ImageMetadata &meta, ImageData &scaled)
{
    try
    {
        ImageData full;
        rawplain::PlainRawFileInfo info;
        if (!loadCachedFull(path, full, info))
            return false;
        scaled = cropScaleNearest(full, x, y, w, h, targetW, targetH);
        if (scaled.isNull())
            return false;
        rawplain::fillPlainRawMetadata(info, path, meta);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

bool PlainRawDecoder::canDecode(const std::string &path) const
{
    return rawplain::isPlainRawPath(path);
}

std::vector<std::string> PlainRawDecoder::extensions() const
{
    return {"raw", "gray", "grey"};
}

const char *PlainRawDecoder::name() const
{
    // Keeps the row-runner installer in this translation unit.
    (void)g_installed;
    return "PlainRawDecoder";
}

mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
PlainRawDecoder::decodeFullResult(const std::string &path,
                                  mviewer::domain::ImageMetadata &outMeta) const
{
    using mviewer::domain::DecodeErrorCode;
    using Result = mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>;
    rawplain::PlainRawFileInfo info;
    std::string why;
    ImageData image = rawplain::decodePlainRawFile(path, 0, &info, &why);
    if (image.isNull())
    {
        const DecodeErrorCode code =
            info.resolved.known ? DecodeErrorCode::CorruptData : DecodeErrorCode::UnsupportedFormat;
        return Result::err(makeError(code, std::string("RAW parameters: ") + why, path));
    }
    rawplain::fillPlainRawMetadata(info, path, outMeta);
    return Result::ok(std::move(image));
}

ImageData PlainRawDecoder::decodeFull(const std::string &path,
                                      mviewer::domain::ImageMetadata &outMeta) const
{
    auto result = decodeFullResult(path, outMeta);
    if (!result.isOk())
        return ImageData{};
    return result.value();
}

ImageData PlainRawDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData PlainRawDecoder::decodeScaled(const std::string &path, int maxEdge,
                                        mviewer::domain::ImageMetadata &outMeta) const
{
    if (maxEdge <= 0)
        return decodeFull(path, outMeta);
    rawplain::PlainRawFileInfo info;
    ImageData image = rawplain::decodePlainRawFile(path, maxEdge, &info, nullptr);
    if (!image.isNull())
        rawplain::fillPlainRawMetadata(info, path, outMeta);
    return image;
}

ImageData PlainRawDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

bool PlainRawDecoder::canProbe(const std::string &path) const
{
    return canDecode(path);
}

bool PlainRawDecoder::probeMetadata(const std::string &path,
                                    mviewer::domain::ImageMetadata &meta) const
{
    rawplain::PlainRawFileInfo info = rawplain::resolvePlainRawFile(path);
    if (!info.resolved.known)
        return false;
    rawplain::fillPlainRawMetadata(info, path, meta);
    return true;
}

bool PlainRawDecoder::canNativeLod(const std::string &path) const
{
    (void)path;
    return false;
}

bool PlainRawDecoder::canNativeRegion(const std::string &path) const
{
    (void)path;
    return false;
}

ImageData PlainRawDecoder::decodeLod(const std::string &path, int maxEdge,
                                     mviewer::domain::ImageMetadata &meta) const
{
    return decodeScaled(path, maxEdge, meta);
}

ImageData PlainRawDecoder::decodeRegion(const std::string &path, int x, int y, int w, int h,
                                        int targetW, int targetH,
                                        mviewer::domain::ImageMetadata &meta) const
{
    ImageData scaled;
    if (!decodeCachedRegion(path, x, y, w, h, targetW, targetH, meta, scaled))
        return ImageData{};
    return scaled;
}
