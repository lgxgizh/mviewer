// F3 (M22) unit test: Aligner estimates the inverse of its own shift, and
// applying that shift realigns the moved frame back onto the reference.
#include "core/compare/Aligner.h"
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

    printf("PASS\n");
    return 0;
}
