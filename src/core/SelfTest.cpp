#include "core/SelfTest.h"

#include "core/image/ImageBuffer.h"
#include "core/image/decoder/DecoderRegistry.h"

#include <QImage>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

namespace mviewer::core
{

int runSelfTest()
{
    int pass = 0;
    int fail = 0;
    auto check = [&](bool c, const char *m)
    {
        if (c)
        {
            printf("  PASS: %s\n", m);
            ++pass;
        }
        else
        {
            printf("  FAIL: %s\n", m);
            ++fail;
        }
    };

    printf("\n[Self-test] core decode roundtrip\n");
    fflush(stdout);

    DecoderRegistry::instance().resetToDefaults();

    // 1) Generate a known 64x64 gradient.
    QImage src(64, 64, QImage::Format_RGB888);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            src.setPixel(x, y, qRgb((x * 4) & 0xFF, (y * 4) & 0xFF, (x + y) & 0xFF));

    // 2) Encode to a temp PNG (exercises the encode/export path).
    QTemporaryDir tmp;
    const std::string pngPath = tmp.path().toStdString() + "/selftest.png";
    if (!src.save(QString::fromStdString(pngPath), "PNG"))
    {
        check(false, "encode PNG for self-test");
        printf("\n=== Self-test: %d passed, %d failed ===\n", pass, fail);
        fflush(stdout);
        return 1;
    }

    // 3) Decode through the registry (the real product decode path).
    mviewer::domain::ImageMetadata meta;
    ImageData d = DecoderRegistry::instance().decodeFull(pngPath, meta);
    check(!d.isNull(), "decode roundtrip non-null");
    check(d.width == 64 && d.height == 64, "decoded dimensions 64x64");

    // 4) Pixel fidelity at the center.
    const int cx = 32, cy = 32;
    if (!d.isNull())
    {
        const uint8_t *p =
            d.buffer->data() + static_cast<size_t>(cy) * d.stride() + static_cast<size_t>(cx) * 3;
        const int r = src.pixelColor(cx, cy).red();
        const int g = src.pixelColor(cx, cy).green();
        const int b = src.pixelColor(cx, cy).blue();
        check(p[0] == static_cast<uint8_t>(r) && p[1] == static_cast<uint8_t>(g) &&
                  p[2] == static_cast<uint8_t>(b),
              "center pixel RGB matches source");
    }

    // 5) Metadata populated.
    check(meta.width == 64 && meta.height == 64, "metadata width/height");

    printf("\n=== Self-test: %d passed, %d failed ===\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

} // namespace mviewer::core
