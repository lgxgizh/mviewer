#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// YUV4MPEG2 stream and frame headers. Pixel layout is always planar, little-endian,
// and not MSB-aligned. The stream has no matrix field; parsed formats use BT.709.

namespace mviewer::core::yuv
{

constexpr size_t kMaxY4mHeaderBytes = 4096;

struct Y4mInfo
{
    Format format;
    int64_t headerBytes = 0; // stream header length, including the terminating '\n'
    int fpsNum = 0;
    int fpsDen = 0;
    int parNum = 0;
    int parDen = 0;
    char interlace = 'p';  // p, t, b, m, or '?' when the I tag is absent or not a single ptbm char
    std::string chromaTag; // raw C value; "420jpeg" when the C tag is absent
    bool colorRangeExplicit = false;
};

// Parses the stream header at the start of 'data'. Fails when the magic, newline window,
// required W/H, a duplicate W/H/C, an unknown C tag, an embedded NUL, or isValid() fails.
// 444alpha fails with why="alpha not supported". Malformed F/A values are ignored.
std::optional<Y4mInfo> parseY4mHeader(const uint8_t *data, size_t size, std::string *why = nullptr);

// "FRAME" plus optional space-separated parameters and a terminating '\n', inside the first
// min(size, kMaxY4mHeaderBytes) bytes. Returns that length, or -1 when malformed or truncated.
int64_t parseY4mFrameHeader(const uint8_t *data, size_t size);

// Reads up to n bytes at an absolute file offset. Returns the count read, or 0 on EOF/error.
using ReadAtFn = std::function<size_t(int64_t offset, uint8_t *buf, size_t n)>;

// Absolute offsets of each frame's pixel data (the byte after its FRAME header). Stops on the
// first malformed frame header, or when pixel data would extend past fileSize. Caps at maxFrames.
// Reads at most kMaxY4mHeaderBytes per frame header. Frame size is computeFrameLayout().frameBytes.
std::vector<int64_t> indexY4mFrames(const Y4mInfo &info, int64_t fileSize, const ReadAtFn &read,
                                    size_t maxFrames = 1000000);

// "YUV4MPEG2 W<w> H<h> F<n>:<d> Ip A1:1 C<tag>[ XCOLORRANGE=FULL]\n", or "" when 'f' has no
// y4m chroma tag (non-planar, packed, VU order, big-endian, MSB-aligned, or bit depth outside
// {8, 10, 12, 16}, or a 4:2:0 siting/depth pair the C tag cannot express).
std::string makeY4mHeader(const Format &f, int fpsNum = 25, int fpsDen = 1);

} // namespace mviewer::core::yuv
