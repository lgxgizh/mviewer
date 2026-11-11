#pragma once

// TIFF / EXIF IFD reading primitives for RawMetadata.cpp (split out to keep
// that TU under the 800-line cap). Internal header: include only from
// src/core/image/RawMetadata*.cpp and the RAW fuzz target.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace mviewer::core::raw_tiff
{

// Subtraction form prevents 32-bit/64-bit integer overflow when checking buffer bounds.
inline bool fits(uint64_t offset, uint64_t need, uint64_t size)
{
    return offset <= size && need <= size - offset;
}

// 64-bit file positions. `long` is 32-bit on MSVC, so fseek/ftell wrap or fail
// past 2 GiB (large DNG / scanned TIFF lost every field).
inline int seek64(FILE *f, int64_t pos, int whence)
{
#ifdef _WIN32
    return _fseeki64(f, pos, whence);
#else
    return fseeko(f, static_cast<off_t>(pos), whence);
#endif
}

inline int64_t tell64(FILE *f)
{
#ifdef _WIN32
    return _ftelli64(f);
#else
    return static_cast<int64_t>(ftello(f));
#endif
}

// TIFF offsets are relative to the TIFF header. For RAW files that header is
// the start of the file; for JPEG it sits inside the APP1 "Exif" segment, so
// every offset seek is shifted by the header's file position.
thread_local int64_t t_tiffBase = 0;

inline int seekTiff(FILE *f, uint64_t offset)
{
    return seek64(f, t_tiffBase + static_cast<int64_t>(offset), SEEK_SET);
}

struct TiffHeader
{
    bool littleEndian = false;
    uint32_t ifdOffset = 0;
};

inline bool readTiffHeader(FILE *f, uint64_t fileSize, TiffHeader &h)
{
    if (fileSize < 8)
        return false;
    uint8_t buf[8];
    if (std::fread(buf, 1, 8, f) != 8)
        return false;
    if (buf[0] == 'I' && buf[1] == 'I')
        h.littleEndian = true;
    else if (buf[0] == 'M' && buf[1] == 'M')
        h.littleEndian = false;
    else
        return false;
    uint16_t magic = h.littleEndian ? static_cast<uint16_t>(buf[2] | (buf[3] << 8))
                                    : static_cast<uint16_t>((buf[2] << 8) | buf[3]);
    if (magic != 42)
        return false;
    h.ifdOffset =
        h.littleEndian
            ? static_cast<uint32_t>(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24))
            : static_cast<uint32_t>((buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7]);
    return fits(h.ifdOffset, 2, fileSize);
}

// clang-analyzer-unix.Stream: never read after a failed read left the stream
// at EOF or with an error (position indeterminate).
inline bool streamOk(FILE *f)
{
    return std::feof(f) == 0 && std::ferror(f) == 0;
}

inline uint16_t readU16(FILE *f, bool le)
{
    uint8_t b[2];
    if (!streamOk(f) || std::fread(b, 1, 2, f) != 2)
        return 0;
    return le ? static_cast<uint16_t>(b[0] | (b[1] << 8))
              : static_cast<uint16_t>((b[0] << 8) | b[1]);
}

inline uint32_t readU32(FILE *f, bool le)
{
    uint8_t b[4];
    if (!streamOk(f) || std::fread(b, 1, 4, f) != 4)
        return 0;
    return le ? static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24))
              : static_cast<uint32_t>((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

struct IfdEntry
{
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    uint32_t value = 0;
    // The 4 value/offset bytes exactly as stored in the file. Values that fit
    // in 4 bytes (BYTE/ASCII count <= 4) live here in file order, so they must
    // be read from these bytes, not from `value`, which is byte-order decoded.
    uint8_t raw[4] = {0, 0, 0, 0};
};

inline bool readEntry(FILE *f, bool le, IfdEntry &e)
{
    uint8_t b[12];
    if (!streamOk(f) || std::fread(b, 1, 12, f) != 12)
        return false;
    std::memcpy(e.raw, b + 8, 4);
    if (le)
    {
        e.tag = static_cast<uint16_t>(b[0] | (b[1] << 8));
        e.type = static_cast<uint16_t>(b[2] | (b[3] << 8));
        e.count = static_cast<uint32_t>(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24));
        e.value = static_cast<uint32_t>(b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24));
    }
    else
    {
        e.tag = static_cast<uint16_t>((b[0] << 8) | b[1]);
        e.type = static_cast<uint16_t>((b[2] << 8) | b[3]);
        e.count = static_cast<uint32_t>((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]);
        e.value = static_cast<uint32_t>((b[8] << 24) | (b[9] << 16) | (b[10] << 8) | b[11]);
    }
    return true;
}

inline std::string readString(FILE *f, uint64_t fileSize, const IfdEntry &e)
{
    const uint32_t count = e.count;
    if (count == 0 || count > 4096)
        return "";
    std::string s(count, '\0');
    if (count <= 4)
    {
        // TIFF 6.0: values of 4 bytes or fewer are stored inline.
        std::memcpy(s.data(), e.raw, count);
    }
    else
    {
        if (!fits(e.value, count, fileSize))
            return "";
        const int64_t cur = tell64(f);
        if (seekTiff(f, e.value) != 0)
            return "";
        const size_t readCount = std::fread(s.data(), 1, count, f);
        seek64(f, cur, SEEK_SET);
        if (readCount != count)
            return "";
    }
    while (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

inline uint16_t readTagU16(FILE *f, bool le, uint64_t fileSize, uint32_t v, uint16_t type,
                           uint32_t count)
{
    if (type == 3 && count <= 2)
        return le ? static_cast<uint16_t>(v & 0xFFFF) : static_cast<uint16_t>((v >> 16) & 0xFFFF);
    if (!fits(v, 2, fileSize))
        return 0;
    const int64_t cur = tell64(f);
    if (seekTiff(f, v) != 0)
        return 0;
    const uint16_t r = readU16(f, le);
    seek64(f, cur, SEEK_SET);
    return r;
}

inline uint32_t readTagU32(FILE *f, bool le, uint64_t fileSize, uint32_t v, uint16_t type,
                           uint32_t count)
{
    if (type == 4 && count == 1)
        return v;
    if (!fits(v, 4, fileSize))
        return 0;
    const int64_t cur = tell64(f);
    if (seekTiff(f, v) != 0)
        return 0;
    const uint32_t r = readU32(f, le);
    seek64(f, cur, SEEK_SET);
    return r;
}

inline double readRational(FILE *f, bool le, uint64_t fileSize, uint32_t offset)
{
    if (!fits(offset, 8, fileSize))
        return 0.0;
    const int64_t cur = tell64(f);
    if (seekTiff(f, offset) != 0)
        return 0.0;
    const uint32_t num = readU32(f, le);
    const uint32_t den = readU32(f, le);
    seek64(f, cur, SEEK_SET);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

// SRATIONAL (type 10): two signed 32-bit values.
inline double readSRational(FILE *f, bool le, uint64_t fileSize, uint32_t offset)
{
    if (!fits(offset, 8, fileSize))
        return 0.0;
    const int64_t cur = tell64(f);
    if (seekTiff(f, offset) != 0)
        return 0.0;
    const auto num = static_cast<int32_t>(readU32(f, le));
    const auto den = static_cast<int32_t>(readU32(f, le));
    seek64(f, cur, SEEK_SET);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

} // namespace mviewer::core::raw_tiff
