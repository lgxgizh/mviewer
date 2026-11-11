#include "core/image/decoder/PlainRawDecoder.h"

#include "core/image/rawplain/PlainRawFile.h"

#include <string>
#include <utility>

namespace
{

namespace rawplain = mviewer::core::rawplain;

mviewer::domain::DecodeError makeError(mviewer::domain::DecodeErrorCode code, std::string message,
                                       const std::string &path)
{
    mviewer::domain::DecodeError err;
    err.code = code;
    err.message = std::move(message);
    err.path = path;
    return err;
}

} // namespace

bool PlainRawDecoder::canDecode(const std::string &path) const
{
    return rawplain::isPlainRawPath(path);
}

std::vector<std::string> PlainRawDecoder::extensions() const
{
    return {"raw"};
}

const char *PlainRawDecoder::name() const
{
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
