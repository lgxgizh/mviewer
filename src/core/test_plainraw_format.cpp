// NOLINTBEGIN
#include "core/image/rawplain/PlainRawFormat.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace mviewer::core::rawplain;

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

static Params sized(int w, int h)
{
    Params p;
    p.width = w;
    p.height = h;
    return p;
}

int main()
{
    {
        Params p = sized(4096, 3072);
        p.packing = Packing::Bits8;
        p.bitDepth = 8;
        CHECK(minRowBytes(p) == 4096);
        CHECK(frameBytes(p) == int64_t{4096} * 3072);

        p.packing = Packing::Lsb16;
        p.bitDepth = 10;
        CHECK(minRowBytes(p) == 8192);
        p.packing = Packing::Msb16;
        CHECK(minRowBytes(p) == 8192);

        p.packing = Packing::Mipi10;
        p.bitDepth = 10;
        CHECK(minRowBytes(p) == 5120);
        p.width = 4097;
        CHECK(minRowBytes(p) == 5125);

        p.packing = Packing::Mipi12;
        p.bitDepth = 12;
        p.width = 4096;
        CHECK(minRowBytes(p) == 6144);
        p.width = 4097;
        CHECK(minRowBytes(p) == 6147);
    }

    {
        Params p;
        p.bitDepth = 10;
        CHECK(effectiveWhiteLevel(p) == 1023);
        p.whiteLevel = 1000;
        CHECK(effectiveWhiteLevel(p) == 1000);
        p.bitDepth = 12;
        p.whiteLevel = 0;
        CHECK(effectiveWhiteLevel(p) == 4095);
        p.bitDepth = 8;
        CHECK(effectiveWhiteLevel(p) == 255);
    }

    {
        Params p = sized(64, 48);
        p.bitDepth = 10;
        p.packing = Packing::Lsb16;
        std::string why;
        CHECK(isValid(p, &why));
        CHECK(isValidDefaults(p, &why));

        Params bad = p;
        bad.packing = Packing::Mipi10;
        bad.bitDepth = 12;
        CHECK(!isValid(bad, &why));
        CHECK(!why.empty());
        CHECK(!isValidDefaults(bad, &why));

        bad = p;
        bad.packing = Packing::Bits8;
        bad.bitDepth = 10;
        CHECK(!isValid(bad, &why));

        bad = p;
        bad.blackLevel = 1023;
        CHECK(!isValid(bad, &why));
        CHECK(why.find("black") != std::string::npos);

        bad = p;
        bad.rowStride = 1;
        CHECK(!isValid(bad, &why));
        CHECK(why.find("stride") != std::string::npos);

        bad = p;
        bad.headerOffset = -1;
        CHECK(!isValid(bad, &why));
        CHECK(why.find("offset") != std::string::npos);

        bad = p;
        bad.width = 0;
        CHECK(!isValid(bad, &why));
        CHECK(why == "zero size");

        bad = p;
        bad.height = 0;
        CHECK(!isValid(bad, &why));
        CHECK(why == "zero size");

        bad = p;
        bad.width = kMaxDimension + 1;
        CHECK(!isValid(bad, &why));
        CHECK(why == "width or height above 65536");

        bad = p;
        bad.height = kMaxDimension + 1;
        CHECK(!isValid(bad, &why));
        CHECK(why == "width or height above 65536");

        bad = p;
        bad.width = 20000;
        bad.height = 20000;
        CHECK(!isValid(bad, &why));

        bad = p;
        bad.gain = std::numeric_limits<double>::quiet_NaN();
        CHECK(!isValid(bad, &why));
        CHECK(!isValidDefaults(bad, &why));
    }

    {
        Params p = sized(4096, 3072);
        p.bitDepth = 10;
        p.packing = Packing::Lsb16;
        p.blackLevel = 64;
        p.wb = WhiteBalance::Manual;
        p.wbGainR = 1.5;
        p.wbGainB = 2.25;
        p.gain = 0.5;
        p.srgbGamma = false;
        p.byteOrder = ByteOrder::Big;
        p.cfa = Cfa::BGGR;
        p.headerOffset = 16;
        p.rowStride = 8192;
        const std::string s = toString(p);
        const auto back = parseParams(s);
        CHECK(back.has_value());
        CHECK(toString(*back) == s);
        CHECK(*back == p);

        Params q;
        q.width = 100;
        q.height = 200;
        q.gain = 1.125;
        const std::string s2 = toString(q);
        CHECK(toString(*parseParams(s2)) == s2);
        CHECK(toString(*parseParams(toString(q))) == toString(q));
    }

    {
        CHECK(!parseParams("w=abc").has_value());
        CHECK(!parseParams("gain=inf").has_value());
        CHECK(!parseParams("pack=foo").has_value());
        CHECK(!parseParams("w=1;unknown=1").has_value());
        CHECK(!parseParams("w=1;w=2").has_value());
        CHECK(!parseParams("gain=nan").has_value());
        CHECK(parseParams("w=10;h=20").has_value());
        CHECK(parseParams("").has_value());
    }

    {
        auto a = sizeFromFileName("cam_4096x3072_10bit.raw");
        CHECK(a.has_value() && a->first == 4096 && a->second == 3072);
        auto b = sizeFromFileName("A4096X3072.raw");
        CHECK(b.has_value() && b->first == 4096 && b->second == 3072);
        CHECK(!sizeFromFileName("1x2.raw").has_value());
        CHECK(!sizeFromFileName("123456x100").has_value());
        auto c = sizeFromFileName("img_640x480x2.raw");
        CHECK(c.has_value() && c->first == 640 && c->second == 480);
        CHECK(sizeFromFileName("640x480").has_value());
        auto d = sizeFromFileName("4096X3072");
        CHECK(d.has_value() && d->first == 4096 && d->second == 3072);
        auto e = sizeFromFileName("4096x3072");
        CHECK(e.has_value() && e->first == 4096 && e->second == 3072);
        const char mul[] = {'4', '0', '9', '6', '\xC3', '\x97', '3', '0', '7', '2', 0};
        auto f = sizeFromFileName(mul);
        CHECK(f.has_value() && f->first == 4096 && f->second == 3072);
        auto g = sizeFromFileName("/tmp/cam4096x3072dump.raw");
        CHECK(g.has_value() && g->first == 4096 && g->second == 3072);
        CHECK(!sizeFromFileName("640xabc").has_value() ||
              sizeFromFileName("foo_320x240.raw").has_value());
        auto h = sizeFromFileName("640xabc_320x240.raw");
        CHECK(h.has_value() && h->first == 320 && h->second == 240);
    }

    {
        Params p = sized(64, 32);
        p.packing = Packing::Lsb16;
        p.bitDepth = 10;
        const int64_t need = frameBytes(p);
        CHECK(checkFileSize(p, need) == SizeCheck::Exact);
        CHECK(checkFileSize(p, need - 1) == SizeCheck::TooSmall);
        CHECK(checkFileSize(p, need + 10) == SizeCheck::Larger);
        Params inv;
        CHECK(checkFileSize(inv, 100) == SizeCheck::Invalid);
    }

    {
        Params base;
        base.width = 4096;
        base.height = 3072;
        base.packing = Packing::Mipi10;
        base.bitDepth = 10;
        base.blackLevel = 64;
        const int64_t lsb = int64_t{4096} * 3072 * 2;
        auto g1 = guessFromFileSize(base, lsb);
        CHECK(!g1.empty());
        CHECK(!g1.empty() && g1.front().packing == Packing::Lsb16);
        CHECK(!g1.empty() && g1.front().bitDepth == 10);

        const int64_t mipi = int64_t{4096} * 3072 * 5 / 4;
        auto g2 = guessFromFileSize(base, mipi);
        CHECK(!g2.empty());
        CHECK(!g2.empty() && g2.front().packing == Packing::Mipi10);

        const int64_t b8 = int64_t{4096} * 3072;
        auto g3 = guessFromFileSize(base, b8);
        CHECK(!g3.empty());
        CHECK(!g3.empty() && g3.front().packing == Packing::Bits8);
        CHECK(!g3.empty() && g3.front().bitDepth == 8);
        CHECK(!g3.empty() && g3.front().blackLevel == 16);
    }

    {
        Params def;
        auto r = resolveForFile(def, "cam_4096x3072_10bit.raw", int64_t{4096} * 3072 * 2);
        CHECK(r.sizeFromName);
        CHECK(r.params.width == 4096);
        CHECK(r.params.height == 3072);
        CHECK(!r.guessed);
        CHECK(r.params.packing == Packing::Lsb16);
        CHECK(r.known);
        CHECK(r.check == SizeCheck::Exact);
        CHECK(r.extraBytes == 0);

        auto m = resolveForFile(def, "cam_4096x3072_10bit.raw", int64_t{4096} * 3072 * 5 / 4);
        CHECK(m.guessed);
        CHECK(m.known);
        CHECK(m.params.packing == Packing::Mipi10);

        auto u = resolveForFile(def, "nosize.raw", 100);
        CHECK(!u.known);
        CHECK(u.why.find("image size unknown") != std::string::npos);

        Params d2;
        d2.width = 64;
        d2.height = 32;
        d2.packing = Packing::Lsb16;
        const int64_t need = frameBytes(d2);
        auto small = resolveForFile(d2, "cam_4096x3072.raw", need - 10);
        CHECK(!small.sizeFromName);
        CHECK(small.params.width == 64);
        CHECK(!small.known);
        CHECK(small.why.find("smaller than one frame") != std::string::npos);
        CHECK(small.why.find(std::to_string(need)) != std::string::npos);
        CHECK(small.extraBytes == 0);

        auto larger = resolveForFile(d2, "cam_64x32.raw", need + 24);
        CHECK(larger.check == SizeCheck::Larger);
        CHECK(larger.known);
        CHECK(larger.extraBytes == 24);
        CHECK(!larger.guessed);

        const char *crop = "crop_128x128_from_4096x3072.raw";
        const auto tokens = sizesFromFileName(crop);
        CHECK(tokens.size() == 2);
        CHECK(tokens[0].first == 128 && tokens[0].second == 128);
        CHECK(tokens[1].first == 4096 && tokens[1].second == 3072);
        auto firstToken = sizeFromFileName(crop);
        CHECK(firstToken.has_value() && firstToken->first == 128 && firstToken->second == 128);

        auto big = resolveForFile(def, crop, int64_t{4096} * 3072 * 2);
        CHECK(big.sizeFromName);
        CHECK(big.params.width == 4096);
        CHECK(big.params.height == 3072);
        CHECK(big.check == SizeCheck::Exact);
        CHECK(big.extraBytes == 0);
        CHECK(!big.guessed);

        auto cropSmall = resolveForFile(def, crop, int64_t{128} * 128 * 2);
        CHECK(cropSmall.params.width == 128);
        CHECK(cropSmall.params.height == 128);
        CHECK(cropSmall.check == SizeCheck::Exact);

        auto named = resolveForFile(def, "a_640x480.raw", int64_t{640} * 480 * 2);
        CHECK(named.sizeFromName);
        CHECK(named.params.width == 640);
        CHECK(named.params.height == 480);
        CHECK(named.params.packing == Packing::Lsb16);
        CHECK(named.check == SizeCheck::Exact);
        CHECK(!named.guessed);
        CHECK(sizesFromFileName("a_640x480.raw").size() == 1);

        auto cropMipi = resolveForFile(def, crop, int64_t{4096} * 3072 * 5 / 4);
        CHECK(cropMipi.params.width == 4096);
        CHECK(cropMipi.params.height == 3072);
        CHECK(cropMipi.guessed);
        CHECK(cropMipi.params.packing == Packing::Mipi10);
        CHECK(cropMipi.check == SizeCheck::Exact);
    }

    {
        Params base;
        base.packing = Packing::Lsb16;
        base.bitDepth = 10;
        const auto hd = guessCommonSize(base, int64_t{1920} * 1080 * 2);
        CHECK(hd.has_value());
        CHECK(hd->width == 1920 && hd->height == 1080);
        CHECK(hd->packing == Packing::Lsb16);
        CHECK(hd->bitDepth == 10);

        const auto mipiHd = guessCommonSize(base, int64_t{1920} * 1080 * 5 / 4);
        CHECK(mipiHd.has_value());
        CHECK(mipiHd->width == 1920 && mipiHd->height == 1080);
        CHECK(mipiHd->packing == Packing::Mipi10);

        CHECK(!guessCommonSize(base, 12345).has_value());
    }

    if (g_failures != 0)
    {
        std::printf("test_plainraw_format: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_plainraw_format: all passed\n");
    return 0;
}
// NOLINTEND
