#include "core/analysis/PixelInspector.h"

#include <algorithm>
#include <cmath>

namespace mviewer::core
{
    namespace
    {
        inline double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

        // Rec. 709 luma — the luminance used for neighborhood stats.
        inline double luma(double r, double g, double b)
        {
            return 0.2126 * r + 0.7152 * g + 0.0722 * b; // r,g,b in 0..1
        }
    }

    ColorTriple toColorSpace(uint8_t r, uint8_t g, uint8_t b, ColorSpace space)
    {
        const double R = r / 255.0, G = g / 255.0, B = b / 255.0;
        ColorTriple out;

        switch (space)
        {
            case ColorSpace::RGB:
                out.c1 = r;
                out.c2 = g;
                out.c3 = b;
                break;

            case ColorSpace::HSV:
            {
                const double mx = std::max({R, G, B});
                const double mn = std::min({R, G, B});
                const double d = mx - mn;
                double h = 0.0;
                if (d > 1e-9)
                {
                    if (mx == R)
                        h = std::fmod(60.0 * ((G - B) / d), 360.0);
                    else if (mx == G)
                        h = 60.0 * ((B - R) / d + 2.0);
                    else
                        h = 60.0 * ((R - G) / d + 4.0);
                    if (h < 0.0)
                        h += 360.0;
                }
                const double v = mx;
                const double s = mx > 1e-9 ? d / mx : 0.0;
                out.c1 = h;
                out.c2 = s * 100.0;
                out.c3 = v * 100.0;
                break;
            }

            case ColorSpace::Lab:
            {
                // sRGB → linear → XYZ (D65) → Lab.
                auto toLin = [](double c)
                { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
                const double lr = toLin(R), lg = toLin(G), lb = toLin(B);
                const double X = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / 0.95047;
                const double Y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / 1.00000;
                const double Z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / 1.08883;
                auto f = [](double t)
                { return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0); };
                const double fx = f(X), fy = f(Y), fz = f(Z);
                out.c1 = 116.0 * fy - 16.0;
                out.c2 = 500.0 * (fx - fy);
                out.c3 = 200.0 * (fy - fz);
                break;
            }

            case ColorSpace::YUV:
            {
                // BT.601, Y in 0..255, U/V in -128..127. Scaled so a neutral
                // gray (R=G=B) yields exactly U=V=0.
                const double Y = 0.299 * r + 0.587 * g + 0.114 * b;
                out.c1 = Y;
                out.c2 = 0.564 * (b - Y); // 0.564 ≈ 0.5 / (1 - 0.114)
                out.c3 = 0.713 * (r - Y); // 0.713 ≈ 0.5 / (1 - 0.299)
                break;
            }

            case ColorSpace::YCbCr:
            {
                // BT.601, full range, Y/Cb/Cr in 0..255.
                out.c1 = 0.299 * r + 0.587 * g + 0.114 * b;
                out.c2 = -0.168736 * r - 0.331264 * g + 0.5 * b + 128.0;
                out.c3 = 0.5 * r - 0.418688 * g - 0.081312 * b + 128.0;
                break;
            }
        }
        return out;
    }

    NeighborhoodStats neighborhoodStats(const uint8_t *data, int stride, int width,
                                        int height, int cx, int cy, int n)
    {
        NeighborhoodStats s;
        if (!data || width <= 0 || height <= 0 || n < 1 || cx < 0 || cy < 0 ||
            cx >= width || cy >= height)
            return s;

        long sum = 0, sumSq = 0;
        int mn = 255, mx = 0, count = 0;
        const int half = n / 2; // n=1→0, n=3→1, n=5→2, n=7→3
        for (int dy = -half; dy <= half; ++dy)
        {
            const int yy = cy + dy;
            if (yy < 0 || yy >= height)
                continue;
            const uint8_t *row = data + static_cast<size_t>(yy) * stride;
            for (int dx = -half; dx <= half; ++dx)
            {
                const int xx = cx + dx;
                if (xx < 0 || xx >= width)
                    continue;
                const uint8_t *p = row + static_cast<size_t>(xx) * 3;
                const double lum = luma(p[0], p[1], p[2]); // 0..255
                const int v = static_cast<int>(lum + 0.5);
                sum += v;
                sumSq += static_cast<long>(v) * v;
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
                ++count;
            }
        }
        if (count == 0)
            return s;
        const double mean = static_cast<double>(sum) / count;
        const double var = static_cast<double>(sumSq) / count - mean * mean;
        s.mean = mean;
        s.variance = var > 0 ? var : 0.0;
        s.stdDev = std::sqrt(s.variance);
        s.min = mn;
        s.max = mx;
        s.count = count;
        return s;
    }

    const char *colorSpaceLabel(ColorSpace space)
    {
        switch (space)
        {
            case ColorSpace::RGB:   return "RGB";
            case ColorSpace::HSV:   return "HSV";
            case ColorSpace::Lab:   return "Lab";
            case ColorSpace::YUV:   return "YUV";
            case ColorSpace::YCbCr: return "YCbCr";
        }
        return "RGB";
    }
}
