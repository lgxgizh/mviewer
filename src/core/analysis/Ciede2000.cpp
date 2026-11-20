#include "core/analysis/Ciede2000.h"

#include <cmath>

namespace mviewer::core
{
namespace
{
// D65, 2° observer, normalized so Y = 1 (the modern convention: XYZ in [0,1]).
constexpr double kRefX = 0.95047;
constexpr double kRefY = 1.00000;
constexpr double kRefZ = 1.08883;
constexpr double kEpsilon = 216.0 / 24389.0; // (6/29)^3
constexpr double kKappa = 24389.0 / 27.0;    // 903.3

double labF(double t)
{
    if (t > kEpsilon)
        return std::cbrt(t);
    return (kKappa * t + 16.0) / 116.0;
}

double degrees(double radians)
{
    double d = radians * 180.0 / 3.14159265358979323846;
    while (d < 0.0)
        d += 360.0;
    while (d >= 360.0)
        d -= 360.0;
    return d;
}
} // namespace

Lab labFromXyz(const Xyz &xyz)
{
    const double fx = labF(xyz.X / kRefX);
    const double fy = labF(xyz.Y / kRefY);
    const double fz = labF(xyz.Z / kRefZ);
    Lab out;
    out.L = 116.0 * fy - 16.0;
    out.a = 500.0 * (fx - fy);
    out.b = 200.0 * (fy - fz);
    return out;
}

double ciede2000(const Lab &reference, const Lab &sample)
{
    const double c1 = std::sqrt(reference.a * reference.a + reference.b * reference.b);
    const double c2 = std::sqrt(sample.a * sample.a + sample.b * sample.b);
    const double cBar = (c1 + c2) / 2.0;
    const double cBar7 = std::pow(cBar, 7.0);
    const double g = 0.5 * (1.0 - std::sqrt(cBar7 / (cBar7 + std::pow(25.0, 7.0))));

    const double a1p = (1.0 + g) * reference.a;
    const double a2p = (1.0 + g) * sample.a;
    const double c1p = std::sqrt(a1p * a1p + reference.b * reference.b);
    const double c2p = std::sqrt(a2p * a2p + sample.b * sample.b);

    const double h1p = (c1p == 0.0) ? 0.0 : degrees(std::atan2(reference.b, a1p));
    const double h2p = (c2p == 0.0) ? 0.0 : degrees(std::atan2(sample.b, a2p));

    const double dLp = sample.L - reference.L;
    const double dCp = c2p - c1p;

    double dhp = 0.0;
    if (c1p * c2p != 0.0)
    {
        double diff = h2p - h1p;
        if (diff > 180.0)
            diff -= 360.0;
        else if (diff < -180.0)
            diff += 360.0;
        dhp = diff;
    }
    const double dHp = 2.0 * std::sqrt(c1p * c2p) * std::sin(dhp * 3.14159265358979323846 / 360.0);

    const double lBarp = (reference.L + sample.L) / 2.0;
    const double cBarp = (c1p + c2p) / 2.0;

    double hBarp;
    if (c1p * c2p == 0.0)
    {
        hBarp = h1p + h2p;
    }
    else
    {
        const double sum = std::fabs(h1p - h2p);
        if (sum <= 180.0)
        {
            hBarp = (h1p + h2p) / 2.0;
        }
        else if (h1p + h2p < 360.0)
        {
            hBarp = (h1p + h2p + 360.0) / 2.0;
        }
        else
        {
            hBarp = (h1p + h2p - 360.0) / 2.0;
        }
    }

    const double pi = 3.14159265358979323846;
    const double t = 1.0 - 0.17 * std::cos((hBarp - 30.0) * pi / 180.0) +
                     0.24 * std::cos(2.0 * hBarp * pi / 180.0) +
                     0.32 * std::cos((3.0 * hBarp + 6.0) * pi / 180.0) -
                     0.20 * std::cos((4.0 * hBarp - 63.0) * pi / 180.0);
    const double dTheta = 30.0 * std::exp(-std::pow((hBarp - 275.0) / 25.0, 2.0));
    const double cBarp7 = std::pow(cBarp, 7.0);
    const double rc = 2.0 * std::sqrt(cBarp7 / (cBarp7 + std::pow(25.0, 7.0)));
    const double sl =
        1.0 + (0.015 * std::pow(lBarp - 50.0, 2.0)) / std::sqrt(20.0 + std::pow(lBarp - 50.0, 2.0));
    const double sc = 1.0 + 0.045 * cBarp;
    const double sh = 1.0 + 0.015 * cBarp * t;
    const double rt = -std::sin(2.0 * dTheta * pi / 180.0) * rc;

    const double termL = dLp / sl;
    const double termC = dCp / sc;
    const double termH = dHp / sh;
    return std::sqrt(termL * termL + termC * termC + termH * termH + rt * termC * termH);
}

} // namespace mviewer::core
