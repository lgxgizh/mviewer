#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int g_fail = 0;

static void CHECK(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ─── 1. Null / Empty checks ─────────────────────────────────────────────
    {
        ImageData nullData;
        QImage nullImg = mvcore::toQImage(nullData);
        CHECK(nullImg.isNull(), "toQImage(null) must be null");

        QImage nullRef = mvcore::toQImageRef(nullData);
        CHECK(nullRef.isNull(), "toQImageRef(null) must be null");

        ImageData fromNull = mvcore::fromQImage(QImage());
        CHECK(fromNull.isNull(), "fromQImage(null) must be null");
    }

    // ─── 2. Grayscale8 toQImage & fromQImage ────────────────────────────────
    {
        const int w = 32;
        const int h = 24;
        ImageData src = makeImageData(w, h, PixelFormat::Grayscale8);
        for (int y = 0; y < h; ++y)
        {
            uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uint8_t>((x + y * 3) & 0xFF);
        }

        QImage q = mvcore::toQImage(src);
        CHECK(!q.isNull(), "Grayscale8 toQImage non-null");
        CHECK(q.format() == QImage::Format_Grayscale8, "Grayscale8 format check");
        CHECK(q.width() == w && q.height() == h, "Grayscale8 dims check");

        bool match = true;
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *sl = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            const uint8_t *dl = q.constScanLine(y);
            if (std::memcmp(sl, dl, static_cast<size_t>(w)) != 0)
            {
                match = false;
                break;
            }
        }
        CHECK(match, "Grayscale8 pixel data match");

        // fromQImage roundtrip
        ImageData roundTrip = mvcore::fromQImage(q);
        CHECK(!roundTrip.isNull(), "fromQImage(Grayscale8) non-null");
        CHECK(roundTrip.format == PixelFormat::Grayscale8, "Grayscale8 format preserved");
        CHECK(roundTrip.width == w && roundTrip.height == h, "Grayscale8 dims preserved");

        // Non-owning ref
        QImage ref = mvcore::toQImageRef(src);
        CHECK(!ref.isNull(), "toQImageRef(Grayscale8) non-null");
        CHECK(ref.constBits() == src.buffer->data(), "toQImageRef zero-copy pointer match");
    }

    // ─── 3. RGB24 toQImage & fromQImage ─────────────────────────────────────
    {
        const int w = 64;
        const int h = 48;
        ImageData src = makeImageData(w, h, PixelFormat::RGB24);
        for (int y = 0; y < h; ++y)
        {
            uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
            {
                row[x * 3 + 0] = static_cast<uint8_t>((x * 4) & 0xFF);      // R
                row[x * 3 + 1] = static_cast<uint8_t>((y * 5) & 0xFF);      // G
                row[x * 3 + 2] = static_cast<uint8_t>((x + y + 10) & 0xFF); // B
            }
        }

        QImage q = mvcore::toQImage(src);
        CHECK(!q.isNull(), "RGB24 toQImage non-null");
        CHECK(q.format() == QImage::Format_RGB888, "RGB24 format is RGB888");

        bool match = true;
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *sl = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            const uint8_t *dl = q.constScanLine(y);
            if (std::memcmp(sl, dl, static_cast<size_t>(w) * 3) != 0)
            {
                match = false;
                break;
            }
        }
        CHECK(match, "RGB24 pixel data match");

        // fromQImage direct RGB888 extraction
        ImageData extracted = mvcore::fromQImage(q);
        CHECK(!extracted.isNull(), "fromQImage(RGB888) non-null");
        CHECK(extracted.format == PixelFormat::RGB24, "Format is RGB24");
        CHECK(std::memcmp(src.buffer->data(), extracted.buffer->data(),
                          static_cast<size_t>(w * h * 3)) == 0,
              "fromQImage direct copy matches original RGB24");

        // Non-owning ref
        QImage ref = mvcore::toQImageRef(src);
        CHECK(!ref.isNull() && ref.constBits() == src.buffer->data(),
              "RGB24 toQImageRef zero-copy");
    }

    // ─── 4. BGRA32 toQImage & toQImageRef ───────────────────────────────────
    {
        const int w = 32;
        const int h = 16;
        ImageData src = makeImageData(w, h, PixelFormat::BGRA32);
        for (int y = 0; y < h; ++y)
        {
            uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
            {
                row[x * 4 + 0] = static_cast<uint8_t>(x * 2);         // B
                row[x * 4 + 1] = static_cast<uint8_t>(y * 4);         // G
                row[x * 4 + 2] = static_cast<uint8_t>(x + y);         // R
                row[x * 4 + 3] = static_cast<uint8_t>(200 + (x % 5)); // A
            }
        }

        QImage q = mvcore::toQImage(src);
        CHECK(!q.isNull(), "BGRA32 toQImage non-null");
        CHECK(q.format() == QImage::Format_ARGB32, "BGRA32 format is ARGB32");

        bool match = true;
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *sl = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            const uint8_t *dl = q.constScanLine(y);
            if (std::memcmp(sl, dl, static_cast<size_t>(w) * 4) != 0)
            {
                match = false;
                break;
            }
        }
        CHECK(match, "BGRA32 to ARGB32 memory layout identical (little-endian)");

        QImage ref = mvcore::toQImageRef(src);
        CHECK(!ref.isNull() && ref.constBits() == src.buffer->data(),
              "BGRA32 toQImageRef zero-copy");
    }

    // ─── 5. RGBA32 toQImage with SIMD and Tail Swizzling ────────────────────
    {
        // Use non-power-of-two and odd width to test vector alignment & tail
        const int w = 67;
        const int h = 33;
        ImageData src = makeImageData(w, h, PixelFormat::RGBA32);
        for (int y = 0; y < h; ++y)
        {
            uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
            {
                row[x * 4 + 0] = static_cast<uint8_t>((x * 3) & 0xFF);   // R
                row[x * 4 + 1] = static_cast<uint8_t>((y * 7) & 0xFF);   // G
                row[x * 4 + 2] = static_cast<uint8_t>((x + y) & 0xFF);   // B
                row[x * 4 + 3] = static_cast<uint8_t>((255 - x) & 0xFF); // A
            }
        }

        QImage q = mvcore::toQImage(src);
        CHECK(!q.isNull(), "RGBA32 toQImage non-null");
        CHECK(q.format() == QImage::Format_ARGB32, "RGBA32 format ARGB32");

        bool channelsMatch = true;
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
            {
                QRgb p = q.pixel(x, y);
                if (qRed(p) != row[x * 4 + 0] || qGreen(p) != row[x * 4 + 1] ||
                    qBlue(p) != row[x * 4 + 2] || qAlpha(p) != row[x * 4 + 3])
                {
                    channelsMatch = false;
                    break;
                }
            }
            if (!channelsMatch)
                break;
        }
        CHECK(channelsMatch, "RGBA32 SIMD channel swap correctness across odd dimensions");

        // RGBA32 cannot be zero-copy ref'd (requires channel swap)
        QImage ref = mvcore::toQImageRef(src);
        CHECK(ref.isNull(), "RGBA32 toQImageRef must return null per ADR-002");
    }

    // ─── 6. BGR24 toQImage Channel Swap ─────────────────────────────────────
    {
        const int w = 45;
        const int h = 25;
        ImageData src = makeImageData(w, h, PixelFormat::BGR24);
        for (int y = 0; y < h; ++y)
        {
            uint8_t *row = src.buffer->data() + static_cast<size_t>(y) * src.stride();
            for (int x = 0; x < w; ++x)
            {
                row[x * 3 + 0] = 30; // B
                row[x * 3 + 1] = 60; // G
                row[x * 3 + 2] = 90; // R
            }
        }

        QImage q = mvcore::toQImage(src);
        CHECK(!q.isNull(), "BGR24 toQImage non-null");
        CHECK(q.format() == QImage::Format_RGB888, "BGR24 format RGB888");

        QRgb p = q.pixel(10, 10);
        CHECK(qRed(p) == 90 && qGreen(p) == 60 && qBlue(p) == 30, "BGR24 channel swap to RGB888");

        // Zero-copy ref is valid for BGR24 (maps to QImage::Format_BGR888)
        QImage ref = mvcore::toQImageRef(src);
        CHECK(!ref.isNull() && ref.constBits() == src.buffer->data(),
              "BGR24 toQImageRef zero-copy");
    }

    // ─── 7. fromQImage Format_ARGB32 / Format_RGB32 Direct Extraction ───────
    {
        const int w = 48;
        const int h = 32;
        QImage q(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                q.setPixel(x, y, qRgba(x * 4, y * 4, (x + y) * 2, 200));
            }
        }

        ImageData out = mvcore::fromQImage(q);
        CHECK(!out.isNull(), "fromQImage(ARGB32) non-null");
        CHECK(out.format == PixelFormat::RGB24, "fromQImage(ARGB32) outputs RGB24");
        CHECK(out.width == w && out.height == h, "fromQImage(ARGB32) dims");

        bool match = true;
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *row = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                QRgb p = q.pixel(x, y);
                if (row[x * 3 + 0] != qRed(p) || row[x * 3 + 1] != qGreen(p) ||
                    row[x * 3 + 2] != qBlue(p))
                {
                    match = false;
                    break;
                }
            }
            if (!match)
                break;
        }
        CHECK(match, "fromQImage(ARGB32) direct extraction pixel match");
    }

    // ─── 8. Comprehensive round-trip parity tests ───────────────────────────
    {
        const int w = 32;
        const int h = 24;
        // Test Grayscale8 round-trip
        QImage qg(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                qg.setPixel(x, y, qRgb(x * 7, x * 7, x * 7));
        ImageData dg = mvcore::fromQImage(qg);
        QImage qg2 = mvcore::toQImage(dg);
        CHECK(qg == qg2, "Grayscale8 full QImage roundtrip parity");

        // Test RGB888 round-trip
        QImage qr(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                qr.setPixel(x, y, qRgb(x * 7, y * 9, (x + y) * 3));
        ImageData dr = mvcore::fromQImage(qr);
        QImage qr2 = mvcore::toQImage(dr);
        CHECK(qr == qr2, "RGB888 full QImage roundtrip parity");
    }

    std::fprintf(stderr, "%s: %d failures\n", g_fail == 0 ? "All tests passed" : "Tests failed",
                 g_fail);
    return g_fail == 0 ? 0 : 1;
}
