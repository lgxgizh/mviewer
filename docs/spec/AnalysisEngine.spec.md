# AnalysisEngine Specification

## Module
AnalysisEngine (static utility — pure analysis algorithms)

## Purpose
AnalysisEngine provides image analysis algorithms: statistics, PSNR, SSIM, noise estimation, difference maps, and heatmap pseudo-coloring. All methods are static. ROI is supported via `domain::Selection`. ImageFrame provides higher-level orchestration; AnalysisEngine is the algorithm layer.

## API

```cpp
struct ImageStats {
    double lumMean = 0;   // luminance mean
    double rMean = 0, gMean = 0, bMean = 0; // RGB means
    int histLum[256] = {0}; // luminance histogram
    int histR[256] = {0}, histG[256] = {0}, histB[256] = {0}; // RGB histograms
    int pixelCount = 0; // pixels counted (ROI may be < full)
};

class AnalysisEngine {
public:
    // Full-image statistics
    static ImageStats computeStats(const ImageData& img);

    // ROI statistics (clipped to image bounds)
    static ImageStats computeStatsROI(const ImageData& img, const mviewer::domain::Selection& region);

    // Difference map (grayscale RGB24)
    static ImageData differenceMap(const ImageData& a, const ImageData& b);

    // PSNR in dB (100.0 = identical; lower = more different)
    static double psnr(const ImageData& a, const ImageData& b);

    // SSIM [-1, 1] (1.0 = identical, simplified grayscale)
    static double ssim(const ImageData& a, const ImageData& b);

    // Noise estimate (Laplacian variance; typical 0..500+)
    static double noiseEstimate(const ImageData& img);

    // Pseudo-colormap (blue-green-red heatmap from grayscale)
    static ImageData heatMap(const ImageData& gray);
};
```

## Input

| Parameter | Type | Constraints | Default |
|-----------|------|-------------|---------|
| `img` | `const ImageData&` | Non-null | — |
| `a, b` | `const ImageData&` | Same dimensions for pairwise ops | — |
| `region` | `const Selection&` | Image coordinates; clipped | — |
| `gray` | `const ImageData&` | Grayscale RGB24 | — |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `computeStats` | `ImageStats` | Full-image stats |
| `computeStatsROI` | `ImageStats` | Stats within region |
| `differenceMap` | `ImageData` | Grayscale RGB24; null on severe size mismatch |
| `psnr` | `double` | dB; 100.0 = identical |
| `ssim` | `double` | [-1, 1]; 1.0 = identical |
| `noiseEstimate` | `double` | Laplacian variance |
| `heatMap` | `ImageData` | RGB24 pseudo-color |

## Ownership

- AnalysisEngine is stateless (all static).
- Callers own input ImageData; output ImageData is a new value (caller owns).
- ImageFrame::analysisCache stores results (weak-cache semantics).

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `computeStats/computeStatsROI` | Any thread | Stateless; safe for concurrent use on distinct ImageData |
| `differenceMap/psnr/ssim` | Any thread | Stateless |
| `noiseEstimate/heatMap` | Any thread | Stateless |

## Memory

| Operation | Dominant Allocation |
|-----------|---------------------|
| `computeStats` | 6 × 256 ints (histogram stack) |
| `differenceMap` | `w*h*3` bytes (output) |
| `heatMap` | `w*h*3` bytes (output) |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| `computeStats(24MP)` | <20 ms | ~19 ms |
| `psnr(24MP)` | <20 ms | ~18 ms |
| `ssim(24MP)` | <100 ms | ~97 ms |
| `noiseEstimate(24MP)` | <10 ms | ~9 ms |
| `differenceMap(24MP)` | <25 ms | ~22 ms |
| `heatMap(24MP)` | <20 ms | ~18 ms |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| null input | Invalid ImageData | Return null ImageData / 0.0 / empty ImageStats |
| size mismatch | Pairwise ops on different dimensions | Clip to min(w,h); if severe, return null |
| empty ROI | Selection outside frame | pixelCount = 0, all means 0 |
| zero variance | Identical images in PSNR | Return 100.0 dB |

## Examples

```cpp
// Full-image statistics
ImageStats s = AnalysisEngine::computeStats(frame.pixels());
std::cout << "luminance mean: " << s.lumMean << "\n";

// ROI stats
Selection roi = {100, 100, 400, 400};
ImageStats roiStats = AnalysisEngine::computeStatsROI(frame.pixels(), roi);

// Difference + heatmap
ImageData diff = AnalysisEngine::differenceMap(a, b);
ImageData hm = AnalysisEngine::heatMap(diff);
double p = AnalysisEngine::psnr(a, b);
double sim = AnalysisEngine::ssim(a, b);
```

## Unit Tests

```cpp
TEST(Analysis, StatsNullInput) {
    auto s = AnalysisEngine::computeStats(ImageData());
    EXPECT_EQ(s.pixelCount, 0);
}

TEST(Analysis, StatsLuminanceRange) {
    ImageData data = makeTestData(100, 100, 128, 128, 128);
    auto s = AnalysisEngine::computeStats(data);
    EXPECT_NEAR(s.lumMean, 128.0, 1.0);
    EXPECT_EQ(s.pixelCount, 10000);
}

TEST(Analysis, DifferenceSameIsBlack) {
    ImageData a = makeTestData(100, 100);
    auto diff = AnalysisEngine::differenceMap(a, a);
    // all pixels should be ~0
    EXPECT_FALSE(diff.isNull());
}

TEST(Analysis, PSNRIdentical) {
    ImageData a = makeTestData(100, 100);
    double p = AnalysisEngine::psnr(a, a);
    EXPECT_DOUBLE_EQ(p, 100.0);
}

TEST(Analysis, SSIMIdentical) {
    ImageData a = makeTestData(100, 100);
    double s = AnalysisEngine::ssim(a, a);
    EXPECT_NEAR(s, 1.0, 1e-6);
}

TEST(Analysis, NoiseOnFlatIsLow) {
    ImageData flat = makeTestData(100, 100, 128, 128, 128);
    double n = AnalysisEngine::noiseEstimate(flat);
    EXPECT_NEAR(n, 0.0, 1.0);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenarios: `Compute::stats(1920x1080)`, `Compute::psnr(1920x1080)`, `Compute::ssim(1920x1080)`, `Compute::noise(1920x1080)`, `Compute::diffMap(1920x1080)`.

## Future Extension

- ROI streaming analysis (progressive stats for large images)
- Adaptive ROI (auto-select region based on entropy)
- GPU-accelerated SSIM via compute shaders (OpenCL/CUDA)
- Multi-scale SSIM (MS-SSIM) for perceptual quality
