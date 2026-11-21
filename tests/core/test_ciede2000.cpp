// CIEDE2000 (report feature 6).
//
// The expected values are the published reference pairs from
//   G. Sharma, W. Wu, E. N. Dalal, "The CIEDE2000 Color-Difference Formula:
//   Implementation Notes, Supplementary Test Data, and Mathematical
//   Observations", Color Research and Application 30(1), 2005.
//   https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/
// so this pins the formula against the authors' own verification data rather
// than a hand-picked tolerance.
#include "core/analysis/Ciede2000.h"

#include <cmath>
#include <cstdio>

namespace
{
int g_pass = 0;
int g_fail = 0;

struct Pair
{
    double l1, a1, b1, l2, a2, b2, expected;
};

// Sharma/Wu/Dalal (2005) verification data, all 34 pairs.
const Pair kPairs[] = {
    {50.0000, 2.6772, -79.7751, 50.0000, 0.0000, -82.7485, 2.0425},
    {50.0000, 3.1571, -77.2803, 50.0000, 0.0000, -82.7485, 2.8615},
    {50.0000, 2.8361, -74.0200, 50.0000, 0.0000, -82.7485, 3.4412},
    {50.0000, -1.3802, -84.2814, 50.0000, 0.0000, -82.7485, 1.0000},
    {50.0000, -1.1848, -84.8006, 50.0000, 0.0000, -82.7485, 1.0000},
    {50.0000, -0.9009, -85.5211, 50.0000, 0.0000, -82.7485, 1.0000},
    {50.0000, 0.0000, 0.0000, 50.0000, -1.0000, 2.0000, 2.3669},
    {50.0000, -1.0000, 2.0000, 50.0000, 0.0000, 0.0000, 2.3669},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0009, 7.1792},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0010, 7.1792},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0011, 7.2195},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0012, 7.2195},
    {50.0000, -0.0010, 2.4900, 50.0000, 0.0009, -2.4900, 4.8045},
    {50.0000, -0.0010, 2.4900, 50.0000, 0.0010, -2.4900, 4.8045},
    {50.0000, -0.0010, 2.4900, 50.0000, 0.0011, -2.4900, 4.7461},
    {50.0000, 2.5000, 0.0000, 50.0000, 0.0000, -2.5000, 4.3065},
    {50.0000, 2.5000, 0.0000, 73.0000, 25.0000, -18.0000, 27.1492},
    {50.0000, 2.5000, 0.0000, 61.0000, -5.0000, 29.0000, 22.8977},
    {50.0000, 2.5000, 0.0000, 56.0000, -27.0000, -3.0000, 31.9030},
    {50.0000, 2.5000, 0.0000, 58.0000, 24.0000, 15.0000, 19.4535},
    {50.0000, 2.5000, 0.0000, 50.0000, 3.1736, 0.5854, 1.0000},
    {50.0000, 2.5000, 0.0000, 50.0000, 3.2972, 0.0000, 1.0000},
    {50.0000, 2.5000, 0.0000, 50.0000, 1.8634, 0.5757, 1.0000},
    {50.0000, 2.5000, 0.0000, 50.0000, 3.2592, 0.3350, 1.0000},
    {60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644},
    {63.0109, -31.0961, -5.8663, 62.8187, -29.7946, -4.0864, 1.2630},
    {61.2901, 3.7196, -5.3901, 61.4292, 2.2480, -4.9620, 1.8731},
    {35.0831, -44.1164, 3.7933, 35.0232, -40.0716, 1.5901, 1.8645},
    {22.7233, 20.0904, -46.6940, 23.0331, 14.9730, -42.5619, 2.0373},
    {36.4612, 47.8580, 18.3852, 36.2715, 50.5065, 21.2231, 1.4146},
    {90.8027, -2.0831, 1.4410, 91.1528, -1.6435, 0.0447, 1.4441},
    {90.9257, -0.5406, -0.9208, 88.6381, -0.8985, -0.7239, 1.5381},
    {6.7747, -0.2908, -2.4247, 5.8714, -0.0985, -2.2286, 0.6377},
    {2.0776, 0.0795, -1.1350, 0.9033, -0.0636, -0.5514, 0.9082},
};
} // namespace

int main()
{
    using mviewer::core::ciede2000;
    using mviewer::core::Lab;

    for (const Pair &p : kPairs)
    {
        const double got = ciede2000(Lab{p.l1, p.a1, p.b1}, Lab{p.l2, p.a2, p.b2});
        const bool ok = std::fabs(got - p.expected) < 1e-3;
        if (ok)
            ++g_pass;
        else
        {
            ++g_fail;
            std::printf("  FAIL: dE00(%.4f %.4f %.4f | %.4f %.4f %.4f) = %.4f (expected %.4f)\n",
                        p.l1, p.a1, p.b1, p.l2, p.a2, p.b2, got, p.expected);
        }
    }
    std::printf("  %s: %zu Sharma reference pairs reproduce\n", g_fail == 0 ? "PASS" : "FAIL",
                sizeof(kPairs) / sizeof(kPairs[0]));

    // Identity and symmetry.
    const Lab gray{50.0, 0.0, 0.0};
    const double self = ciede2000(gray, gray);
    const bool selfOk = self == 0.0;
    std::printf("  %s: identical colours give 0 (got %.6f)\n", selfOk ? "PASS" : "FAIL", self);
    if (selfOk)
        ++g_pass;
    else
        ++g_fail;

    const Lab p1{50.0, 2.5, 0.0};
    const Lab p2{60.2574, -34.0099, 36.2677};
    const double forward = ciede2000(p1, p2);
    const double backward = ciede2000(p2, p1);
    const bool symmetric = std::fabs(forward - backward) < 1e-9;
    std::printf("  %s: dE00 is symmetric (%.6f vs %.6f)\n", symmetric ? "PASS" : "FAIL", forward,
                backward);
    if (symmetric)
        ++g_pass;
    else
        ++g_fail;

    // Lab conversion sanity: D65 white maps to L=100, a=b=0.
    const Lab white = mviewer::core::labFromXyz(mviewer::core::Xyz{0.95047, 1.0, 1.08883});
    const bool whiteOk =
        std::fabs(white.L - 100.0) < 1e-6 && std::fabs(white.a) < 1e-6 && std::fabs(white.b) < 1e-6;
    std::printf("  %s: D65 white -> Lab(%.6f %.6f %.6f)\n", whiteOk ? "PASS" : "FAIL", white.L,
                white.a, white.b);
    if (whiteOk)
        ++g_pass;
    else
        ++g_fail;

    std::printf("\n==== CIEDE2000 test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
