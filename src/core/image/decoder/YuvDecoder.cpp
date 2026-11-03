#include "core/image/decoder/YuvDecoder.h"

#include "core/filesystem/Utf8Path.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <utility>

namespace
{

using mviewer::core::yuv::Format;
using mviewer::core::yuv::Matrix;
using mviewer::core::yuv::Range;
using mviewer::core::yuv::ReadAtFn;
using mviewer::core::yuv::Subsampling;
using mviewer::core::yuv::YuvPlanes;
using mviewer::core::yuv::YuvSourceInfo;

bool endsWithIgnoreCase(const std::string &path, const char *suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (path.size() < n)
    {
        return false;
    }
    const std::size_t off = path.size() - n;
    for (std::size_t i = 0; i < n; ++i)
    {
        unsigned char a = static_cast<unsigned char>(path[off + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (a >= 'A' && a <= 'Z')
        {
            a = static_cast<unsigned char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z')
        {
            b = static_cast<unsigned char>(b - 'A' + 'a');
        }
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

void splitPathNames(const std::string &path, std::string &fileName, std::string &folderName)
{
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
    {
        fileName = path;
        folderName.clear();
        return;
    }
    fileName = path.substr(slash + 1);
    if (slash == 0)
    {
        folderName.clear();
        return;
    }
    const std::string parent = path.substr(0, slash);
    const auto slash2 = parent.find_last_of("/\\");
    folderName = (slash2 == std::string::npos) ? parent : parent.substr(slash2 + 1);
}

int64_t fileSizeOf(const std::string &path)
{
    try
    {
        std::error_code ec;
        const auto native = mviewer::core::pathFromUtf8(path);
        const auto sz = std::filesystem::file_size(native, ec);
        if (ec || sz > static_cast<std::uintmax_t>(std::numeric_limits<int64_t>::max()))
        {
            return 0;
        }
        return static_cast<int64_t>(sz);
    }
    catch (...)
    {
        return 0;
    }
}

size_t readAt(const std::shared_ptr<std::ifstream> &file, int64_t offset, uint8_t *buf, size_t n)
{
    try
    {
        if (!file || buf == nullptr || n == 0 || offset < 0)
        {
            return 0;
        }
        if (offset > static_cast<int64_t>(std::numeric_limits<std::streamoff>::max()))
        {
            return 0;
        }
        file->clear();
        file->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file->good())
        {
            file->clear();
            return 0;
        }
        const auto maxAsk = static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
        const std::streamsize ask = (static_cast<std::uint64_t>(n) > maxAsk)
                                        ? std::numeric_limits<std::streamsize>::max()
                                        : static_cast<std::streamsize>(n);
        file->read(reinterpret_cast<char *>(buf), ask);
        const auto got = file->gcount();
        if (!file->good())
        {
            file->clear();
        }
        if (got <= 0)
        {
            return 0;
        }
        return static_cast<size_t>(got);
    }
    catch (...)
    {
        if (file)
        {
            try
            {
                file->clear();
            }
            catch (...)
            {
            }
        }
        return 0;
    }
}

std::string colorSpaceOf(const Format &format)
{
    const char *matrix = "BT.709";
    if (format.matrix == Matrix::BT601)
    {
        matrix = "BT.601";
    }
    else if (format.matrix == Matrix::BT2020)
    {
        matrix = "BT.2020";
    }
    const char *range = (format.range == Range::Full) ? "full" : "limited";
    std::string text = matrix;
    text.push_back(' ');
    text += range;
    return text;
}

void fillMeta(const YuvSourceInfo &info, const std::string &path, int64_t fileSize,
              mviewer::domain::ImageMetadata &meta)
{
    std::string fileName;
    std::string folderName;
    splitPathNames(path, fileName, folderName);
    meta.width = info.format.width;
    meta.height = info.format.height;
    meta.fileSize = fileSize;
    meta.fileName = std::move(fileName);
    meta.filePath = path;
    meta.bitDepth = info.format.bitDepth;
    meta.channels = (info.format.subsampling == Subsampling::S400) ? 1 : 3;
    meta.format = info.isY4m ? "Y4M" : "YUV";
    meta.colorSpace = colorSpaceOf(info.format);
    meta.frameCount = 1;
    meta.sequenceKind = "static";
}

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

std::string YuvDecoder::paramKey(const std::string &path)
{
    return mviewer::core::normalizeKey(path);
}

bool YuvDecoder::openSource(const std::string &path, YuvSourceInfo &info,
                            std::shared_ptr<std::ifstream> &file) const
{
    info = YuvSourceInfo{};
    file.reset();
    try
    {
        const auto native = mviewer::core::pathFromUtf8(path);
        file = std::make_shared<std::ifstream>(native, std::ios::binary);
        if (!file || !file->is_open())
        {
            file.reset();
            info.known = false;
            info.why = "cannot open file";
            return false;
        }
        std::error_code ec;
        const auto sz = std::filesystem::file_size(native, ec);
        if (ec || sz > static_cast<std::uintmax_t>(std::numeric_limits<int64_t>::max()))
        {
            file.reset();
            info.known = false;
            info.why = "cannot read file size";
            return false;
        }
        const int64_t fileSize = static_cast<int64_t>(sz);
        std::string fileName;
        std::string folderName;
        splitPathNames(path, fileName, folderName);
        const std::shared_ptr<std::ifstream> opened = file;
        const ReadAtFn read = [opened](int64_t offset, uint8_t *buf, size_t n)
        { return readAt(opened, offset, buf, n); };
        const auto saved = mviewer::core::yuv::YuvParamStore::instance().get(paramKey(path));
        info = mviewer::core::yuv::resolveYuvSource(fileName, folderName, fileSize, read, saved);
        return info.known;
    }
    catch (...)
    {
        file.reset();
        info.known = false;
        if (info.why.empty())
        {
            info.why = "exception while opening file";
        }
        return false;
    }
}

bool YuvDecoder::canDecode(const std::string &path) const
{
    return endsWithIgnoreCase(path, ".yuv") || endsWithIgnoreCase(path, ".y4m");
}

std::vector<std::string> YuvDecoder::extensions() const
{
    return {"yuv", "y4m"};
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
        YuvSourceInfo info;
        std::shared_ptr<std::ifstream> file;
        if (!openSource(path, info, file) || !file)
        {
            return Result::err(makeError(DecodeErrorCode::UnsupportedFormat,
                                         std::string("YUV parameters unknown: ") + info.why, path));
        }
        YuvPlanes planes;
        std::string why;
        const std::shared_ptr<std::ifstream> opened = file;
        const ReadAtFn read = [opened](int64_t offset, uint8_t *buf, size_t n)
        { return readAt(opened, offset, buf, n); };
        if (!mviewer::core::yuv::readYuvFrame(info, 0, read, planes, &why))
        {
            const std::string message = why.empty() ? std::string("YUV frame read failed") : why;
            return Result::err(makeError(DecodeErrorCode::CorruptData, message, path));
        }
        ImageData image = mviewer::core::yuv::renderYuvImage(planes, info.format, 0);
        if (image.isNull())
        {
            return Result::err(
                makeError(DecodeErrorCode::CorruptData, "YUV conversion failed", path));
        }
        fillMeta(info, path, fileSizeOf(path), outMeta);
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
    {
        return ImageData{};
    }
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
        {
            return decodeFull(path, outMeta);
        }
        YuvSourceInfo info;
        std::shared_ptr<std::ifstream> file;
        if (!openSource(path, info, file) || !file)
        {
            return ImageData{};
        }
        YuvPlanes planes;
        const std::shared_ptr<std::ifstream> opened = file;
        const ReadAtFn read = [opened](int64_t offset, uint8_t *buf, size_t n)
        { return readAt(opened, offset, buf, n); };
        if (!mviewer::core::yuv::readYuvFrame(info, 0, read, planes, nullptr))
        {
            return ImageData{};
        }
        ImageData image = mviewer::core::yuv::renderYuvImage(planes, info.format, maxEdge);
        if (image.isNull())
        {
            return ImageData{};
        }
        fillMeta(info, path, fileSizeOf(path), outMeta);
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
        YuvSourceInfo info;
        std::shared_ptr<std::ifstream> file;
        if (!openSource(path, info, file))
        {
            return false;
        }
        fillMeta(info, path, fileSizeOf(path), meta);
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
