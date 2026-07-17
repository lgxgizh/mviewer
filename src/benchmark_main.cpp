// MViewer benchmark entry point — measures cache, encode, analysis, compare
// paths
#include "core/analysis/AnalysisEngine.h"
#include "core/benchmark/Benchmark.h"
#include "core/cache/CacheManager.h"
#include "core/compare/CompareEngine.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/Encoder.h"
#include "core/image/ImageCache.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QStandardPaths>
#include <fstream>
#include <iostream>
#include <random>

static QImage makeTestImage(int w, int h, int seed = 42)
{
    QImage img(w, h, QImage::Format_RGB32);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (int y = 0; y < h; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            line[x] = qRgb(dist(rng), dist(rng), dist(rng));
        }
    }
    return img;
}

static void benchDecode()
{
    QImage testImg = makeTestImage(1920, 1080, 1);
    QString tmpPath = QDir::tempPath() + "/mviewer_bench_decode.png";
    testImg.save(tmpPath);

    Benchmark::instance().run(
        "Decoder::decodeFull(1920x1080)",
        [tmpPath]()
        {
            ImageData d = Decoder::decodeFull(tmpPath.toStdString());
            (void)d;
        },
        20);
}

static void benchEncode()
{
    QImage testImg = makeTestImage(1920, 1080, 2);
    ImageData data = mvcore::fromQImage(testImg);
    QString tmpPath = QDir::tempPath() + "/mviewer_bench_encode.jpg";

    Benchmark::instance().run(
        "Encoder::encode(JPEG 1920x1080)",
        [&data, tmpPath]() { Encoder::encode(data, tmpPath.toStdString()); }, 20);
}

static void benchAnalysis()
{
    QImage testImg = makeTestImage(1920, 1080, 3);
    ImageData data = mvcore::fromQImage(testImg);

    Benchmark::instance().run(
        "AnalysisEngine::computeStats(1920x1080)",
        [&data]()
        {
            ImageStats s = AnalysisEngine::computeStats(data);
            (void)s;
        },
        30);

    Benchmark::instance().run(
        "AnalysisEngine::noiseEstimate(1920x1080)",
        [&data]()
        {
            double n = AnalysisEngine::noiseEstimate(data);
            (void)n;
        },
        30);
}

static void benchPSNR()
{
    QImage testImgA = makeTestImage(1920, 1080, 4);
    QImage testImgB = makeTestImage(1920, 1080, 5);
    ImageData a = mvcore::fromQImage(testImgA);
    ImageData b = mvcore::fromQImage(testImgB);

    Benchmark::instance().run(
        "AnalysisEngine::psnr(1920x1080)",
        [&a, &b]()
        {
            double v = AnalysisEngine::psnr(a, b);
            (void)v;
        },
        20);

    Benchmark::instance().run(
        "AnalysisEngine::ssim(1920x1080)",
        [&a, &b]()
        {
            double v = AnalysisEngine::ssim(a, b);
            (void)v;
        },
        10);
}

static void benchCache()
{
    QImage testImg = makeTestImage(800, 600, 6);
    ImageData data = mvcore::fromQImage(testImg);
    std::string key = "bench_cache_key";

    Benchmark::instance().run(
        "ImageCache::put(FullImage)",
        [&data, &key]() { ImageCache::instance().put(ImageCache::Viewer, key, data); }, 100);

    Benchmark::instance().run(
        "ImageCache::get(hit)",
        [&key]()
        {
            ImageData out;
            ImageCache::instance().get(ImageCache::Viewer, key, out);
            (void)out;
        },
        100);

    Benchmark::instance().run(
        "CacheManager::get(Miss)",
        [&key]()
        {
            ImageData out;
            CacheManager::instance().get(CacheLevel::FullImage, "no_such_key_" + key, out);
            (void)out;
        },
        100);
}

