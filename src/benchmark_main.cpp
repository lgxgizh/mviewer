// MViewer benchmark entry point — measures cache, encode, analysis, compare
// paths
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QStandardPaths>

#include "core/analysis/AnalysisEngine.h"
#include "core/benchmark/Benchmark.h"
#include "core/cache/CacheManager.h"
#include "core/compare/CompareEngine.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/Encoder.h"
#include "core/image/ImageCache.h"
#include "core/image/QtConvert.h"

#include <fstream>
#include <iostream>
#include <random>

static QImage makeTestImage(int w, int h, int seed = 42) {
  QImage img(w, h, QImage::Format_RGB32);
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  for (int y = 0; y < h; ++y) {
    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
    for (int x = 0; x < w; ++x) {
      line[x] = qRgb(dist(rng), dist(rng), dist(rng));
    }
  }
  return img;
}

static void benchDecode() {
  QImage testImg = makeTestImage(1920, 1080, 1);
  QString tmpPath = QDir::tempPath() + "/mviewer_bench_decode.png";
  testImg.save(tmpPath);

  Benchmark::instance().run(
      "Decoder::decodeFull(1920x1080)",
      [tmpPath]() {
        ImageData d = Decoder::decodeFull(tmpPath.toStdString());
        (void)d;
      },
      20);
}

static void benchEncode() {
  QImage testImg = makeTestImage(1920, 1080, 2);
  ImageData data = mvcore::fromQImage(testImg);
  QString tmpPath = QDir::tempPath() + "/mviewer_bench_encode.jpg";

  Benchmark::instance().run(
      "Encoder::encode(JPEG 1920x1080)",
      [&data, tmpPath]() { Encoder::encode(data, tmpPath.toStdString()); }, 20);
}

static void benchAnalysis() {
  QImage testImg = makeTestImage(1920, 1080, 3);
  ImageData data = mvcore::fromQImage(testImg);

  Benchmark::instance().run(
      "AnalysisEngine::computeStats(1920x1080)",
      [&data]() {
        ImageStats s = AnalysisEngine::computeStats(data);
        (void)s;
      },
      30);

  Benchmark::instance().run(
      "AnalysisEngine::noiseEstimate(1920x1080)",
      [&data]() {
        double n = AnalysisEngine::noiseEstimate(data);
        (void)n;
      },
      30);
}

static void benchPSNR() {
  QImage testImgA = makeTestImage(1920, 1080, 4);
  QImage testImgB = makeTestImage(1920, 1080, 5);
  ImageData a = mvcore::fromQImage(testImgA);
  ImageData b = mvcore::fromQImage(testImgB);

  Benchmark::instance().run(
      "AnalysisEngine::psnr(1920x1080)",
      [&a, &b]() {
        double v = AnalysisEngine::psnr(a, b);
        (void)v;
      },
      20);

  Benchmark::instance().run(
      "AnalysisEngine::ssim(1920x1080)",
      [&a, &b]() {
        double v = AnalysisEngine::ssim(a, b);
        (void)v;
      },
      10);
}

static void benchCache() {
  QImage testImg = makeTestImage(800, 600, 6);
  ImageData data = mvcore::fromQImage(testImg);
  std::string key = "bench_cache_key";

  Benchmark::instance().run(
      "ImageCache::put(FullImage)",
      [&data, &key]() {
        ImageCache::instance().put(ImageCache::Viewer, key, data);
      },
      100);

  Benchmark::instance().run(
      "ImageCache::get(hit)",
      [&key]() {
        ImageData out;
        ImageCache::instance().get(ImageCache::Viewer, key, out);
        (void)out;
      },
      100);

  Benchmark::instance().run(
      "CacheManager::get(Miss)",
      [&key]() {
        ImageData out;
        CacheManager::instance().get(CacheLevel::FullImage,
                                     "no_such_key_" + key, out);
        (void)out;
      },
      100);
}

static void benchROI() {
  QImage testImg = makeTestImage(1920, 1080, 7);
  ImageData data = mvcore::fromQImage(testImg);
  mviewer::domain::Selection roi = {100, 100, 800, 600};

  Benchmark::instance().run(
      "AnalysisEngine::computeStatsROI(800x600)",
      [&data, &roi]() {
        ImageStats s = AnalysisEngine::computeStatsROI(data, roi);
        (void)s;
      },
      30);
}

static void benchDifference() {
  QImage testImgA = makeTestImage(1920, 1080, 8);
  QImage testImgB = makeTestImage(1920, 1080, 9);
  ImageData a = mvcore::fromQImage(testImgA);
  ImageData b = mvcore::fromQImage(testImgB);

  Benchmark::instance().run(
      "AnalysisEngine::differenceMap(1920x1080)",
      [&a, &b]() {
        ImageData d = AnalysisEngine::differenceMap(a, b);
        (void)d;
      },
      20);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  std::cout << "MViewer Benchmark Suite (M5)" << std::endl;
  std::cout << "Processor QImage(1920x1080) random data" << std::endl
            << std::endl;

  benchDecode();
  benchEncode();
  benchAnalysis();
  benchPSNR();
  benchROI();
  benchDifference();
  benchCache();

  std::cout << std::endl;
  Benchmark::instance().report();

  std::cout << std::endl;
  std::cout << "Cache after benchmarks:" << std::endl;
  std::cout << "  ImageCache totalUsedBytes = "
            << ImageCache::instance().totalUsedBytes() << std::endl;
  std::cout << "  CacheManager memoryUsageBytes = "
            << CacheManager::instance().memoryUsageBytes() << std::endl;
  return 0;
}
