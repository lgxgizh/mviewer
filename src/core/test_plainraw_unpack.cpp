// NOLINTBEGIN
#include "core/image/rawplain/PlainRawUnpack.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

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

static Params makeP(int w, int h, Packing pack, int bits, ByteOrder order = ByteOrder::Little)
{
    Params p;
    p.width = w;
    p.height = h;
    p.packing = pack;
    p.bitDepth = bits;
    p.byteOrder = order;
    p.wb = WhiteBalance::None;
    return p;
}

static void store16(std::uint8_t *p, std::uint16_t v, ByteOrder order)
{
    if (order == ByteOrder::Little)
    {
        p[0] = static_cast<std::uint8_t>(v & 0xFFu);
        p[1] = static_cast<std::uint8_t>(v >> 8);
    }
    else
    {
        p[1] = static_cast<std::uint8_t>(v & 0xFFu);
        p[0] = static_cast<std::uint8_t>(v >> 8);
    }
}

static void packMipi10(const std::uint16_t *src, int w, std::uint8_t *dst)
{
    int x = 0;
    int g = 0;
    while (x < w)
    {
        std::uint8_t *b = dst + static_cast<std::size_t>(g) * 5u;
        unsigned lsb = 0;
        const int n = (w - x >= 4) ? 4 : (w - x);
        for (int k = 0; k < 4; ++k)
        {
            const std::uint16_t v = (k < n) ? (src[x + k] & 0x3FFu) : 0;
            b[k] = static_cast<std::uint8_t>(v >> 2);
            lsb |= static_cast<unsigned>(v & 3u) << (2 * k);
        }
        b[4] = static_cast<std::uint8_t>(lsb);
        x += n;
        ++g;
    }
}

static void packMipi12(const std::uint16_t *src, int w, std::uint8_t *dst)
{
    int x = 0;
    int g = 0;
    while (x < w)
    {
        std::uint8_t *b = dst + static_cast<std::size_t>(g) * 3u;
        const std::uint16_t v0 = src[x] & 0xFFFu;
        b[0] = static_cast<std::uint8_t>(v0 >> 4);
        unsigned n1 = 0;
        if (x + 1 < w)
        {
            n1 = src[x + 1] & 0xFFFu;
        }
        b[1] = static_cast<std::uint8_t>(n1 >> 4);
        b[2] = static_cast<std::uint8_t>((v0 & 0xFu) | ((n1 & 0xFu) << 4));
        x += 2;
        ++g;
    }
}

static void checkAllSamples(const Params &p, const std::uint8_t *data, std::size_t size,
                            const std::vector<std::uint16_t> &unpacked)
{
    CHECK(static_cast<int>(unpacked.size()) == p.width * p.height);
    for (int y = 0; y < p.height; ++y)
    {
        for (int x = 0; x < p.width; ++x)
        {
            const int s = sampleAt(p, data, size, x, y);
            CHECK(s == static_cast<int>(unpacked[static_cast<std::size_t>(y * p.width + x)]));
        }
    }
    CHECK(sampleAt(p, data, size, -1, 0) == -1);
    CHECK(sampleAt(p, data, size, 0, -1) == -1);
    CHECK(sampleAt(p, data, size, p.width, 0) == -1);
    CHECK(sampleAt(p, data, size, 0, p.height) == -1);
}

