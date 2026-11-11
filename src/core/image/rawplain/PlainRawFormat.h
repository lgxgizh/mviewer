#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ADR 033: parameters of a headerless sensor dump (.raw): Bayer or mono
// samples, no container. Qt-free, never throws, no UB on any input.

namespace mviewer::core::rawplain
{

inline constexpr int kMaxDimension = 65536;
inline constexpr int64_t kMaxPixels = int64_t{1} << 28; // 268M samples

enum class Packing : std::uint8_t
{
    Bits8,  // one byte per sample (bitDepth 8)
    Lsb16,  // 16-bit container, value in the low bits (default, ADR 033 1A)
    Msb16,  // 16-bit container, value in the high bits
    Mipi10, // MIPI CSI-2 RAW10: 4 samples in 5 bytes (bitDepth 10)
    Mipi12  // MIPI CSI-2 RAW12: 2 samples in 3 bytes (bitDepth 12)
};

enum class ByteOrder : std::uint8_t
{
    Little,
    Big
};

// Colour of the top-left 2x2 quad, row-major. Mono = no colour filter.
enum class Cfa : std::uint8_t
{
    RGGB,
    BGGR,
    GRBG,
    GBRG,
    Mono
};

enum class WhiteBalance : std::uint8_t
{
    None,       // unit gains
    GrayWorld,  // channel means equalised to green
    WhitePatch, // brightest non-clipped region equalised to green
    Manual      // wbGainR / wbGainB
};

struct Params
{
    int width = 0;  // 0 = take from the file name
    int height = 0; // 0 = take from the file name
    int bitDepth = 10;
    Packing packing = Packing::Lsb16;
    ByteOrder byteOrder = ByteOrder::Little; // 16-bit containers only
    int64_t headerOffset = 0;                // bytes skipped at file start
    int64_t rowStride = 0;                   // bytes per row; 0 = minRowBytes()
    int blackLevel = 0;                      // in bitDepth units
    int whiteLevel = 0;                      // 0 = (1 << bitDepth) - 1
    Cfa cfa = Cfa::RGGB;
    WhiteBalance wb = WhiteBalance::GrayWorld;
    double wbGainR = 1.0; // Manual only, range [0.1, 16]
    double wbGainB = 1.0; // Manual only, range [0.1, 16]
    double gain = 1.0;    // digital gain, range [0.125, 64]; default 1x (ADR 033 2B)
    bool srgbGamma = true;

    bool operator==(const Params &) const = default;
};

// Bytes holding one row of samples, without padding. -1 when width <= 0 or
// the packing does not match bitDepth.
int64_t minRowBytes(const Params &p);
// rowStride when > 0, else minRowBytes(p).
int64_t effectiveRowStride(const Params &p);
// whiteLevel when > 0, else (1 << bitDepth) - 1.
int effectiveWhiteLevel(const Params &p);
// headerOffset + effectiveRowStride * height (checked int64); -1 when invalid.
int64_t frameBytes(const Params &p);

// Packing/bitDepth pairs: Bits8 -> 8; Mipi10 -> 10; Mipi12 -> 12;
// Lsb16/Msb16 -> 8..16.
bool packingSupportsBitDepth(Packing packing, int bitDepth);

// Width and height set and in [1, kMaxDimension], width*height <= kMaxPixels,
// packing/bitDepth compatible, headerOffset >= 0, rowStride 0 or >=
// minRowBytes, 0 <= blackLevel < effectiveWhiteLevel <= (1 << bitDepth) - 1,
// gains finite and in range. *why gets a short English reason when false.
bool isValid(const Params &p, std::string *why = nullptr);
// isValid() ignoring width/height (for global defaults where size comes from
// the file name).
bool isValidDefaults(const Params &p, std::string *why = nullptr);

// Canonical text, e.g.
// "w=4096;h=3072;bits=10;pack=lsb16;order=le;offset=0;stride=0;black=64;white=0;cfa=rggb;wb=gray;wbr=1;wbb=1;gain=1;gamma=1"
// Tokens: pack bits8|lsb16|msb16|mipi10|mipi12, order le|be,
// cfa rggb|bggr|grbg|gbrg|mono, wb none|gray|patch|manual. Doubles printed
// with up to 4 decimals and no trailing zeros.
std::string toString(const Params &p);
// Inverse of toString. Keys may appear in any order; missing keys keep their
// Params{} default; unknown keys, duplicate keys, bad numbers or bad tokens ->
// nullopt. Does not call isValid().
std::optional<Params> parseParams(std::string_view text);

// First "<W>x<H>" in the name (case-insensitive 'x', also U+00D7 in UTF-8),
// where W and H are 1..5 digit numbers not adjacent to other digits and both
// in [16, kMaxDimension]. Equivalent to the regex
// (?<![0-9])([0-9]{2,5})[xX\u00D7]([0-9]{2,5})(?![0-9]).
std::optional<std::pair<int, int>> sizeFromFileName(std::string_view fileName);

enum class SizeCheck : std::uint8_t
{
    Invalid,  // params not valid
    TooSmall, // file shorter than frameBytes
    Exact,    // file size == frameBytes
    Larger    // file has trailing bytes
};
SizeCheck checkFileSize(const Params &p, int64_t fileSize);

// Variants of 'base' (same size, offset, black level, CFA, WB, gain) whose
// frameBytes == fileSize exactly, rowStride forced to 0, in this order:
// base itself; Lsb16 with base.bitDepth (or 10 when base is Bits8);
// Mipi10/10; Mipi12/12; Bits8/8; Lsb16 with 12; Lsb16 with 16.
// Duplicates removed. When the packing/bitDepth changes, blackLevel is
// rescaled by the bit-depth difference (shift) and whiteLevel reset to 0.
std::vector<Params> guessFromFileSize(const Params &base, int64_t fileSize);

// Effective params for one file from the global defaults: width/height from
// the file name when the defaults have 0; then, if checkFileSize is not Exact
// and guessFromFileSize has a candidate, the first candidate is used.
struct Resolved
{
    Params params;
    bool known = false;        // isValid && checkFileSize is Exact or Larger
    bool sizeFromName = false; // width/height came from the file name
    bool guessed = false;      // packing/bitDepth came from guessFromFileSize
    SizeCheck check = SizeCheck::Invalid;
    int64_t expectedBytes = -1;
    std::string why; // English reason when !known
};
Resolved resolveForFile(const Params &defaults, std::string_view fileName, int64_t fileSize);

} // namespace mviewer::core::rawplain
