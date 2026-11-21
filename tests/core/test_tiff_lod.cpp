// tiff_lod_tests — perf review P2-4: bounded uncompressed strip TIFF LOD used
// where WIC is unavailable. Fixtures come from a small hand-written TIFF writer
// (both byte orders) so the test does not depend on Qt's TIFF plugin.
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace mviewer::core::detail
{
// Defined in QtDecoder_tiff.cpp (exported from mviewer_core).
bool tiffStripLodEligible(const std::string &path);
QImage decodeTiffStripLod(const std::string &path, const QSize &target);
} // namespace mviewer::core::detail

using mviewer::core::detail::decodeTiffStripLod;
using mviewer::core::detail::tiffStripLodEligible;

static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::printf("  PASS: %s\n", msg);                                                      \
        else                                                                                       \
        {                                                                                          \
            std::printf("  FAIL: %s\n", msg);                                                      \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (0)

namespace
{

struct TiffSpec
{
    int width = 0;
    int height = 0;
    int samples = 3;
    int bits = 8;
    int photometric = 2;
    int extraSample = -1; // -1: no ExtraSamples tag
    int rowsPerStrip = 0; // 0: single strip
    bool bigEndian = false;
    int compression = 1;
    int planar = 1;
    int orientation = 1;
    bool tiled = false;
};

class Writer
{
  public:
    explicit Writer(bool be) : m_be(be)
    {
    }
    void u16(uint32_t v)
    {
        if (m_be)
            put({static_cast<char>(v >> 8), static_cast<char>(v)});
        else
            put({static_cast<char>(v), static_cast<char>(v >> 8)});
    }
    void u32(uint32_t v)
    {
        if (m_be)
        {
            u16(v >> 16);
            u16(v & 0xFFFFu);
        }
        else
        {
            u16(v & 0xFFFFu);
            u16(v >> 16);
        }
    }
    void entry(uint32_t tag, uint32_t type, uint32_t count, uint32_t value)
    {
        u16(tag);
        u16(type);
        u32(count);
        if (type == 3 && count == 1)
        {
            u16(value);
            u16(0);
        }
        else
            u32(value);
    }
    QByteArray &bytes()
    {
        return m_bytes;
    }

  private:
    void put(std::initializer_list<char> c)
    {
        for (const char ch : c)
            m_bytes.append(ch);
    }
    bool m_be;
    QByteArray m_bytes;
};

// Pixel source: channel value for (x, y, c) in the sample's full range.
using PixelFn = uint32_t (*)(int x, int y, int c, int w, int h, int maxv);

uint32_t gradientFn(int x, int y, int c, int w, int h, int maxv)
{
    if (c == 0)
        return static_cast<uint32_t>(static_cast<int64_t>(x) * maxv / std::max(1, w - 1));
    if (c == 1)
        return static_cast<uint32_t>(static_cast<int64_t>(y) * maxv / std::max(1, h - 1));
    return static_cast<uint32_t>(maxv / 2);
}

QByteArray pixelData(const TiffSpec &s, PixelFn fn)
{
    const int maxv = s.bits == 16 ? 65535 : 255;
    Writer px(s.bigEndian);
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
            for (int c = 0; c < s.samples; ++c)
            {
                const uint32_t v = fn(x, y, c, s.width, s.height, maxv);
                if (s.bits == 16)
                    px.u16(v);
                else
                    px.bytes().append(static_cast<char>(v));
            }
    return px.bytes();
}

bool writeTiff(const QString &path, const TiffSpec &s, PixelFn fn)
{
    const QByteArray data = pixelData(s, fn);
    const int rps = s.rowsPerStrip > 0 ? std::min(s.rowsPerStrip, s.height) : s.height;
    const int strips = (s.height + rps - 1) / rps;
    const uint32_t rowBytes = static_cast<uint32_t>(s.width) * s.samples * (s.bits / 8);
    const int nEntries =
        10 + (s.extraSample >= 0 ? 1 : 0) + (s.orientation != 1 ? 1 : 0) + (s.tiled ? 1 : 0);
    const uint32_t ifd = 8;
    const uint32_t extra = ifd + 2 + static_cast<uint32_t>(nEntries) * 12 + 4;
    const uint32_t bpsOff = extra;
    const uint32_t offOff = bpsOff + 8;
    const uint32_t cntOff = offOff + static_cast<uint32_t>(strips) * 4;
    const uint32_t dataOff = cntOff + static_cast<uint32_t>(strips) * 4;

    Writer w(s.bigEndian);
    w.bytes().append(s.bigEndian ? "MM" : "II", 2);
    w.u16(42);
    w.u32(ifd);
    w.u16(static_cast<uint32_t>(nEntries));
    w.entry(256, 4, 1, static_cast<uint32_t>(s.width));
    w.entry(257, 4, 1, static_cast<uint32_t>(s.height));
    if (s.samples == 1)
        w.entry(258, 3, 1, static_cast<uint32_t>(s.bits));
    else
        w.entry(258, 3, static_cast<uint32_t>(s.samples), bpsOff);
    w.entry(259, 3, 1, static_cast<uint32_t>(s.compression));
    w.entry(262, 3, 1, static_cast<uint32_t>(s.photometric));
    w.entry(273, 4, static_cast<uint32_t>(strips), strips == 1 ? dataOff : offOff);
    if (s.orientation != 1)
        w.entry(274, 3, 1, static_cast<uint32_t>(s.orientation));
    w.entry(277, 3, 1, static_cast<uint32_t>(s.samples));
    w.entry(278, 4, 1, static_cast<uint32_t>(rps));
    w.entry(279, 4, static_cast<uint32_t>(strips), strips == 1 ? rowBytes * rps : cntOff);
    w.entry(284, 3, 1, static_cast<uint32_t>(s.planar));
    if (s.tiled)
        w.entry(322, 3, 1, 16);
    if (s.extraSample >= 0)
        w.entry(338, 3, 1, static_cast<uint32_t>(s.extraSample));
    w.u32(0);
    for (int i = 0; i < 4; ++i)
        w.u16(static_cast<uint32_t>(s.bits));
    for (int i = 0; i < strips; ++i)
        w.u32(dataOff + static_cast<uint32_t>(i) * rowBytes * static_cast<uint32_t>(rps));
    for (int i = 0; i < strips; ++i)
    {
        const int rows = std::min(rps, s.height - i * rps);
        w.u32(rowBytes * static_cast<uint32_t>(rows));
    }
    w.bytes().append(data);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(w.bytes()) == w.bytes().size();
}

QImage gradientImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
    {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                line[static_cast<ptrdiff_t>(x) * 3 + c] =
                    static_cast<uchar>(gradientFn(x, y, c, w, h, 255));
    }
    return img;
}

double rgbMad(const QImage &a0, const QImage &b0)
{
    const QImage a = a0.convertToFormat(QImage::Format_RGB888);
    const QImage b = b0.convertToFormat(QImage::Format_RGB888);
    if (a.size() != b.size() || a.isNull())
        return std::numeric_limits<double>::max();
    double sum = 0;
    for (int y = 0; y < a.height(); ++y)
    {
        const uchar *p = a.constScanLine(y);
        const uchar *q = b.constScanLine(y);
        for (int x = 0; x < a.width() * 3; ++x)
            sum += std::abs(static_cast<int>(p[x]) - static_cast<int>(q[x]));
    }
    return sum / (static_cast<double>(a.width()) * a.height() * 3);
}

bool near(int a, int b, int tol)
{
    return std::abs(a - b) <= tol;
}

std::string str(const QString &s)
{
    return std::string(s.toUtf8().constData());
}

} // namespace

