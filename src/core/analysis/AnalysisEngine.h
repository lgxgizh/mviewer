#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"

#include <cstdint>
#include <string>
#include <vector>

// Image statistics algorithms (independent of QWidget; the interface exposes only ImageData/std
// types). Internal implementation may use Qt or SIMD; this interface is not bound to Qt.
struct ImageStats
{
    double lumMean = 0;                                             // luminance mean
    double vMean = 0;                                               // HSV-V mean
    double rMean = 0, gMean = 0, bMean = 0;                         // RGB means
    double lumStdDev = 0;                                           // luminance stddev
    double rStdDev = 0, gStdDev = 0, bStdDev = 0;                   // RGB channel stddevs
    int lumMin = 0, lumMax = 0;                                     // luminance min/max
    int rMin = 0, rMax = 0, gMin = 0, gMax = 0, bMin = 0, bMax = 0; // RGB channel min/max
    int64_t histLum[256] = {0};                                     // luminance histogram
    int64_t histV[256] = {0};                                       // HSV-V histogram
    int64_t histR[256] = {0}, histG[256] = {0}, histB[256] = {0};   // RGB histograms
    int64_t pixelCount = 0; // pixels counted (may be < full image for ROI)

    std::string toSummaryString() const;
    std::string toCsvRow() const;
    static std::string csvHeader();
};

class AnalysisEngine
{
  public:
    // Compute statistics for a single image
    static ImageStats computeStats(const ImageData &img);

    // Compute ROI region statistics (region is in image coordinates; out-of-bounds areas are
    // cropped automatically)
    static ImageStats computeStatsROI(const ImageData &img,
                                      const mviewer::domain::Selection &region);

    // Compute the difference between two images (same size required; returns grayscale difference
    // image Grayscale8)
    static ImageData differenceMap(const ImageData &a, const ImageData &b);

    // PSNR (peak signal-to-noise ratio, in dB; same dimensions; a perfect match returns 100)
    static double psnr(const ImageData &a, const ImageData &b);

    // SSIM (structural similarity; same dimensions; returns [-1,1], 1 means identical; simplified
    // grayscale)
    static double ssim(const ImageData &a, const ImageData &b);

    // Noise estimation: Laplacian variance method (a larger return value means stronger noise;
    // typical range 0..500+) Variance of the Laplacian operator on the grayscale image; estimates
    // Gaussian white noise well
    static double noiseEstimate(const ImageData &img);

    // False-color heatmap: map the grayscale difference to blue-green-red (returns RGB24)
    static ImageData heatMap(const ImageData &gray);
};
