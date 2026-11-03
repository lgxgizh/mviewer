#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Unpacks one raw YUV frame into native-resolution sample planes.

namespace mviewer::core::yuv
{

// Samples are raw code values (0 .. 2^bitDepth - 1), already de-endianed and,
// for MSB-aligned formats such as P010, shifted down. Chroma planes keep their
// subsampled size. Planes are row-major with stride == plane width.
struct YuvPlanes
{
    int width = 0;
    int height = 0;
    int chromaWidth = 0; // 0 for S400
    int chromaHeight = 0;
    int bitDepth = 8;
    std::vector<uint16_t> y;
    std::vector<uint16_t> u; // empty for S400
    std::vector<uint16_t> v;
};

// 'data' holds one frame; size must be >= computeFrameLayout(f)->frameBytes
// (extra bytes are ignored). Returns false and sets *why when the format is
// invalid or the data is too short. Never reads past data + size.
bool unpackYuvFrame(const Format &f, const uint8_t *data, size_t size, YuvPlanes &out,
                    std::string *why = nullptr);

// Plane sample with coordinates clamped into the plane (0 when it is empty).
uint16_t sampleAt(const std::vector<uint16_t> &plane, int planeW, int planeH, int x, int y);

} // namespace mviewer::core::yuv
