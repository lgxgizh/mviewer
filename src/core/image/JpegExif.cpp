#include "core/image/JpegExif.h"

#include <cstring>

namespace mviewer::core
{
namespace
{

struct Marker
{
    size_t ff = 0;
    uint8_t code = 0;
    size_t content = 0;
    size_t end = 0;
    bool payload = false;
};

struct ExifSegment
{
    size_t content = 0;
    size_t tiff = 0;
    size_t end = 0;
    size_t ff = 0;
};

enum class ByteOrder : std::uint8_t
{
    Little,
    Big
};

bool readBe16(const std::vector<uint8_t> &bytes, size_t index, uint16_t &out)
{
    if (index + 1 >= bytes.size())
        return false;
    out = static_cast<uint16_t>((static_cast<uint16_t>(bytes[index]) << 8) |
                                static_cast<uint16_t>(bytes[index + 1]));
    return true;
}

bool readU16(const std::vector<uint8_t> &bytes, size_t index, ByteOrder order, uint16_t &out)
{
    if (index + 1 >= bytes.size())
        return false;
    if (order == ByteOrder::Little)
    {
        out = static_cast<uint16_t>(bytes[index] |
                                    (static_cast<uint16_t>(bytes[index + 1]) << 8));
    }
    else
    {
        out = static_cast<uint16_t>((static_cast<uint16_t>(bytes[index]) << 8) | bytes[index + 1]);
    }
    return true;
}

bool readU32(const std::vector<uint8_t> &bytes, size_t index, ByteOrder order, uint32_t &out)
{
    if (index + 3 >= bytes.size())
        return false;
    if (order == ByteOrder::Little)
    {
        out = static_cast<uint32_t>(bytes[index]) |
              (static_cast<uint32_t>(bytes[index + 1]) << 8) |
              (static_cast<uint32_t>(bytes[index + 2]) << 16) |
              (static_cast<uint32_t>(bytes[index + 3]) << 24);
    }
    else
    {
        out = (static_cast<uint32_t>(bytes[index]) << 24) |
              (static_cast<uint32_t>(bytes[index + 1]) << 16) |
              (static_cast<uint32_t>(bytes[index + 2]) << 8) |
              static_cast<uint32_t>(bytes[index + 3]);
    }
    return true;
}

bool writeU16(std::vector<uint8_t> &bytes, size_t index, ByteOrder order, uint16_t value)
{
    if (index + 1 >= bytes.size())
        return false;
    if (order == ByteOrder::Little)
    {
        bytes[index] = static_cast<uint8_t>(value & 0xFF);
        bytes[index + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }
    else
    {
        bytes[index] = static_cast<uint8_t>((value >> 8) & 0xFF);
        bytes[index + 1] = static_cast<uint8_t>(value & 0xFF);
    }
    return true;
}

bool startsWith(const std::vector<uint8_t> &bytes, size_t index, size_t end, const char *text,
                size_t length)
{
    if (length > end || index > end - length || index + length > bytes.size())
        return false;
    return std::memcmp(bytes.data() + index, text, length) == 0;
}

bool nextMarker(const std::vector<uint8_t> &jpeg, size_t pos, Marker &marker)
{
    if (pos >= jpeg.size())
        return false;
    while (pos + 1 < jpeg.size() && jpeg[pos] == 0xFF && jpeg[pos + 1] == 0xFF)
        ++pos;
    if (pos + 1 >= jpeg.size() || jpeg[pos] != 0xFF)
        return false;

    marker.ff = pos;
    marker.code = jpeg[pos + 1];
    if (marker.code == 0x00)
        return false;

    const bool standalone =
        marker.code == 0x01 || (marker.code >= 0xD0 && marker.code <= 0xD9);
    if (standalone)
    {
        marker.payload = false;
        marker.content = pos + 2;
        marker.end = pos + 2;
        return true;
    }
    uint16_t length = 0;
    if (!readBe16(jpeg, pos + 2, length) || length < 2)
        return false;
    const size_t end = pos + 2 + static_cast<size_t>(length);
    if (end > jpeg.size())
        return false;
    marker.payload = true;
    marker.content = pos + 4;
    marker.end = end;
    return true;
}

bool isExifApp1(const std::vector<uint8_t> &jpeg, const Marker &marker)
{
    return marker.code == 0xE1 && marker.payload &&
           startsWith(jpeg, marker.content, marker.end, "Exif\0\0", 6);
}

bool isJfifApp0(const std::vector<uint8_t> &jpeg, const Marker &marker)
{
    return marker.code == 0xE0 && marker.payload &&
           startsWith(jpeg, marker.content, marker.end, "JFIF\0", 5);
}

bool findExifApp1(const std::vector<uint8_t> &jpeg, ExifSegment &segment)
{
    size_t pos = 0;
    if (jpeg.size() >= 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8)
        pos = 2;
    while (pos < jpeg.size())
    {
        Marker marker;
        if (!nextMarker(jpeg, pos, marker))
            return false;
        if (marker.code == 0xDA || marker.code == 0xD9)
            return false;
        if (isExifApp1(jpeg, marker))
        {
            if (marker.content + 6 > marker.end)
                return false;
            segment.ff = marker.ff;
            segment.content = marker.content;
            segment.tiff = marker.content + 6;
            segment.end = marker.end;
            return true;
        }
        pos = marker.end;
    }
    return false;
}

bool offsetInside(size_t base, uint32_t offset, size_t limit)
{
    if (base > limit)
        return false;
    return static_cast<size_t>(offset) <= limit - base;
}

struct IfdView
{
    size_t ifd = 0;
    uint16_t count = 0;
    ByteOrder order = ByteOrder::Little;
};

bool openIfd0(const std::vector<uint8_t> &jpeg, const ExifSegment &segment, IfdView &view)
{
    if (segment.tiff + 8 > segment.end || segment.end > jpeg.size())
        return false;
    if (jpeg[segment.tiff] == 'I' && jpeg[segment.tiff + 1] == 'I')
        view.order = ByteOrder::Little;
    else if (jpeg[segment.tiff] == 'M' && jpeg[segment.tiff + 1] == 'M')
        view.order = ByteOrder::Big;
    else
        return false;

    uint16_t magic = 0;
    if (!readU16(jpeg, segment.tiff + 2, view.order, magic) || magic != 42)
        return false;
    uint32_t ifdOffset = 0;
    if (!readU32(jpeg, segment.tiff + 4, view.order, ifdOffset))
        return false;
    if (!offsetInside(segment.tiff, ifdOffset, segment.end))
        return false;
    const size_t ifd = segment.tiff + static_cast<size_t>(ifdOffset);
    if (segment.end - ifd < 6)
        return false;
    uint16_t count = 0;
    if (!readU16(jpeg, ifd, view.order, count))
        return false;
    if (count > (segment.end - ifd - 2) / 12)
        return false;
    const size_t entriesEnd = ifd + 2 + static_cast<size_t>(count) * 12;
    if (segment.end - entriesEnd < 4)
        return false;
    view.ifd = ifd;
    view.count = count;
    return true;
}

// Locate the inline SHORT value of IFD0 tag 0x0112. On success `valueIndex`
// is the first of the two endian-dependent value bytes. The buffer is not
// modified. A missing tag or a value outside 1..8 returns false.
bool findOrientationValue(const std::vector<uint8_t> &jpeg, const ExifSegment &segment,
                          size_t &valueIndex, ByteOrder &order, int *orientation)
{
    IfdView view;
    if (!openIfd0(jpeg, segment, view))
        return false;
    order = view.order;
    for (uint16_t i = 0; i < view.count; ++i)
    {
        const size_t entry = view.ifd + 2 + static_cast<size_t>(i) * 12;
        uint16_t tag = 0;
        uint16_t type = 0;
        uint32_t number = 0;
        if (!readU16(jpeg, entry, view.order, tag) || !readU16(jpeg, entry + 2, view.order, type) ||
            !readU32(jpeg, entry + 4, view.order, number))
            return false;
        if (tag != 0x0112)
            continue;
        if (type != 3 || number != 1)
            return false;
        uint16_t value = 0;
        if (!readU16(jpeg, entry + 8, view.order, value))
            return false;
        if (value < 1 || value > 8)
            return false;
        valueIndex = entry + 8;
        if (orientation)
            *orientation = static_cast<int>(value);
        return true;
    }
    return false;
}

bool isSof(uint8_t code)
{
    return code >= 0xC0 && code <= 0xCF && code != 0xC4 && code != 0xC8 && code != 0xCC;
}

const int kRot90[9] = {0, 6, 7, 8, 5, 2, 3, 4, 1};
const int kRot180[9] = {0, 3, 4, 1, 2, 7, 8, 5, 6};
const int kRot270[9] = {0, 8, 5, 6, 7, 4, 1, 2, 3};
const int kFlipH[9] = {0, 2, 1, 4, 3, 6, 5, 8, 7};
const int kFlipV[9] = {0, 4, 3, 2, 1, 8, 7, 6, 5};

int normalizedOrientation(int current)
{
    if (current < 1 || current > 8)
        return 1;
    return current;
}

} // namespace

std::optional<int> readJpegOrientation(const std::vector<uint8_t> &jpeg)
{
    ExifSegment segment;
    if (!findExifApp1(jpeg, segment))
        return std::nullopt;
    size_t valueIndex = 0;
    ByteOrder order = ByteOrder::Little;
    int orientation = 0;
    if (!findOrientationValue(jpeg, segment, valueIndex, order, &orientation) || orientation == 0)
        return std::nullopt;
    return orientation;
}

bool writeJpegOrientationInPlace(std::vector<uint8_t> &jpeg, int orientation)
{
    if (orientation < 1 || orientation > 8)
        return false;
    ExifSegment segment;
    if (!findExifApp1(jpeg, segment))
        return false;
    size_t valueIndex = 0;
    ByteOrder order = ByteOrder::Little;
    if (!findOrientationValue(jpeg, segment, valueIndex, order, nullptr))
        return false;
    return writeU16(jpeg, valueIndex, order, static_cast<uint16_t>(orientation));
}

std::vector<uint8_t> extractJpegApp1Exif(const std::vector<uint8_t> &jpeg)
{
    ExifSegment segment;
    if (!findExifApp1(jpeg, segment))
        return {};
    if (segment.ff >= segment.end || segment.end > jpeg.size())
        return {};
    return std::vector<uint8_t>(jpeg.begin() + static_cast<std::ptrdiff_t>(segment.ff),
                                jpeg.begin() + static_cast<std::ptrdiff_t>(segment.end));
}

std::vector<uint8_t> spliceJpegApp1Exif(const std::vector<uint8_t> &encoded,
                                        const std::vector<uint8_t> &app1, bool resetOrientationTo1)
{
    if (encoded.size() < 2 || encoded[0] != 0xFF || encoded[1] != 0xD8)
        return encoded;
    if (app1.empty())
        return encoded;

    std::vector<uint8_t> segment = app1;
    if (resetOrientationTo1)
        writeJpegOrientationInPlace(segment, 1);

    struct Piece
    {
        size_t begin = 0;
        size_t end = 0;
        bool exif = false;
        bool jfif = false;
    };
    std::vector<Piece> pieces;
    size_t pos = 2;
    size_t tail = encoded.size();
    while (pos < encoded.size())
    {
        Marker marker;
        if (!nextMarker(encoded, pos, marker))
        {
            tail = pos;
            break;
        }
        if (marker.ff > pos)
            pieces.push_back(Piece{pos, marker.ff, false, false});
        if (marker.code == 0xDA || marker.code == 0xD9)
        {
            tail = marker.ff;
            break;
        }
        pieces.push_back(Piece{marker.ff, marker.end, isExifApp1(encoded, marker),
                               isJfifApp0(encoded, marker)});
        pos = marker.end;
    }

    bool hasJfif = false;
    for (const Piece &piece : pieces)
    {
        if (piece.jfif)
            hasJfif = true;
    }

    std::vector<uint8_t> out;
    out.reserve(encoded.size() + segment.size());
    out.push_back(0xFF);
    out.push_back(0xD8);
    bool inserted = false;
    if (!hasJfif)
    {
        out.insert(out.end(), segment.begin(), segment.end());
        inserted = true;
    }
    for (const Piece &piece : pieces)
    {
        if (piece.exif)
            continue;
        out.insert(out.end(), encoded.begin() + static_cast<std::ptrdiff_t>(piece.begin),
                   encoded.begin() + static_cast<std::ptrdiff_t>(piece.end));
        if (piece.jfif && !inserted)
        {
            out.insert(out.end(), segment.begin(), segment.end());
            inserted = true;
        }
    }
    if (!inserted)
        out.insert(out.end(), segment.begin(), segment.end());
    if (tail < encoded.size())
        out.insert(out.end(), encoded.begin() + static_cast<std::ptrdiff_t>(tail), encoded.end());
    return out;
}

int composeOrientation(int current, FilePixelOp op, int degreesCw)
{
    const int orientation = normalizedOrientation(current);
    if (op == FilePixelOp::FlipH)
        return kFlipH[orientation];
    if (op == FilePixelOp::FlipV)
        return kFlipV[orientation];

    int degrees = degreesCw % 360;
    if (degrees < 0)
        degrees += 360;
    if (degrees % 90 != 0)
        return orientation;
    if (degrees == 90)
        return kRot90[orientation];
    if (degrees == 180)
        return kRot180[orientation];
    if (degrees == 270)
        return kRot270[orientation];
    return orientation;
}

bool readJpegStoredSize(const std::vector<uint8_t> &jpeg, int &width, int &height)
{
    width = 0;
    height = 0;
    if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
        return false;
    size_t pos = 2;
    while (pos < jpeg.size())
    {
        Marker marker;
        if (!nextMarker(jpeg, pos, marker))
            return false;
        if (marker.code == 0xDA || marker.code == 0xD9)
            return false;
        if (isSof(marker.code))
        {
            if (marker.content + 5 > marker.end)
                return false;
            uint16_t storedHeight = 0;
            uint16_t storedWidth = 0;
            if (!readBe16(jpeg, marker.content + 1, storedHeight) ||
                !readBe16(jpeg, marker.content + 3, storedWidth))
                return false;
            if (storedWidth == 0 || storedHeight == 0)
                return false;
            width = storedWidth;
            height = storedHeight;
            return true;
        }
        pos = marker.end;
    }
    return false;
}

} // namespace mviewer::core
