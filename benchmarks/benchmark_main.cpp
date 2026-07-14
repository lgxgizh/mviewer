// Per-scenario benchmark suite: measures each scenario, outputs CSV.
#include <QImage>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QBuffer>
#include <QElapsedTimer>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "benchmarks/benchmark_scenario.h"

#include "core/benchmark/Benchmark.h"
#include "core/cache/CacheManager.h"
#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageCache.h"
#include "core/image/DiskCache.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/compare/CompareEngine.h"
#include "core/image/QtConvert.h"
#include "core/image/ImageFrame.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"
#include "core/analyzer/EntropyAnalyzer.h"
#include "core/analyzer/SharpnessAnalyzer.h"
#include "core/render/RenderEngine.h"
#include "core/compare/DifferenceEngine.h"

static QImage makeTestImage(int w, int h, int seed = 42) {
    QImage img(w, h, QImage::Format_RGB32);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgb(dist(rng), dist(rng), dist(rng));
    }
    return img;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ScenarioBenchmark bench;
    std::string baselinePath;
    double threshold = 1.2; // allowed +20% degradation
    std::string csvPath = (argc > 1 && argv[1][0] != '-')
        ? argv[1]
        : std::string(MVIEWER_SOURCE_DIR) + "/build_msvc/bin/benchmark_results.csv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--baseline" && i+1 < argc) baselinePath = argv[++i];
        else if (a == "--threshold" && i+1 < argc) threshold = std::stod(argv[++i]);
        else if (a == "--csv" && i+1 < argc) csvPath = argv[++i];
        else if (a == "--help") {
            std::cout << "Usage: benchmark_scenario [--baseline <csv>] [--threshold <ratio>] [--csv <path>]\n"
                      << "Defaults: threshold=1.2 (+20% degradation allowed)\n";
            return 0;
        }
    }

    std::cout << "MViewer Engineering Benchmark Suite\n";
    if (!baselinePath.empty())
        std::cout << "Baseline: " << baselinePath << "  threshold: " << threshold << "x\n";

    // Setup: generate test images
    QImage img1080 = makeTestImage(1920, 1080, 1);
    QImage img1080b = makeTestImage(1920, 1080, 2);
    QImage img720 = makeTestImage(1280, 720, 3);
    QImage imgSmall = makeTestImage(256, 256, 4);
    ImageData data1080 = mvcore::fromQImage(img1080);
    ImageData data1080b = mvcore::fromQImage(img1080b);
    ImageData data720 = mvcore::fromQImage(img720);
    ImageData dataSmall = mvcore::fromQImage(imgSmall);

    QString tmpPng = QDir::tempPath() + "/mviewer_bench_decode.png";
    img1080.save(tmpPng);
    QString tmpJpg = QDir::tempPath() + "/mviewer_bench_encode.jpg";

    // Decode (Open File / Open Directory)
    bench.run("Open File", "decodeFull(1920x1080)", [&]() {
        ImageData d = Decoder::decodeFull(tmpPng.toStdString());
        (void)d;
    }, 20);

    bench.run("Open File", "decodeFull(1280x720)", [&]() {
        ImageData d = Decoder::decodeFull(tmpPng.toStdString());
        (void)d;
    }, 20);

    // Switch Image (decode + cache hit)
    bench.run("Switch Image", "cacheHit(FullImage)", [&]() {
        ImageCache::instance().put(ImageCache::Viewer, "switch_key", data1080);
        ImageData out;
        ImageCache::instance().get(ImageCache::Viewer, "switch_key", out);
    }, 100);

    // Thumbnail (Encoder scaled-down)
    bench.run("Thumbnail", "encode(PNG 256x256)", [&]() {
        auto buf = Encoder::encodeToBuffer(dataSmall, "png", {});
        (void)buf;
    }, 50);

    // Compare (DifferenceEngine)
    bench.run("Compare", "differenceMap(1920x1080)", [&]() {
        ImageData d = DifferenceEngine::differenceMap(data1080, data1080b);
        (void)d;
    }, 20);

    bench.run("Compare", "heatMap(1920x1080)", [&]() {
        ImageData d = DifferenceEngine::heatMap(data1080);
        (void)d;
    }, 20);

    // Histogram Analyzer
    bench.run("Histogram", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        HistogramAnalyzer a;
        a.analyze(f);
    }, 10);

    // RGBMean Analyzer
    bench.run("RGBMean", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        RGBMeanAnalyzer a;
        a.analyze(f);
    }, 10);

    // Noise Analyzer
    bench.run("Noise", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        NoiseAnalyzer a;
        a.analyze(f);
    }, 10);

    // PSNR Analyzer
    bench.run("PSNR", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        ImageFrame g = ImageFrame::create("/tmp2.png", data1080b);
        PSNRAnalyzer a;
        a.setReference(f);
        a.analyze(g);
    }, 10);

    // SSIM Analyzer
    bench.run("SSIM", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        ImageFrame g = ImageFrame::create("/tmp2.png", data1080b);
        SSIMAnalyzer a;
        a.setReference(f);
        a.analyze(g);
    }, 5);

    // Entropy Analyzer
    bench.run("Entropy", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        EntropyAnalyzer a;
        a.analyze(f);
    }, 10);

    // Sharpness Analyzer
    bench.run("Sharpness", "analyze(1920x1080)", [&]() {
        ImageFrame f = ImageFrame::create("/tmp.png", data1080);
        SharpnessAnalyzer a;
        a.analyze(f);
    }, 10);

    // Cache
    bench.run("Cache", "memoryUsage", [&]() {
        volatile size_t s = CacheManager::instance().memoryUsageBytes();
        (void)s;
    }, 100);

    // Render Engine scale
    bench.run("Render", "scale(1920x1080→1280x720)", [&]() {
        RenderEngine::instance().scale(data1080, {1280, 720});
    }, 10);

    // CSV output
    bench.writeCsv(csvPath);

    // Baseline comparison
    if (!baselinePath.empty()) {
        auto baseline = ScenarioBenchmark::loadBaseline(baselinePath);
        auto cmp = bench.compare(baseline, threshold);
        int fails = 0;
        std::cout << "\n=== Baseline Comparison (threshold " << threshold << "x) ===\n";
        for (const auto& c : cmp) {
            std::cout << (c.passed ? "PASS  " : "FAIL  ")
                      << c.scenario << "::" << c.name
                      << "  cur=" << c.current_avg_ms << "ms"
                      << "  base=" << c.baseline_avg_ms << "ms"
                      << "  ratio=" << c.ratio << "x\n";
            if (!c.passed) ++fails;
        }
        if (fails > 0) {
            std::cerr << "\n" << fails << " scenario(s) exceeded degradation threshold\n";
            return 2;
        }
        std::cout << "\nAll scenarios within baseline threshold\n";
    }

    return 0;
}
