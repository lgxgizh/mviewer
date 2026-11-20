#pragma once

// CIEDE2000 colour difference (report feature 6).
//
// Euclidean distance in CIE L*a*b* is a poor predictor of perceived
// difference: it over-reports large chroma differences and ignores the
// non-uniformity of the a* axis. CIEDE2000 is the reference metric for
// "does this reproduction match the target", which is what an ISP review needs.

namespace mviewer::core
{

struct Lab
{
    double L = 0.0;
    double a = 0.0;
    double b = 0.0;
};

struct Xyz
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

/// CIE L*a*b* from CIE XYZ under the D65 / 2° reference white.
Lab labFromXyz(const Xyz &xyz);

/// CIEDE2000 ΔE00. Reference: Sharma, Wu & Dalal (2005).
double ciede2000(const Lab &reference, const Lab &sample);

} // namespace mviewer::core
