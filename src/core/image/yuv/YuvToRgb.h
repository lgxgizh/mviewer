#pragma once

#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// YUV -> 8-bit RGB conversion (ITU-R BT.601 / BT.709 / BT.2020 non-constant
// luminance). Integer Q16 math on 12-bit normalized samples. The scalar row
// converter is the bit-exact reference that any SIMD path must reproduce.
// BT.2020 is SDR Y'CbCr only: those luma coefficients, quantized to 8-bit RGB,
// with no PQ/HLG transfer and no HDR tone map.

namespace mviewer::core::yuv
{

enum class ChromaUpsample : std::uint8_t
{
    Nearest,
    Bilinear
};

struct YuvCoeffs
{
    int32_t cy = 0;
    int32_t crv = 0;
    int32_t cgu = 0;
    int32_t cgv = 0;
    int32_t cbu = 0;
    int32_t yOff = 0;
    int32_t cOff = 0;
};

// Full range scales by the source code range (8-bit white 255 -> 4080 in
// 12-bit units, 10-bit 1023 -> 4092), so 'bitDepth' matters only for Full.
// Matrix::BT2020 uses the BT.2020 Kr/Kb on that SDR Y'CbCr (no PQ/HLG, no HDR
// tone map). The display path tags the resulting RGB as Rec.2020 separately.
YuvCoeffs makeYuvCoeffs(Matrix m, Range r, int bitDepth = 8);

// Normalizes a sample of 'bitDepth' bits (1..16) to 12 bits.
int16_t toTwelveBit(uint16_t sample, int bitDepth);

// One row of n pixels. y12/u12/v12 hold 12-bit samples with chroma already at
// luma resolution. u12 or v12 == nullptr converts as gray (R = G = B).
void convertRowScalar(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                      const YuvCoeffs &c, uint8_t *rgb24);

// Bit-exact with convertRowScalar. On non-x86, or when built without that ISA,
// each falls back to convertRowScalar.
void convertRowSse2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24);
void convertRowAvx2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24);

using RowConverterFn = void (*)(const int16_t *, const int16_t *, const int16_t *, int,
                                const YuvCoeffs &, uint8_t *);

enum class RowConverterKind : std::uint8_t
{
    Scalar,
    Sse2,
    Avx2
};

// Scalar when this CPU cannot run the requested kind.
RowConverterFn rowConverterFor(RowConverterKind kind);

// Avx2, else Sse2, else Scalar.
RowConverterKind bestRowConverterKind();

// Returns the fastest available row converter.
RowConverterFn selectRowConverter();

// Runs fn over row bands [y0, y1) that together cover [0, rowCount), possibly on
// several threads, and returns when every band is done. fn must not throw.
using YuvRowsRunner = void (*)(int rowCount, const std::function<void(int, int)> &fn);

// Installs the runner convertToRgb24 uses for large frames (nullptr = serial, the
// default). The yuv module stays std-only; the app side installs a thread pool.
void setYuvRowsRunner(YuvRowsRunner runner) noexcept;
YuvRowsRunner yuvRowsRunner() noexcept;

// Converts a whole frame into RGB24 rows of dstStride bytes (>= width * 3).
// False when the planes do not match the format or dst is too small.
bool convertToRgb24(const YuvPlanes &p, const Format &f, ChromaUpsample up, uint8_t *dst,
                    size_t dstSize, size_t dstStride);

// Process-wide chroma upsample for production decoders (FrameSequence,
// YuvDecoder). Initial value is Bilinear. Thread-safe.
// Independent of toRgb24's parameter default: toRgb24(planes, format) is always
// Bilinear unless the caller passes an upsample, even after
// setDefaultChromaUpsample(Nearest).
ChromaUpsample defaultChromaUpsample() noexcept;
void setDefaultChromaUpsample(ChromaUpsample up) noexcept;

// Tightly packed width * height * 3 bytes, or empty on failure.
// The upsample default is Bilinear and does not follow defaultChromaUpsample().
std::vector<uint8_t> toRgb24(const YuvPlanes &p, const Format &f,
                             ChromaUpsample up = ChromaUpsample::Bilinear);

} // namespace mviewer::core::yuv
