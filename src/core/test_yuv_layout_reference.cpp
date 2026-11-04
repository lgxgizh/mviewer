// Layout correctness against an independent reference.
//
// Synthesises random Y/U/V samples, packs them into each byte layout by hand
// (planar, semi-planar UV/VU, packed 4:2:2, 8..16 bit, LE/BE, MSB/LSB), decodes
// through the production path (resolveYuvSource + readYuvFrameImage) and checks
// every RGB pixel against a floating-point BT.601/709/2020 reference. Also:
// identical samples in NV12 / NV21 / I420 / YV12 must give byte-identical RGB,
// NV12 bytes read as I420 must not (the silent mis-decode users hit when the
// semi-planar layout is not chosen), the generic pixel-format names round-trip
// for every dialog combination, and the extension mapping drives inference.
#include "core/image/yuv/YuvExtensionMap.h"
#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvParamInference.h"
#include "core/image/yuv/YuvSource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace mviewer::core;
using namespace mviewer::core::yuv;

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

struct Samples
{
    int w = 0, h = 0, cw = 0, ch = 0;
    std::vector<uint16_t> y, u, v; // values in [0, 2^depth)
};

Samples makeSamples(const Format &f, uint32_t seed)
{
    Samples s;
    s.w = f.width;
    s.h = f.height;
    s.cw = chromaWidth(f);
    s.ch = chromaHeight(f);
    std::mt19937 rng(seed);
    const uint32_t maxV = (1u << f.bitDepth) - 1u;
    std::uniform_int_distribution<uint32_t> dist(0, maxV);
    s.y.resize(size_t(s.w) * s.h);
    for (auto &x : s.y)
        x = uint16_t(dist(rng));
    if (f.subsampling != Subsampling::S400)
    {
        s.u.resize(size_t(s.cw) * s.ch);
        s.v.resize(size_t(s.cw) * s.ch);
        for (auto &x : s.u)
            x = uint16_t(dist(rng));
        for (auto &x : s.v)
            x = uint16_t(dist(rng));
    }
    return s;
}

void putSample(std::vector<uint8_t> &dst, int64_t at, uint16_t value, const Format &f)
{
    if (f.bitDepth <= 8)
    {
        dst[size_t(at)] = uint8_t(value);
        return;
    }
    const uint16_t stored = f.msbAligned ? uint16_t(value << (16 - f.bitDepth)) : value;
    const uint8_t lo = uint8_t(stored & 0xFF), hi = uint8_t(stored >> 8);
    if (f.endian == Endian::Little)
    {
        dst[size_t(at)] = lo;
        dst[size_t(at) + 1] = hi;
    }
    else
    {
        dst[size_t(at)] = hi;
        dst[size_t(at) + 1] = lo;
    }
}

