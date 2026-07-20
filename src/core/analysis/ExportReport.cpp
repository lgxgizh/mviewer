#include "core/analysis/ExportReport.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace mviewer::core
{

namespace
{

// Scan a grayscale diff map for min/mean/max (0..255).
void summarizeDiff(const ImageData &d, double &mn, double &mean, double &mx)
{
    mn = 255.0;
    mx = 0.0;
    double sum = 0.0;
    const int n = d.width * d.height;
    if (n <= 0)
    {
        mn = mean = mx = 0.0;
        return;
    }
    const uint8_t *p = d.buffer->data();
    for (int i = 0; i < n; ++i)
    {
        const double v = static_cast<double>(p[i]);
        if (v < mn)
            mn = v;
        if (v > mx)
            mx = v;
        sum += v;
    }
    mean = sum / static_cast<double>(n);
}

void fillMeanRgb(const ImageFrame &f, double &r, double &g, double &b)
{
    RGBMeanAnalyzer an;
    if (an.analyze(f))
    {
        const RGBMeanAnalyzer::Result res = an.result();
        r = res.rMean;
        g = res.gMean;
        b = res.bMean;
    }
}

void fillNoise(const ImageFrame &f, double &out)
{
    NoiseAnalyzer an;
    if (an.analyze(f))
        out = an.noiseLevel();
}

} // namespace

CompareReport buildCompareReport(const ImageFrame &a, const ImageFrame &b)
{
    CompareReport r;
    r.imageA = a.metadata().filePath;
    r.imageB = b.metadata().filePath;

    if (!a.isValid() || !b.isValid())
        return r;

    // Dual-image quality metrics: reference = A, target = B.
    PSNRAnalyzer psnr;
    psnr.setReference(a);
    if (psnr.analyze(b))
        r.psnr = psnr.psnrValue();

    SSIMAnalyzer ssim;
    ssim.setReference(a);
    if (ssim.analyze(b))
        r.ssim = ssim.ssimValue();

    // Per-image stats.
    fillMeanRgb(a, r.meanR_A, r.meanG_A, r.meanB_A);
    fillMeanRgb(b, r.meanR_B, r.meanG_B, r.meanB_B);
    fillNoise(a, r.noiseA);
    fillNoise(b, r.noiseB);

    // Diff summary.
    const ImageData diff = DifferenceEngine::differenceMap(a.pixels(), b.pixels());
    if (!diff.isNull())
        summarizeDiff(diff, r.diffMin, r.diffMean, r.diffMax);

    return r;
}

ImageData compareDiffImage(const ImageFrame &a, const ImageFrame &b)
{
    if (!a.isValid() || !b.isValid())
        return ImageData{};
    const ImageData diff = DifferenceEngine::differenceMap(a.pixels(), b.pixels());
    if (diff.isNull())
        return ImageData{};
    return DifferenceEngine::heatMap(diff);
}

std::string CompareReport::toJson() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"imageA\": \"" << imageA << "\",\n";
    os << "  \"imageB\": \"" << imageB << "\",\n";
    os << "  \"psnr_dB\": " << psnr << ",\n";
    os << "  \"ssim\": " << ssim << ",\n";
    os << "  \"meanRGB_A\": [" << meanR_A << ", " << meanG_A << ", " << meanB_A << "],\n";
    os << "  \"meanRGB_B\": [" << meanR_B << ", " << meanG_B << ", " << meanB_B << "],\n";
    os << "  \"noise_A\": " << noiseA << ",\n";
    os << "  \"noise_B\": " << noiseB << ",\n";
    os << "  \"diff\": { \"min\": " << diffMin << ", \"mean\": " << diffMean
       << ", \"max\": " << diffMax << " }\n";
    os << "}\n";
    return os.str();
}

std::string CompareReport::toCsv() const
{
    std::ostringstream os;
    os << "imageA,imageB,psnr_dB,ssim,meanR_A,meanG_A,meanB_A,"
          "meanR_B,meanG_B,meanB_B,noise_A,noise_B,diff_min,diff_mean,diff_max\n";
    os << imageA << "," << imageB << "," << psnr << "," << ssim << "," << meanR_A << "," << meanG_A
       << "," << meanB_A << "," << meanR_B << "," << meanG_B << "," << meanB_B << "," << noiseA
       << "," << noiseB << "," << diffMin << "," << diffMean << "," << diffMax << "\n";
    return os.str();
}

} // namespace mviewer::core
