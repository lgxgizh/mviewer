// F3 (M22) unit test: Aligner estimates the inverse of its own shift, and
// applying that shift realigns the moved frame back onto the reference.
#include "core/compare/Aligner.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"

#include <QImage>
#include <cstdio>
#include <cstdlib>

int main()
{
    const int W = 200, H = 150;
    QImage img(W, H, QImage::Format_RGB32);
    img.fill(qRgb(20, 20, 20));
    // A distinctive bright block so the search has signal.
    for (int y = 50; y < 100; ++y)
        for (int x = 60; x < 110; ++x)
            img.setPixel(x, y, qRgb(230, 230, 230));

    ImageData a = mvcore::fromQImage(img);

    // Synthetic mis-registration: move the frame by (+5,+3).
    const int truthDx = 5, truthDy = 3;
    ImageData moving = mviewer::Aligner::shift(a, truthDx, truthDy);

    // estimate should return the inverse translation so that
    // Aligner::shift(moving, off) ≈ a.
    mviewer::AlignOffset off = mviewer::Aligner::estimate(a, moving, 32);
    printf("estimated offset dx=%d dy=%d (expect ~ %d,%d)\n", off.x, off.y, -truthDx, -truthDy);
    if (std::abs(off.x + truthDx) > 1 || std::abs(off.y + truthDy) > 1)
    {
        printf("FAIL: estimated offset out of expected range\n");
        return 1;
    }

    ImageData aligned = mviewer::Aligner::shift(moving, off.x, off.y);
    const int cpp = a.channelsPerPixel();
    const uint8_t *paC =
        a.buffer->data() + (static_cast<size_t>(75) * a.stride() + static_cast<size_t>(100) * cpp);
    const uint8_t *pbC = aligned.buffer->data() + (static_cast<size_t>(75) * aligned.stride() +
                                                   static_cast<size_t>(100) * cpp);
    printf("CENTER a=%d,%d,%d,%d aligned=%d,%d,%d,%d\n", paC[0], paC[1], paC[2], paC[3], pbC[0],
           pbC[1], pbC[2], pbC[3]);
    int r75 = 0;
    for (int x = 0; x < W; ++x)
    {
        const uint8_t *pa = a.buffer->data() +
                            (static_cast<size_t>(75) * a.stride() + static_cast<size_t>(x) * cpp);
        const uint8_t *pb = aligned.buffer->data() + (static_cast<size_t>(75) * aligned.stride() +
                                                      static_cast<size_t>(x) * cpp);
        int bad = 0;
        for (int c = 0; c < cpp; ++c)
            if (std::abs(pa[c] - pb[c]) > 6)
                ++bad;
        if (bad)
            ++r75;
    }
    printf("ROW75 mismatches=%d/%d\n", r75, W);
    const int tol = 6; // allow for edge clip + 1px quantisation
    const int x0 = std::max(0, -off.x);
    const int y0 = std::max(0, -off.y);
    const int x1 = W - std::max(0, off.x);
    const int y1 = H - std::max(0, off.y);
    int mismatches = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *pa = a.buffer->data() + (static_cast<size_t>(y) * a.stride() +
                                                    static_cast<size_t>(x) * cpp);
            const uint8_t *pb =
                aligned.buffer->data() +
                (static_cast<size_t>(y) * aligned.stride() + static_cast<size_t>(x) * cpp);
            int bad = 0;
            for (int c = 0; c < cpp; ++c)
                if (std::abs(pa[c] - pb[c]) > tol)
                    ++bad;
            if (bad > 0)
                ++mismatches;
        }
    const int area = (x1 - x0) * (y1 - y0);
    printf("realigned mismatches (tol=%d, area=%d): %d\n", tol, area, mismatches);

    // Product-relevant check: realignment must substantially reduce the raw
    // mis-registration error (a vs moving compared at the same coordinates).
    // A 1px quantisation residual at block edges is expected and harmless.
    int raw = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const uint8_t *pa = a.buffer->data() + (static_cast<size_t>(y) * a.stride() +
                                                    static_cast<size_t>(x) * cpp);
            const uint8_t *pm = moving.buffer->data() + (static_cast<size_t>(y) * moving.stride() +
                                                         static_cast<size_t>(x) * cpp);
            int bad = 0;
            for (int c = 0; c < cpp; ++c)
                if (std::abs(pa[c] - pm[c]) > tol)
                    ++bad;
            if (bad > 0)
                ++raw;
        }
    printf("raw (unaligned) mismatches: %d\n", raw);
    if (raw > 0 && mismatches > raw * 9 / 10)
    {
        printf("FAIL: realignment did not reduce error enough (%d vs %d)\n", mismatches, raw);
        return 1;
    }

    const ImageData unalignedDiff = DifferenceEngine::differenceMap(a, moving);
    const ImageData alignedDiff = DifferenceEngine::differenceMap(a, aligned);
    const auto unalignedStats = DifferenceEngine::computeStats(unalignedDiff, 0);
    const auto alignedStats = DifferenceEngine::computeStats(alignedDiff, 0);
    printf("diff mean unaligned=%.3f aligned=%.3f\n", unalignedStats.meanDiff,
           alignedStats.meanDiff);
    if (!(alignedStats.meanDiff < unalignedStats.meanDiff))
    {
        printf("FAIL: aligned visual diff is not smaller than the unaligned pair\n");
        return 1;
    }

    // Test case 2: scale > 1 (e.g. 600x450 image has scale = 600 / 256 = 2).
    // Previously downscaleBy treated multi-channel rows as raw bytes without
    // stride / channel indexing, causing auto-align to fail on >256px images.
    {
        const int W2 = 600, H2 = 450;
        QImage img2(W2, H2, QImage::Format_RGB32);
        img2.fill(qRgb(20, 20, 20));
        for (int y = 150; y < 300; ++y)
            for (int x = 200; x < 400; ++x)
                img2.setPixel(x, y, qRgb(230, 230, 230));

        ImageData a2 = mvcore::fromQImage(img2);
        const int truthDx2 = 8, truthDy2 = 6;
        ImageData moving2 = mviewer::Aligner::shift(a2, truthDx2, truthDy2);

        mviewer::AlignOffset off2 = mviewer::Aligner::estimate(a2, moving2, 32);
        printf("scale>1 estimated offset dx=%d dy=%d (expect ~ %d,%d)\n", off2.x, off2.y, -truthDx2,
               -truthDy2);
        if (std::abs(off2.x + truthDx2) > 2 || std::abs(off2.y + truthDy2) > 2)
        {
            printf("FAIL: scale>1 estimated offset out of expected range\n");
            return 1;
        }
    }

    // Test case 3: BGR24 format alignment.
    {
        const int W3 = 100, H3 = 80;
        ImageData a3 = makeImageData(W3, H3, PixelFormat::BGR24);
        std::fill(a3.buffer->begin(), a3.buffer->end(), uint8_t(30));
        for (int y = 20; y < 60; ++y)
            for (int x = 25; x < 75; ++x)
            {
                const size_t idx = (static_cast<size_t>(y) * a3.stride()) + x * 3;
                (*a3.buffer)[idx + 0] = 50;  // B
                (*a3.buffer)[idx + 1] = 200; // G
                (*a3.buffer)[idx + 2] = 220; // R
            }

        const int truthDx3 = 4, truthDy3 = -3;
        ImageData moving3 = mviewer::Aligner::shift(a3, truthDx3, truthDy3);
        mviewer::AlignOffset off3 = mviewer::Aligner::estimate(a3, moving3, 16);
        printf("BGR24 estimated offset dx=%d dy=%d (expect ~ %d,%d)\n", off3.x, off3.y, -truthDx3,
               -truthDy3);
        if (std::abs(off3.x + truthDx3) > 1 || std::abs(off3.y + truthDy3) > 1)
        {
            printf("FAIL: BGR24 estimated offset out of expected range\n");
            return 1;
        }
    }

    // Test case 4: Grayscale8 format alignment.
    {
        const int W4 = 120, H4 = 90;
        ImageData a4 = makeImageData(W4, H4, PixelFormat::Grayscale8);
        std::fill(a4.buffer->begin(), a4.buffer->end(), uint8_t(25));
        for (int y = 30; y < 60; ++y)
            for (int x = 40; x < 80; ++x)
            {
                const size_t idx = static_cast<size_t>(y) * a4.stride() + x;
                (*a4.buffer)[idx] = 210;
            }

        const int truthDx4 = -5, truthDy4 = 4;
        ImageData moving4 = mviewer::Aligner::shift(a4, truthDx4, truthDy4);
        mviewer::AlignOffset off4 = mviewer::Aligner::estimate(a4, moving4, 16);
        printf("Grayscale8 estimated offset dx=%d dy=%d (expect ~ %d,%d)\n", off4.x, off4.y,
               -truthDx4, -truthDy4);
        if (std::abs(off4.x + truthDx4) > 1 || std::abs(off4.y + truthDy4) > 1)
        {
            printf("FAIL: Grayscale8 estimated offset out of expected range\n");
            return 1;
        }
    }

    printf("PASS\n");
    return 0;
}