int main()
{
    {
        Params p = makeP(4, 2, Packing::Bits8, 8);
        std::vector<std::uint8_t> buf = {1, 2, 3, 4, 5, 6, 7, 8};
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out.size() == 8);
        CHECK(out[0] == 1 && out[7] == 8);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(2, 1, Packing::Lsb16, 10, ByteOrder::Little);
        std::vector<std::uint8_t> buf(4);
        store16(buf.data(), static_cast<std::uint16_t>(0x1234), ByteOrder::Little);
        store16(buf.data() + 2, static_cast<std::uint16_t>(0x03FF), ByteOrder::Little);
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out[0] == (0x1234 & 0x3FF));
        CHECK(out[1] == 0x3FF);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(2, 1, Packing::Lsb16, 12, ByteOrder::Big);
        std::vector<std::uint8_t> buf(4);
        store16(buf.data(), static_cast<std::uint16_t>(0xABCD), ByteOrder::Big);
        store16(buf.data() + 2, static_cast<std::uint16_t>(0x0FFF), ByteOrder::Big);
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out[0] == (0xABCD & 0x0FFF));
        CHECK(out[1] == 0x0FFF);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(2, 1, Packing::Msb16, 10, ByteOrder::Little);
        const int v0 = 0x2AA;
        const int v1 = 0x155;
        std::vector<std::uint8_t> buf(4);
        store16(buf.data(), static_cast<std::uint16_t>(v0 << 6), ByteOrder::Little);
        store16(buf.data() + 2, static_cast<std::uint16_t>(v1 << 6), ByteOrder::Little);
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out[0] == v0);
        CHECK(out[1] == v1);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(1, 1, Packing::Msb16, 10, ByteOrder::Big);
        std::vector<std::uint8_t> buf(2);
        store16(buf.data(), static_cast<std::uint16_t>(0x1FF << 6), ByteOrder::Big);
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out[0] == 0x1FF);
        CHECK(sampleAt(p, buf.data(), buf.size(), 0, 0) == 0x1FF);
    }

    {
        Params p = makeP(4, 1, Packing::Mipi10, 10);
        std::vector<std::uint16_t> src = {0x001, 0x3FF, 0x155, 0x2AA};
        std::vector<std::uint8_t> buf(5);
        packMipi10(src.data(), 4, buf.data());
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out == src);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(6, 1, Packing::Mipi10, 10);
        std::vector<std::uint16_t> src = {10, 20, 30, 40, 50, 60};
        const int64_t nb = minRowBytes(p);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(nb), 0);
        packMipi10(src.data(), 6, buf.data());
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out == src);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(2, 1, Packing::Mipi12, 12);
        std::vector<std::uint16_t> src = {0x001, 0xFFF};
        std::vector<std::uint8_t> buf(3);
        packMipi12(src.data(), 2, buf.data());
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out == src);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(3, 1, Packing::Mipi12, 12);
        std::vector<std::uint16_t> src = {0x123, 0xABC, 0x0F0};
        const int64_t nb = minRowBytes(p);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(nb), 0);
        packMipi12(src.data(), 3, buf.data());
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out == src);
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(3, 2, Packing::Bits8, 8);
        p.rowStride = 8;
        std::vector<std::uint8_t> buf(16, 0xEE);
        buf[0] = 1;
        buf[1] = 2;
        buf[2] = 3;
        buf[8] = 4;
        buf[9] = 5;
        buf[10] = 6;
        std::vector<std::uint16_t> out;
        CHECK(unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK((out == std::vector<std::uint16_t>{1, 2, 3, 4, 5, 6}));
        checkAllSamples(p, buf.data(), buf.size(), out);
    }

    {
        Params p = makeP(4, 2, Packing::Bits8, 8);
        std::vector<std::uint8_t> buf = {1, 2, 3};
        std::vector<std::uint16_t> out = {9, 9};
        CHECK(!unpackPlainRaw(p, buf.data(), buf.size(), out));
        CHECK(out.empty());
    }

    {
        std::mt19937 rng(20261111);
        for (int w = 1; w <= 13; ++w)
        {
            Params p10 = makeP(w, 2, Packing::Mipi10, 10);
            std::vector<std::uint16_t> src10(static_cast<std::size_t>(w * 2));
            std::uniform_int_distribution<int> d10(0, 1023);
            for (auto &v : src10)
            {
                v = static_cast<std::uint16_t>(d10(rng));
            }
            const int64_t s10 = minRowBytes(p10);
            std::vector<std::uint8_t> b10(static_cast<std::size_t>(s10) * 2u, 0);
            packMipi10(src10.data(), w, b10.data());
            packMipi10(src10.data() + w, w, b10.data() + static_cast<std::size_t>(s10));
            std::vector<std::uint16_t> o10;
            CHECK(unpackPlainRaw(p10, b10.data(), b10.size(), o10));
            CHECK(o10 == src10);
            checkAllSamples(p10, b10.data(), b10.size(), o10);

            Params p12 = makeP(w, 2, Packing::Mipi12, 12);
            std::vector<std::uint16_t> src12(static_cast<std::size_t>(w * 2));
            std::uniform_int_distribution<int> d12(0, 4095);
            for (auto &v : src12)
            {
                v = static_cast<std::uint16_t>(d12(rng));
            }
            const int64_t s12 = minRowBytes(p12);
            std::vector<std::uint8_t> b12(static_cast<std::size_t>(s12) * 2u, 0);
            packMipi12(src12.data(), w, b12.data());
            packMipi12(src12.data() + w, w, b12.data() + static_cast<std::size_t>(s12));
            std::vector<std::uint16_t> o12;
            CHECK(unpackPlainRaw(p12, b12.data(), b12.size(), o12));
            CHECK(o12 == src12);
            checkAllSamples(p12, b12.data(), b12.size(), o12);
        }
    }

    if (g_failures != 0)
    {
        std::printf("test_plainraw_unpack: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_plainraw_unpack: all passed\n");
    return 0;
}
// NOLINTEND