// Hand-written packer: independent of the production unpacker.
std::vector<uint8_t> pack(const Samples &s, const Format &f)
{
    const auto layout = computeFrameLayout(f);
    if (!layout)
        return {};
    std::vector<uint8_t> out(size_t(layout->frameBytes), 0);
    const int bps = f.bitDepth > 8 ? 2 : 1;
    if (f.layout == Layout::Packed)
    {
        const PlaneGeometry &p = layout->planes[0];
        for (int r = 0; r < s.h; ++r)
            for (int cx = 0; cx < s.cw; ++cx)
            {
                const uint16_t y0 = s.y[size_t(r) * s.w + 2 * cx];
                const uint16_t y1 = 2 * cx + 1 < s.w ? s.y[size_t(r) * s.w + 2 * cx + 1] : 0;
                const uint16_t u = s.u[size_t(r) * s.cw + cx], v = s.v[size_t(r) * s.cw + cx];
                uint16_t q[4];
                switch (f.packing)
                {
                case Packing::YUYV:
                    q[0] = y0, q[1] = u, q[2] = y1, q[3] = v;
                    break;
                case Packing::UYVY:
                    q[0] = u, q[1] = y0, q[2] = v, q[3] = y1;
                    break;
                default:
                    q[0] = y0, q[1] = v, q[2] = y1, q[3] = u;
                    break; // YVYU
                }
                for (int k = 0; k < 4; ++k)
                    putSample(out, p.offset + r * p.rowBytes + int64_t(4 * cx + k) * bps, q[k], f);
            }
        return out;
    }
    const PlaneGeometry &py = layout->planes[0];
    for (int r = 0; r < s.h; ++r)
        for (int c = 0; c < s.w; ++c)
            putSample(out, py.offset + r * py.rowBytes + int64_t(c) * bps, s.y[size_t(r) * s.w + c],
                      f);
    if (f.subsampling == Subsampling::S400)
        return out;
    const bool vu = f.order == PlaneOrder::VU;
    if (f.layout == Layout::SemiPlanar)
    {
        const PlaneGeometry &pc = layout->planes[1];
        for (int r = 0; r < s.ch; ++r)
            for (int c = 0; c < s.cw; ++c)
            {
                const uint16_t u = s.u[size_t(r) * s.cw + c], v = s.v[size_t(r) * s.cw + c];
                const int64_t at = pc.offset + r * pc.rowBytes + int64_t(2 * c) * bps;
                putSample(out, at, vu ? v : u, f);
                putSample(out, at + bps, vu ? u : v, f);
            }
        return out;
    }
    const PlaneGeometry &p1 = layout->planes[1], &p2 = layout->planes[2];
    for (int r = 0; r < s.ch; ++r)
        for (int c = 0; c < s.cw; ++c)
        {
            const uint16_t u = s.u[size_t(r) * s.cw + c], v = s.v[size_t(r) * s.cw + c];
            putSample(out, p1.offset + r * p1.rowBytes + int64_t(c) * bps, vu ? v : u, f);
            putSample(out, p2.offset + r * p2.rowBytes + int64_t(c) * bps, vu ? u : v, f);
        }
    return out;
}

ReadAtFn readerFor(const std::vector<uint8_t> &file)
{
    return [&file](int64_t offset, uint8_t *buf, size_t n) -> size_t
    {
        if (buf == nullptr || offset < 0 || uint64_t(offset) >= file.size())
            return 0;
        const size_t take = std::min(n, file.size() - size_t(offset));
        std::memcpy(buf, file.data() + size_t(offset), take);
        return take;
    };
}

ImageData decode(const std::vector<uint8_t> &bytes, const Format &f)
{
    const ReadAtFn read = readerFor(bytes);
    const YuvSourceInfo info =
        resolveYuvSource("frame.yuv", "", int64_t(bytes.size()), read, std::optional<Format>(f));
    if (!info.known)
        return {};
    return readYuvFrameImage(info, 0, read, 0, ChromaUpsample::Nearest);
}