static void testGradient(const QString &dir)
{
    TiffSpec s;
    s.width = 2048;
    s.height = 1536;
    s.rowsPerStrip = 16;
    const QString le = dir + "/grad_le.tif";
    CHECK(writeTiff(le, s, gradientFn), "a: LE RGB8 TIFF written");
    CHECK(tiffStripLodEligible(str(le)), "a: LE RGB8 TIFF eligible");
    const QImage got = decodeTiffStripLod(str(le), QSize(256, 192));
    CHECK(!got.isNull() && got.size() == QSize(256, 192), "a: decode 256x192");
    CHECK(got.format() == QImage::Format_RGB888, "a: RGB888 output");
    const QImage ref =
        gradientImage(2048, 1536).scaled(256, 192, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    CHECK(rgbMad(got, ref) < 4.0, "a: mean abs diff vs Qt smooth scale < 4");

    s.bigEndian = true;
    const QString be = dir + "/grad_be.tif";
    CHECK(writeTiff(be, s, gradientFn), "b: BE RGB8 TIFF written");
    const QImage gotBe = decodeTiffStripLod(str(be), QSize(256, 192));
    CHECK(!gotBe.isNull() && gotBe == got, "b: big-endian output matches little-endian");
}

static uint32_t quadrantFn(int x, int y, int, int w, int h, int)
{
    return static_cast<uint32_t>((y >= h / 2 ? 140 : 20) + (x >= w / 2 ? 70 : 0));
}

static void testGray(const QString &dir)
{
    TiffSpec s;
    s.width = 1000;
    s.height = 800;
    s.samples = 1;
    s.photometric = 1;
    const QString p = dir + "/gray.tif";
    CHECK(writeTiff(p, s, quadrantFn), "c: gray8 TIFF written");
    const QImage got = decodeTiffStripLod(str(p), QSize(100, 80));
    CHECK(!got.isNull() && got.format() == QImage::Format_Grayscale8, "c: Grayscale8 output");
    if (got.isNull())
        return;
    CHECK(near(qGray(got.pixel(25, 20)), 20, 2) && near(qGray(got.pixel(75, 20)), 90, 2) &&
              near(qGray(got.pixel(25, 60)), 140, 2) && near(qGray(got.pixel(75, 60)), 210, 2),
          "c: quadrant values within 2");
}

static uint32_t halfAlphaFn(int x, int, int c, int w, int, int)
{
    const bool left = x < w / 2;
    if (c == 3)
        return left ? 255u : 0u;
    if (left)
        return c == 0 ? 255u : 0u;
    return c == 1 ? 255u : 0u;
}

static void testAlpha(const QString &dir)
{
    TiffSpec s;
    s.width = 800;
    s.height = 600;
    s.samples = 4;
    s.extraSample = 2;
    const QString p = dir + "/alpha.tif";
    CHECK(writeTiff(p, s, halfAlphaFn), "d: RGBA8 unassociated TIFF written");
    const QImage got = decodeTiffStripLod(str(p), QSize(80, 60));
    CHECK(!got.isNull() && got.format() == QImage::Format_RGBA8888, "d: RGBA8888 output");
    if (got.isNull())
        return;
    const QRgb l = got.pixel(10, 30);
    CHECK(near(qRed(l), 255, 2) && near(qGreen(l), 0, 2) && qAlpha(l) == 255,
          "d: opaque half stays red");
    CHECK(qAlpha(got.pixel(70, 30)) == 0, "d: transparent half has alpha 0");
}

static void testDeep(const QString &dir)
{
    TiffSpec s;
    s.width = 1200;
    s.height = 900;
    s.bits = 16;
    s.rowsPerStrip = 7;
    const QString p = dir + "/rgb16.tif";
    CHECK(writeTiff(p, s, gradientFn), "e: RGB16 TIFF written");
    const QImage got = decodeTiffStripLod(str(p), QSize(120, 90));
    CHECK(!got.isNull() && got.format() == QImage::Format_RGBX64 && got.size() == QSize(120, 90),
          "e: RGBX64 120x90 output");
    const QImage ref =
        gradientImage(1200, 900).scaled(120, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    CHECK(rgbMad(got, ref) < 4.0, "e: 16-bit mean abs diff vs Qt smooth scale < 4");
}

static void testIneligible(const QString &dir)
{
    TiffSpec base;
    base.width = 64;
    base.height = 64;
    struct Case
    {
        const char *name;
        void (*apply)(TiffSpec &);
    };
    const Case cases[] = {
        {"f: LZW-compressed TIFF is not eligible", [](TiffSpec &s) { s.compression = 5; }},
        {"f: tiled TIFF is not eligible", [](TiffSpec &s) { s.tiled = true; }},
        {"f: planar TIFF is not eligible", [](TiffSpec &s) { s.planar = 2; }},
        {"f: rotated TIFF is not eligible", [](TiffSpec &s) { s.orientation = 6; }},
    };
    int i = 0;
    for (const Case &c : cases)
    {
        TiffSpec s = base;
        c.apply(s);
        const QString p = dir + QString("/inel%1.tif").arg(i++);
        const bool ok = writeTiff(p, s, gradientFn);
        CHECK(ok && !tiffStripLodEligible(str(p)), c.name);
    }
}

static void testTruncated(const QString &dir)
{
    QFile src(dir + "/grad_le.tif");
    CHECK(src.open(QIODevice::ReadOnly), "g: source TIFF opened");
    const QByteArray all = src.readAll();
    const QString p = dir + "/trunc.tif";
    QFile out(p);
    CHECK(out.open(QIODevice::WriteOnly) && out.write(all.left(all.size() / 2)) > 0,
          "g: truncated TIFF written");
    out.close();
    const bool eligible = tiffStripLodEligible(str(p));
    const QImage got = decodeTiffStripLod(str(p), QSize(256, 192));
    CHECK(!eligible && got.isNull(), "g: truncated TIFF is rejected without a crash");
}

static void testOddSizes(const QString &dir)
{
    const QSize sizes[] = {QSize(1001, 7), QSize(7, 1001)};
    for (const QSize &sz : sizes)
    {
        TiffSpec s;
        s.width = sz.width();
        s.height = sz.height();
        const QString p = dir + QString("/odd_%1x%2.tif").arg(sz.width()).arg(sz.height());
        CHECK(writeTiff(p, s, gradientFn), "h: odd-size TIFF written");
        const int edge = 256;
        const double ratio = static_cast<double>(edge) / std::max(s.width, s.height);
        const QSize target(std::max(1, static_cast<int>(std::lround(s.width * ratio))),
                           std::max(1, static_cast<int>(std::lround(s.height * ratio))));
        const QImage got = decodeTiffStripLod(str(p), target);
        CHECK(!got.isNull() && got.size() == target, "h: odd-size decode matches lround rule");
    }
}

static void testTiming(const QString &dir)
{
    TiffSpec s;
    s.width = 4096;
    s.height = 4096;
    s.rowsPerStrip = 8;
    const QString p = dir + "/big.tif";
    CHECK(writeTiff(p, s, gradientFn), "i: 4096x4096 TIFF written");
    qint64 bounded = std::numeric_limits<qint64>::max();
    qint64 qt = std::numeric_limits<qint64>::max();
    bool qtOk = true;
    for (int i = 0; i < 3; ++i)
    {
        QElapsedTimer t;
        t.start();
        const QImage a = decodeTiffStripLod(str(p), QSize(256, 256));
        bounded = std::min(bounded, t.elapsed());
        t.start();
        const QImage full(p);
        qtOk = qtOk && !full.isNull();
        const QImage b =
            full.isNull() ? QImage()
                          : full.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        qt = std::min(qt, t.elapsed());
        (void)a;
        (void)b;
    }
    std::printf("tiff_lod timing: bounded=%lld ms qt_full_scaled=%lld ms\n",
                static_cast<long long>(bounded), qtOk ? static_cast<long long>(qt) : -1LL);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temporary directory created");
    if (!tmp.isValid())
        return 1;
    const QString dir = tmp.path();
    testGradient(dir);
    testGray(dir);
    testAlpha(dir);
    testDeep(dir);
    testIneligible(dir);
    testTruncated(dir);
    testOddSizes(dir);
    testTiming(dir);
    std::printf("=== tiff_lod_tests: %s ===\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
