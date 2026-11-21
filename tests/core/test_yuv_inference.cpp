// NOLINTBEGIN
// Unit tests for core/image/yuv/YuvParamInference (ADR 032 steps 3-5): file-name,
// folder-name and file-size inference, plus a fuzz loop over random names.

#include "core/image/yuv/YuvParamInference.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

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

struct Case
{
    const char *file;
    const char *folder;
    int64_t fileSize;
    bool sizeKnown;
    int w;
    int h;
    const char *pixFmt; // "" = do not check
    Matrix matrix;
    Range range;
};

constexpr Matrix M601 = Matrix::BT601;
constexpr Matrix M709 = Matrix::BT709;
constexpr Matrix M2020 = Matrix::BT2020;
constexpr Range LIM = Range::Limited;
constexpr Range FULL = Range::Full;
constexpr int64_t kCif420 = 352 * 288 * 3 / 2;
constexpr int64_t kHd420 = 1920 * 1080 * 3 / 2;
constexpr int64_t kHd88420 = 1920 * 1088 * 3 / 2;
constexpr int64_t kNtsc420 = 720 * 480 * 3 / 2;

static const Case kCases[] = {
    {"foreman_cif.yuv", "", 0, true, 352, 288, "yuv420p", M709, LIM},
    {"akiyo_qcif.yuv", "", 0, true, 176, 144, "yuv420p", M709, LIM},
    {"BasketballDrive_1920x1080_50.yuv", "", 0, true, 1920, 1080, "yuv420p", M709, LIM},
    {"Kimono1_1920x1080_24_10bit.yuv", "", 0, true, 1920, 1080, "yuv420p10le", M709, LIM},
    {"out_3840x2160_yuv420p10le_bt2020.yuv", "", 0, true, 3840, 2160, "yuv420p10le", M2020, LIM},
    {"clip_1280X720.nv12", "", 0, true, 1280, 720, "nv12", M709, LIM},
    {"a_720p_nv21.yuv", "", 0, true, 1280, 720, "nv21", M709, LIM},
    {"test-640x480-yuyv422.yuv", "", 0, true, 640, 480, "yuyv422", M709, LIM},
    {"frame.uyvy", "1920x1080", 0, true, 1920, 1080, "uyvy422", M709, LIM},
    {"x_1920*1080.yuv", "", 0, true, 1920, 1080, "yuv420p", M709, LIM},
    {"2023_holiday.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"cam_1080p_full.yuv", "", 0, true, 1920, 1080, "yuv420p", M709, FULL},
    {"cam_1080p_pc_bt601.yuv", "", 0, true, 1920, 1080, "yuv420p", M601, FULL},
    {"p010_3840x2160.yuv", "", 0, true, 3840, 2160, "p010le", M709, LIM},
    {"img.y", "640x480", 0, true, 640, 480, "gray", M709, LIM},
    {"y_4k_444_12bit.yuv", "", 0, true, 3840, 2160, "yuv444p12le", M709, LIM},
    {"y_422.yuv", "", 0, false, 0, 0, "yuv422p", M709, LIM},
    {"unique.yuv", "", kNtsc420 * 3, true, 720, 480, "yuv420p", M709, LIM},
    {"zero.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"x_1921x1081.yuv", "", 0, true, 1921, 1081, "yuv420p", M709, LIM},
    {"x_99999x99999.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"FOO_1920X1080_NV12.YUV", "", 0, true, 1920, 1080, "nv12", M709, LIM},
    {"[1920x1080]clip(nv12).yuv", "", 0, true, 1920, 1080, "nv12", M709, LIM},
    {"seq_vga.yuv", "", 0, true, 640, 480, "yuv420p", M709, LIM},
    {"seq_qvga.yuv", "", 0, true, 320, 240, "yuv420p", M709, LIM},
    {"seq_sif.yuv", "", 0, true, 352, 240, "yuv420p", M709, LIM},
    {"seq_4cif.yuv", "", 0, true, 704, 576, "yuv420p", M709, LIM},
    {"seq_480p.yuv", "", 0, true, 720, 480, "yuv420p", M709, LIM},
    {"seq_576p.yuv", "", 0, true, 720, 576, "yuv420p", M709, LIM},
    {"seq_1080i.yuv", "", 0, true, 1920, 1080, "yuv420p", M709, LIM},
    {"seq_fhd.yuv", "", 0, true, 1920, 1080, "yuv420p", M709, LIM},
    {"seq_1440p.yuv", "", 0, true, 2560, 1440, "yuv420p", M709, LIM},
    {"seq_qhd.yuv", "", 0, true, 2560, 1440, "yuv420p", M709, LIM},
    {"seq_2160p.yuv", "", 0, true, 3840, 2160, "yuv420p", M709, LIM},
    {"seq_uhd.yuv", "", 0, true, 3840, 2160, "yuv420p", M709, LIM},
    {"seq_dci4k.yuv", "", 0, true, 4096, 2160, "yuv420p", M709, LIM},
    {"seq_8k.yuv", "", 0, true, 7680, 4320, "yuv420p", M709, LIM},
    {"seq_4320p.yuv", "", 0, true, 7680, 4320, "yuv420p", M709, LIM},
    {"seq.yv12", "cif", 0, true, 352, 288, "yv12", M709, LIM},
    {"seq.i420", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"seq.p010", "1080p", 0, true, 1920, 1080, "p010le", M709, LIM},
    {"seq.yuy2", "720p", 0, true, 1280, 720, "yuyv422", M709, LIM},
    {"seq.y", "qcif", 0, true, 176, 144, "gray", M709, LIM},
    {"seq.yuv", "clips_1280x720_bt2020_full", 0, true, 1280, 720, "yuv420p", M2020, FULL},
    {"seq_640x480.yuv", "1920x1080", 0, true, 640, 480, "yuv420p", M709, LIM},
    {"seq_rec709_tv.yuv", "cif", 0, true, 352, 288, "yuv420p", M709, LIM},
    {"seq_smpte170m.yuv", "qcif", 0, true, 176, 144, "yuv420p", M601, LIM},
    {"seq_bt2100_fr_1080p.yuv", "", 0, true, 1920, 1080, "yuv420p", M2020, FULL},
    {"seq_jpeg_vga.yuv", "", 0, true, 640, 480, "yuv420p", M709, FULL},
    {"seq_studio_vga.yuv", "", 0, true, 640, 480, "yuv420p", M709, LIM},
    {"seq_gray16le_vga.yuv", "", 0, true, 640, 480, "gray16le", M709, LIM},
    {"seq_400_10b_vga.yuv", "", 0, true, 640, 480, "gray10le", M709, LIM},
    {"seq_444_vga.yuv", "", 0, true, 640, 480, "yuv444p", M709, LIM},
    {"seq_16b_vga.yuv", "", 0, true, 640, 480, "yuv420p16le", M709, LIM},
    {"seq_nv16_vga.yuv", "", 0, true, 640, 480, "nv16", M709, LIM},
    {"seq_nv24_vga.yuv", "", 0, true, 640, 480, "nv24", M709, LIM},
    {"seq_p210le_1080p.yuv", "", 0, true, 1920, 1080, "p210le", M709, LIM},
    {"seq_yuv420p10be_cif.yuv", "", 0, true, 352, 288, "yuv420p10be", M709, LIM},
    {"seq@1920x1080+nv12.yuv", "", 0, true, 1920, 1080, "nv12", M709, LIM},
    {"seq{cif},601.yuv", "", 0, true, 352, 288, "yuv420p", M601, LIM},
    {"seq_1x1.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"seq_123456x720.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"seq.yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {"seq.yuv", "", -5, false, 0, 0, "yuv420p", M709, LIM},
    {"seq.yuv", "", kHd88420 * 3, true, 1920, 1088, "yuv420p", M709, LIM},
    {"seq_10bit.nv12", "cif", 0, true, 352, 288, "nv12", M709, LIM},
    {"", "", 0, false, 0, 0, "yuv420p", M709, LIM},
    {".yuv", "", 0, false, 0, 0, "yuv420p", M709, LIM},
};

static void testCases()
{
    for (const Case &c : kCases)
    {
        const Inference inf = inferYuvParams(c.file, c.folder, c.fileSize);
        bool ok = inf.sizeKnown == c.sizeKnown;
        if (c.sizeKnown)
            ok = ok && inf.format.width == c.w && inf.format.height == c.h;
        if (c.pixFmt[0] != '\0')
            ok = ok && pixFmtName(inf.format) == c.pixFmt;
        ok = ok && inf.format.matrix == c.matrix && inf.format.range == c.range;
        if (!ok)
        {
            std::printf("FAIL case '%s' / '%s': known=%d %dx%d %s m=%d r=%d\n", c.file, c.folder,
                        int(inf.sizeKnown), inf.format.width, inf.format.height,
                        pixFmtName(inf.format).c_str(), int(inf.format.matrix),
                        int(inf.format.range));
            ++g_failures;
        }
    }
}

static void testSourcesAndCounts()
{
    Inference inf = inferYuvParams("frame.uyvy", "1920x1080", 0);
    CHECK(inf.sizeSource == SizeSource::FolderName && inf.pixFmtKnown);
    inf = inferYuvParams("a_1920x1080.yuv", "", 0);
    CHECK(inf.sizeSource == SizeSource::FileName && !inf.pixFmtKnown);
    CHECK(!inf.matrixKnown && !inf.rangeKnown && inf.frameCount == 0);
    inf = inferYuvParams("unique.yuv", "", kNtsc420 * 3);
    CHECK(inf.sizeSource == SizeSource::FileSize && inf.frameCount == 3);
    CHECK(inf.sizeCandidates.size() == 1);
    // 1080p frames are also a whole number of QVGA frames: ambiguous, both offered.
    inf = inferYuvParams("hd.yuv", "", kHd420);
    CHECK(!inf.sizeKnown && inf.sizeCandidates.size() >= 2);
    inf = inferYuvParams("hd_1920x1080.yuv", "", kHd420);
    CHECK(inf.sizeCandidates.empty());
    inf = inferYuvParams("a_cif.yuv", "", kCif420 * 4 + 77);
    CHECK(inf.frameCount == 4 && inf.trailingBytes == 77);
    inf = inferYuvParams("a_cif.yuv", "", 100);
    CHECK(inf.sizeKnown && inf.frameCount == 0 && inf.trailingBytes == 100);
    // 352x288 and 176x144 both divide this size (4 qcif frames per cif frame): ambiguous.
    inf = inferYuvParams("amb.yuv", "", kCif420 * 4);
    CHECK(!inf.sizeKnown && inf.sizeSource == SizeSource::None);
    inf = inferYuvParams("seq_bt601_full_cif.yuv", "", 0);
    CHECK(inf.matrixKnown && inf.rangeKnown && inf.complete());
}

static void testHelpers()
{
    int w = 0, h = 0;
    CHECK(parseSizeToken("foo_1920x1080_25", w, h) && w == 1920 && h == 1080);
    CHECK(parseSizeToken("a1280X720b", w, h) && w == 1280 && h == 720);
    CHECK(parseSizeToken("q_10*20", w, h) && w == 10 && h == 20);
    CHECK(parseSizeToken("x_uhd_y", w, h) && w == 3840 && h == 2160);
    CHECK(!parseSizeToken("2023", w, h));
    CHECK(!parseSizeToken("1x1080", w, h));
    CHECK(!parseSizeToken("123456x720", w, h));
    CHECK(!parseSizeToken("", w, h));
    CHECK(!parseSizeToken("1920x", w, h));
    const auto t = tokenizeName("A_b-C.d e(f)[g]{h},i+j@k");
    CHECK(t.size() == 11 && t[0] == "a" && t[2] == "c" && t[10] == "k");
    CHECK(tokenizeName("__--").empty());
}

static uint32_t g_rng = 0x9e3779b9u;
static uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void testFuzz()
{
    static const char kAlphabet[] = "0123456789xX*_-.cifpnv12yuv\xff";
    const int64_t kMax = std::numeric_limits<int64_t>::max();
    const int64_t sizes[] = {0, -1, kMax, std::numeric_limits<int64_t>::min(), kCif420, 7};
    int known = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::string name;
        const uint32_t len = nextRand() % 48;
        for (uint32_t k = 0; k < len; ++k)
        {
            const uint32_t r = nextRand();
            name.push_back(r % 7 == 0 ? char(r >> 8) : kAlphabet[r % sizeof(kAlphabet)]);
        }
        const int64_t size = (i % 3 == 0) ? (int64_t(nextRand()) << 16) ^ int64_t(nextRand())
                                          : sizes[nextRand() % 6];
        const Inference inf = inferYuvParams(name, i % 2 ? name : std::string(), size);
        if (inf.sizeKnown)
        {
            ++known;
            CHECK(isValid(inf.format));
            CHECK(inf.frameCount >= 0 && inf.trailingBytes >= 0);
        }
    }
    std::printf("  fuzz: %d of 20000 names gave a size\n", known);
}

int main()
{
    testCases();
    testSourcesAndCounts();
    testHelpers();
    testFuzz();
    if (g_failures == 0)
        std::printf("test_yuv_inference: all passed\n");
    else
        std::printf("test_yuv_inference: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
