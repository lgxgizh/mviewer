#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// YUV frame geometry and FFmpeg-style pixel-format names.
// Byte sizes follow ITU-R BT.601 / BT.709 / BT.2020 sampling (4:4:4, 4:2:2, 4:2:0, 4:0:0).
// Odd chroma dimensions round up. No row or plane padding is inserted.

namespace mviewer::core::yuv
{

enum class Layout : std::uint8_t
{
    Planar,
    SemiPlanar,
    Packed
};

enum class Subsampling : std::uint8_t
{
    S444,
    S422,
    S420,
    S400
};

enum class PlaneOrder : std::uint8_t
{
    UV,
    VU
};

enum class Packing : std::uint8_t
{
    None,
    YUYV,
    UYVY,
    YVYU
};

enum class Matrix : std::uint8_t
{
    BT601,
    BT709,
    BT2020
};

enum class Range : std::uint8_t
{
    Limited,
    Full
};

enum class ChromaSiting : std::uint8_t
{
    Left,
    Center,
    TopLeft
};

enum class Endian : std::uint8_t
{
    Little,
    Big
};

constexpr int kMaxDimension = 65536;
constexpr int64_t kMaxFrameBytes = int64_t(1) << 30;

struct Format
{
    int width = 0;
    int height = 0;
    Layout layout = Layout::Planar;
    Subsampling subsampling = Subsampling::S420;
    PlaneOrder order = PlaneOrder::UV;
    Packing packing = Packing::None;
    int bitDepth = 8;
    bool msbAligned = false;
    Endian endian = Endian::Little;
    Matrix matrix = Matrix::BT709;
    Range range = Range::Limited;
    ChromaSiting siting = ChromaSiting::Left;

    bool operator==(const Format &) const = default;
};

int bytesPerSample(const Format &f);
int chromaShiftX(Subsampling s);
int chromaShiftY(Subsampling s);
int chromaWidth(const Format &f);
int chromaHeight(const Format &f);

// False on signed overflow. checkedMul is also false when either input is negative.
// On failure, out is left unchanged.
bool checkedMul(int64_t a, int64_t b, int64_t &out);
bool checkedAdd(int64_t a, int64_t b, int64_t &out);

struct PlaneGeometry
{
    int width = 0;
    int height = 0;
    int64_t rowBytes = 0;
    int64_t offset = 0;
    int64_t bytes = 0;
};

// Planar: [0] Y, [1] first chroma in file order, [2] second chroma (VU stores V then U).
// Semi-planar: [0] Y, [1] interleaved chroma (width = chromaWidth, rowBytes = width * 2 * bps).
// Packed 4:2:2: [0] holds the frame, rowBytes = ceil(width / 2) * 4 * bps. S400: one plane.
struct FrameLayout
{
    int planeCount = 0;
    std::array<PlaneGeometry, 3> planes{};
    int64_t frameBytes = 0;
};

std::optional<FrameLayout> computeFrameLayout(const Format &f, std::string *why = nullptr);
bool isValid(const Format &f, std::string *why = nullptr);

std::string pixFmtName(const Format &f);
bool applyPixFmtName(std::string_view name, Format &inout);

std::string_view matrixName(Matrix m);
std::string_view rangeName(Range r);
std::string_view sitingName(ChromaSiting s);
bool parseMatrix(std::string_view text, Matrix &out);
bool parseRange(std::string_view text, Range &out);
bool parseSiting(std::string_view text, ChromaSiting &out);

std::string toString(const Format &f);
std::optional<Format> parseFormatString(std::string_view text);

struct FrameCount
{
    int64_t frames = 0;
    int64_t trailingBytes = 0;
};

FrameCount computeFrameCount(int64_t fileSize, int64_t headerBytes, int64_t perFrameOverhead,
                             int64_t frameBytes);

} // namespace mviewer::core::yuv
