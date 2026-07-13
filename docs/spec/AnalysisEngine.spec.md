# AnalysisEngine Specification

## Overview

AnalysisEngine provides image analysis algorithms: statistics, PSNR, SSIM, noise estimation, and difference maps. It is plugin-based for extensibility.

## API

```cpp
struct ImageStats {
    double lumMean = 0, rMean = 0, gMean = 0, bMean = 0;
    int histLum[256] = {0}, histR[256] = {0}, histG[256] = {0}, histB[256] = {0};
    int pixelCount = 0;
};

class AnalysisEngine {
public:
    // Full-image statistics
    static ImageStats computeStats(const ImageData& img);

    // ROI statistics (clipped to image bounds)
    static ImageStats computeStatsROI(const ImageData& img, const Selection& region);

    // Difference map (grayscale)
    static ImageData differenceMap(const ImageData& a, const ImageData& b);

    // PSNR in dB (100.0 = identical, lower = more different)
    static double psnr(const ImageData& a, const ImageData& b);

    // SSIM [-1, 1] (1.0 = identical)
    static double ssim(const ImageData& a, const ImageData& b);

    // Noise estimate (Laplacian variance, higher = noisier)
    static double noiseEstimate(const ImageData& img);

    // Pseudo-colormap (blue-green-red heatmap from grayscale)
    static ImageData heatMap(const ImageData& gray);
};
```

## Performance

| Operation | Budget |
|-----------|--------|
| computeStats (24MP) | < 20ms |
| psnr (24MP) | < 20ms |
| ssim (24MP) | < 100ms |
| noiseEstimate (24MP) | < 10ms |
| differenceMap (24MP) | < 25ms |

## Error Handling

- Null input → null output (or 0.0 for scalars)
- Size mismatch → minimum dimensions used
- Empty ROI → pixelCount = 0

## Future Analyzers (Plugin)

```cpp
class Analyzer {
    virtual string name() const = 0;
    virtual bool analyze(const ImageFrame& frame) = 0;
};
```

Built-in: HistogramAnalyzer, RGBMeanAnalyzer, NoiseAnalyzer, PSNRAnalyzer, SSIMAnalyzer, EntropyAnalyzer, SharpnessAnalyzer.