// Floating-point reference (nearest chroma, i.e. sample (x >> sx, y >> sy)).
void referenceRgb(const Format &f, double Y, double U, double V, uint8_t rgb[3])
{
    double kr = 0.2126, kb = 0.0722;
    if (f.matrix == Matrix::BT601)
        kr = 0.299, kb = 0.114;
    else if (f.matrix == Matrix::BT2020)
        kr = 0.2627, kb = 0.0593;
    const double kg = 1.0 - kr - kb;
    const double scale = double(1 << (f.bitDepth - 8)); // to 8-bit units
    Y /= scale, U /= scale, V /= scale;
    double yn, cb, cr;
    if (f.range == Range::Limited)
        yn = (Y - 16.0) / 219.0, cb = (U - 128.0) / 224.0, cr = (V - 128.0) / 224.0;
    else
        yn = Y / 255.0, cb = (U - 128.0) / 255.0, cr = (V - 128.0) / 255.0;
    if (f.subsampling == Subsampling::S400)
        cb = cr = 0.0;
    const double r = yn + 2.0 * (1.0 - kr) * cr;
    const double b = yn + 2.0 * (1.0 - kb) * cb;
    const double g = (yn - kr * r - kb * b) / kg;
    const double c[3] = {r, g, b};
    for (int i = 0; i < 3; ++i)
    {
        const double v = std::lround(c[i] * 255.0);
        rgb[i] = uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

void checkAgainstReference(const char *name, Format f, uint32_t seed)
{
    const bool ok = applyPixFmtName(name, f);
    CHECK(ok);
    if (!ok)
    {
        std::printf("  (pix fmt %s)\n", name);
        return;
    }
    const Samples s = makeSamples(f, seed);
    const std::vector<uint8_t> bytes = pack(s, f);
    CHECK(!bytes.empty());
    const ImageData img = decode(bytes, f);
    CHECK(!img.isNull() && img.width == f.width && img.height == f.height &&
          img.format == PixelFormat::RGB24);
    if (img.isNull() || img.width != f.width || img.height != f.height)
    {
        std::printf("  (decode failed for %s)\n", name);
        return;
    }
    const int sx = chromaShiftX(f.subsampling), sy = chromaShiftY(f.subsampling);
    int worst = 0;
    for (int r = 0; r < f.height; ++r)
        for (int c = 0; c < f.width; ++c)
        {
            const size_t ci = size_t(r >> sy) * s.cw + size_t(c >> sx);
            const double U = s.u.empty() ? 0 : s.u[ci], V = s.v.empty() ? 0 : s.v[ci];
            uint8_t ref[3];
            referenceRgb(f, s.y[size_t(r) * s.w + c], U, V, ref);
            const uint8_t *px = img.buffer->data() + (size_t(r) * f.width + c) * 3;
            for (int k = 0; k < 3; ++k)
                worst = std::max(worst, std::abs(int(px[k]) - int(ref[k])));
        }
    // Production converts in 12-bit fixed point; one code value of rounding is allowed.
    if (worst > 1)
        std::printf("FAIL %s %dx%d: max |rgb - reference| = %d\n", name, f.width, f.height, worst);
    g_failures += worst > 1 ? 1 : 0;
}

Format base(int w, int h, Matrix m = Matrix::BT709, Range r = Range::Limited)
{
    Format f;
    f.width = w;
    f.height = h;
    f.matrix = m;
    f.range = r;
    return f;
}

void testReferenceLayouts()
{
    const char *names[] = {
        "nv12",        "nv21",       "yuv420p",     "yvu420p",     "p010le",       "p010be",
        "p012le",      "p016le",     "yuv420p10le", "yuv420p10be", "yuv420p12le",  "yuv422p",
        "nv16",        "nv61",       "p210le",      "yuv444p",     "nv24",         "nv42",
        "yuv444p16le", "p410le",     "gray",        "gray10le",    "nv12p10lelsb", "yuv420p10lemsb",
        "nv16p12be",   "yvu422p10le"};
    uint32_t seed = 1;
    for (const char *n : names)
    {
        checkAgainstReference(n, base(17, 11), seed++); // odd size
        checkAgainstReference(n, base(32, 8, Matrix::BT601), seed++);
        checkAgainstReference(n, base(15, 9, Matrix::BT2020), seed++);
    }
    for (const char *n : {"nv12", "yuv420p", "yuv444p", "gray"})
        checkAgainstReference(n, base(19, 7, Matrix::BT709, Range::Full), seed++);
    for (const char *n : {"yuyv422", "uyvy422", "yvyu422"})
    {
        checkAgainstReference(n, base(18, 11), seed++);
        checkAgainstReference(n, base(32, 4, Matrix::BT601, Range::Full), seed++);
    }
}

void testCrossLayoutIdentity()
{
    Format f = base(33, 21);
    CHECK(applyPixFmtName("nv12", f));
    const Samples s = makeSamples(f, 99);
    std::vector<uint8_t> want;
    for (const char *n : {"nv12", "nv21", "yuv420p", "yvu420p"})
    {
        Format g = f;
        CHECK(applyPixFmtName(n, g));
        const ImageData img = decode(pack(s, g), g);
        CHECK(!img.isNull());
        if (img.isNull())
            continue;
        if (want.empty())
            want = *img.buffer;
        else
            CHECK(*img.buffer == want);
    }

    // The bug report: NV12 bytes decoded as planar 4:2:0 have the same frame
    // size, so nothing complains, but the colours are wrong.
    Format planar = f;
    CHECK(applyPixFmtName("yuv420p", planar));
    CHECK(computeFrameLayout(planar)->frameBytes == computeFrameLayout(f)->frameBytes);
    const ImageData wrong = decode(pack(s, f), planar);
    CHECK(!wrong.isNull() && *wrong.buffer != want);
    // The luma is still right, only chroma moves: grey stays grey.
}

void testGenericNamesRoundTrip()
{
    int combos = 0;
    for (Layout l : {Layout::Planar, Layout::SemiPlanar})
        for (Subsampling ss :
             {Subsampling::S420, Subsampling::S422, Subsampling::S444, Subsampling::S400})
            for (PlaneOrder o : {PlaneOrder::UV, PlaneOrder::VU})
                for (int d : {8, 10, 12, 16})
                    for (Endian e : {Endian::Little, Endian::Big})
                        for (bool msb : {false, true})
                        {
                            if (ss == Subsampling::S400 &&
                                (l != Layout::Planar || o != PlaneOrder::UV))
                                continue;
                            if (d == 8 && (msb || e == Endian::Big))
                                continue;
                            Format f = base(16, 8);
                            f.layout = l, f.subsampling = ss, f.order = o, f.bitDepth = d;
                            f.endian = e, f.msbAligned = msb;
                            CHECK(isValid(f));
                            const std::string name = pixFmtName(f);
                            CHECK(!name.empty());
                            Format g = base(16, 8);
                            CHECK(applyPixFmtName(name, g));
                            CHECK(g == f);
                            if (!(g == f))
                                std::printf("  (round trip failed for %s)\n", name.c_str());
                            ++combos;
                        }
    CHECK(combos > 100);
    Format f = base(16, 8);
    for (const char *alias :
         {"i420", "yv12", "yv16", "yv24", "yuv420sp", "yvu420sp", "p012", "i400"})
        CHECK(applyPixFmtName(alias, f));
    for (const char *bad : {"nv12p7le", "yuv420p17le", "nv", "yuv423p", "gray8msb", "nv12lsb"})
        CHECK(!applyPixFmtName(bad, f));
}

void testExtensionMap()
{
    CHECK(normalizeExtension(".NV12") == "nv12");
    CHECK(normalizeExtension("*.Raw_1") == "raw_1");
    CHECK(normalizeExtension("y4m").empty());
    CHECK(normalizeExtension("a.b").empty());
    CHECK(isYuvExtension("yuv") && isYuvExtension("y4m") && isYuvExtension("nv12"));
    CHECK(!isYuvExtension("png"));
    CHECK(mappedPixFmt("i420") == std::optional<std::string>("yuv420p"));
    CHECK(!mappedPixFmt("yuv").has_value());

    const auto rule = parseExtensionRule(" .Cam = NV21 ");
    CHECK(rule && rule->ext == "cam" && rule->pixFmt == "nv21");
    CHECK(rule && formatExtensionRule(*rule) == "cam=nv21");
    CHECK(!parseExtensionRule("cam=bogus"));

    // Root-cause scenario: a .yuv camera dump that is really NV12. By default
    // inference picks planar; mapping .yuv -> nv12 fixes it; tokens still win.
    const std::string before = extensionRulesFingerprint();
    const int64_t bytes = 16 * 10 * 3 / 2;
    CHECK(pixFmtName(inferYuvParams("clip_16x10.yuv", "", bytes).format) == "yuv420p");
    auto rules = defaultExtensionRules();
    for (auto &r : rules)
        if (r.ext == "yuv")
            r.pixFmt = "nv12";
    rules.push_back({"cam", "nv21"});
    const auto installed = setExtensionRules(rules);
    CHECK(installed.size() == rules.size());
    CHECK(extensionRulesFingerprint() != before);
    CHECK(isYuvExtension("CAM"));
    CHECK(pixFmtName(inferYuvParams("clip_16x10.yuv", "", bytes).format) == "nv12");
    CHECK(pixFmtName(inferYuvParams("clip_16x10.cam", "", bytes).format) == "nv21");
    CHECK(pixFmtName(inferYuvParams("clip_16x10_i420.yuv", "", bytes).format) == "yuv420p");
    setExtensionRules(defaultExtensionRules());
    CHECK(extensionRulesFingerprint() == before);
    CHECK(!isYuvExtension("cam"));
}

} // namespace

int main()
{
    testReferenceLayouts();
    testCrossLayoutIdentity();
    testGenericNamesRoundTrip();
    testExtensionMap();
    if (g_failures == 0)
        std::printf("test_yuv_layout_reference: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
