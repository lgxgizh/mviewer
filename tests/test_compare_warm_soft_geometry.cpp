#include "compareworkspace_warm_seed.h"

#include <cstdio>

namespace
{

using mviewer::ui::softPaintGeometry;

bool expect(const char *name, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

} // namespace

int main()
{
    int fails = 0;

    // Full-frame thumb claiming full source geometry → paint at thumb size.
    {
        const auto g = softPaintGeometry(QSize(640, 480), QSize(4000, 3000));
        if (!expect("thumb does not claim full sourceSize",
                    g.sourceSize == QSize(640, 480) &&
                        g.sourceRect == QRect(QPoint(0, 0), QSize(640, 480))))
            ++fails;
    }

    // Explicit claimed size matching the image stays unchanged.
    {
        const auto g = softPaintGeometry(QSize(640, 480), QSize(640, 480));
        if (!expect("matching sizes keep claimed geometry",
                    g.sourceSize == QSize(640, 480) &&
                        g.sourceRect == QRect(QPoint(0, 0), QSize(640, 480))))
            ++fails;
    }

    // Region decode: keep full sourceSize even when the raster is smaller.
    {
        const QRect region(100, 50, 800, 600);
        const auto g = softPaintGeometry(QSize(400, 300), QSize(4000, 3000), region);
        if (!expect("region soft paint keeps full sourceSize",
                    g.sourceSize == QSize(4000, 3000) && g.sourceRect == region))
            ++fails;
    }

    // Missing claimed size falls back to the image.
    {
        const auto g = softPaintGeometry(QSize(320, 240), QSize());
        if (!expect("invalid claimed size uses image size",
                    g.sourceSize == QSize(320, 240) &&
                        g.sourceRect == QRect(QPoint(0, 0), QSize(320, 240))))
            ++fails;
    }

    // Image at least as large as claimed full frame keeps claimed geometry.
    {
        const auto g = softPaintGeometry(QSize(4000, 3000), QSize(4000, 3000));
        if (!expect("full raster keeps claimed geometry",
                    g.sourceSize == QSize(4000, 3000) &&
                        g.sourceRect == QRect(QPoint(0, 0), QSize(4000, 3000))))
            ++fails;
    }

    if (fails != 0)
    {
        std::printf("%d failure(s)\n", fails);
        return 1;
    }
    std::printf("all soft-paint geometry checks passed\n");
    return 0;
}