// Review P1-10 / P2: thumbnail generation throughput at scale. Generates N
// thumbnails by decoding to a max-edge via Decoder::decodeScaled (the real
// pipeline path the UI uses), measuring end-to-end generation latency.
static void benchThumbnailGen(int n, const std::string &label)
{
    const int thumbEdge = 256;
    QImage base = makeTestImage(1920, 1080, 11);
    QString tmpPath = QDir::tempPath() + "/mviewer_bench_thumb_src.png";
    base.save(tmpPath);
    const std::string path = tmpPath.toStdString();

    Benchmark::instance().run(
        "ThumbnailGen(" + label + ",decodeScaled 256px)",
        [path, thumbEdge]()
        {
            ImageData d = Decoder::decodeScaled(path, thumbEdge);
            (void)d;
        },
        n);
}

// Review P1-10 / P2: zoom latency proxy — scaling a full-res image to a target
// edge across representative zoom levels via RenderEngine::scale.
static void benchZoom()
{
    QImage testImg = makeTestImage(1920, 1080, 12);
    ImageData data = mvcore::fromQImage(testImg);

    const int levels[] = {256, 512, 1024, 1920};
    for (int edge : levels)
    {
        Benchmark::instance().run(
            "RenderEngine::scale->" + std::to_string(edge) + "px",
            [&data, edge]()
            {
                ImageData s = RenderEngine::instance().scale(
                    data, RenderSize{edge, edge * 1080 / 1920}, RenderInterp::Bilinear);
                (void)s;
            },
            20);
    }
}

// Review P1-10 / P2: scroll latency proxy — switching to adjacent images via
// ImageRepository::load (decode + frame build + metadata enrich), the hot path
// used when scrolling through a directory.
static void benchScroll()
{
    const int n = 200;
    std::vector<std::string> paths;
    paths.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        QImage img = makeTestImage(1920, 1080, 20 + i);
        QString p = QDir::tempPath() + "/mviewer_bench_scroll_" + QString::number(i) + ".png";
        img.save(p);
        paths.push_back(p.toStdString());
    }

    Benchmark::instance().run(
        "ImageRepository::load(adjacent switch)",
        [&paths, n]()
        {
            for (int i = 0; i < n; ++i)
            {
                ImageRepository::Result r = ImageRepository::instance().load(paths[i]);
                (void)r;
            }
        },
        1);
}

static void benchROI()
{
    QImage testImg = makeTestImage(1920, 1080, 7);
    ImageData data = mvcore::fromQImage(testImg);
    mviewer::domain::Selection roi = {100, 100, 800, 600};

    Benchmark::instance().run(
        "AnalysisEngine::computeStatsROI(800x600)",
        [&data, &roi]()
        {
            ImageStats s = AnalysisEngine::computeStatsROI(data, roi);
            (void)s;
        },
        30);
}

static void benchDifference()
{
    QImage testImgA = makeTestImage(1920, 1080, 8);
    QImage testImgB = makeTestImage(1920, 1080, 9);
    ImageData a = mvcore::fromQImage(testImgA);
    ImageData b = mvcore::fromQImage(testImgB);

    Benchmark::instance().run(
        "AnalysisEngine::differenceMap(1920x1080)",
        [&a, &b]()
        {
            ImageData d = AnalysisEngine::differenceMap(a, b);
            (void)d;
        },
        20);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::cout << "MViewer Benchmark Suite (M5)" << std::endl;
    std::cout << "Processor QImage(1920x1080) random data" << std::endl << std::endl;

    benchDecode();
    benchEncode();
    benchAnalysis();
    benchPSNR();
    benchROI();
    benchDifference();
    benchCache();
    benchThumbnailGen(1000, "1000");
    benchThumbnailGen(10000, "10000");
    benchZoom();
    benchScroll();

    std::cout << std::endl;
    Benchmark::instance().report();

    // P2-⑫: emit a machine-readable baseline (CSV) so PRs can diff against it.
    const std::string csvPath = "benchmark_results.csv";
    if (Benchmark::instance().reportCsv(csvPath))
        std::cout << "\nBenchmark CSV written to: " << csvPath << std::endl;
    else
        std::cerr << "\nWARNING: failed to write benchmark CSV to " << csvPath << std::endl;

    std::cout << std::endl;
    std::cout << "Cache after benchmarks:" << std::endl;
    std::cout << "  ImageCache totalUsedBytes = " << ImageCache::instance().totalUsedBytes()
              << std::endl;
    std::cout << "  CacheManager memoryUsageBytes = " << CacheManager::instance().memoryUsageBytes()
              << std::endl;
    return 0;
}
