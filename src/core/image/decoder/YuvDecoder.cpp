#include "core/image/decoder/YuvDecoder.h"
#include "core/image/yuv/YuvExtensionMap.h"

#include "core/image/yuv/YuvFile.h"

#include <string>
#include <utility>

namespace
{

using mviewer::core::yuv::OpenedYuvFile;
using mviewer::core::yuv::YuvPlanes;

mviewer::domain::DecodeError makeError(mviewer::domain::DecodeErrorCode code, std::string message,
                                       const std::string &path)
{
    mviewer::domain::DecodeError err;
    err.code = code;
    err.message = std::move(message);
    err.path = path;
    return err;
}

bool openKnown(const std::string &path, OpenedYuvFile &opened)
{
    if (!mviewer::core::yuv::openYuvFile(path, opened) || !opened.info.known || !opened.read)
        return false;
    return true;
}

} // namespace

std::string YuvDecoder::paramKey(const std::string &path)
{
    return mviewer::core::yuv::yuvParamKey(path);
}

bool YuvDecoder::canDecode(const std::string &path) const
{
    return mviewer::core::yuv::isYuvPath(path);
}

std::vector<std::string> YuvDecoder::extensions() const
{
    // Live: the registry re-reads this, so a Preferences edit of the YUV
    // extension mapping reaches browse filters and the file list.
    return mviewer::core::yuv::yuvExtensions();
}

const char *YuvDecoder::name() const
{
    return "YuvDecoder";
}

mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
YuvDecoder::decodeFullResult(const std::string &path, mviewer::domain::ImageMetadata &outMeta) const
{
    using mviewer::domain::DecodeErrorCode;
    using Result = mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>;
    try
    {
        OpenedYuvFile opened;
        if (!openKnown(path, opened))
        {
            const std::string why =
                opened.info.why.empty() ? std::string("cannot open file") : opened.info.why;
            return Result::err(makeError(DecodeErrorCode::UnsupportedFormat,
                                         std::string("YUV parameters unknown: ") + why, path));
        }
        YuvPlanes planes;
        std::string why;
        if (!mviewer::core::yuv::readYuvFrame(opened.info, 0, opened.read, planes, &why))
        {
            const std::string message = why.empty() ? std::string("YUV frame read failed") : why;
            return Result::err(makeError(DecodeErrorCode::CorruptData, message, path));
        }
        ImageData image = mviewer::core::yuv::renderYuvImage(
            planes, opened.info.format, 0, mviewer::core::yuv::defaultChromaUpsample());
        if (image.isNull())
        {
            return Result::err(
                makeError(DecodeErrorCode::CorruptData, "YUV conversion failed", path));
        }
        mviewer::core::yuv::fillYuvMetadata(opened, path, 0, outMeta);
        return Result::ok(std::move(image));
    }
    catch (...)
    {
        return Result::err(makeError(DecodeErrorCode::CorruptData, "YUV decode failed", path));
    }
}

ImageData YuvDecoder::decodeFull(const std::string &path,
                                 mviewer::domain::ImageMetadata &outMeta) const
{
    auto result = decodeFullResult(path, outMeta);
    if (!result.isOk())
        return ImageData{};
    return result.value();
}

ImageData YuvDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData YuvDecoder::decodeScaled(const std::string &path, int maxEdge,
                                   mviewer::domain::ImageMetadata &outMeta) const
{
    try
    {
        if (maxEdge <= 0)
            return decodeFull(path, outMeta);
        OpenedYuvFile opened;
        if (!openKnown(path, opened))
            return ImageData{};
        // Samples the raw frame directly: no full-frame unpack for thumbnails.
        ImageData image = mviewer::core::yuv::readYuvFrameImage(
            opened.info, 0, opened.read, maxEdge, mviewer::core::yuv::defaultChromaUpsample());
        if (image.isNull())
            return ImageData{};
        mviewer::core::yuv::fillYuvMetadata(opened, path, 0, outMeta);
        return image;
    }
    catch (...)
    {
        return ImageData{};
    }
}

ImageData YuvDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

bool YuvDecoder::canProbe(const std::string &path) const
{
    return canDecode(path);
}

bool YuvDecoder::probeMetadata(const std::string &path, mviewer::domain::ImageMetadata &meta) const
{
    try
    {
        OpenedYuvFile opened;
        if (!openKnown(path, opened))
            return false;
        mviewer::core::yuv::fillYuvMetadata(opened, path, 0, meta);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool YuvDecoder::canNativeLod(const std::string &path) const
{
    (void)path;
    return false;
}

bool YuvDecoder::canNativeRegion(const std::string &path) const
{
    (void)path;
    return false;
}

ImageData YuvDecoder::decodeLod(const std::string &path, int maxEdge,
                                mviewer::domain::ImageMetadata &meta) const
{
    return decodeScaled(path, maxEdge, meta);
}
