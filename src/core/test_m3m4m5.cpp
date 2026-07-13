// M3/M4/M5 unit tests: ROI stats, noise estimation, Encoder, CacheManager, AnalyzerRegistry
#include <QCoreApplication>
#include <QImage>
#include <QBuffer>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <string>

#include "core/analysis/AnalysisEngine.h"
#include "core/image/Encoder.h"
#include "core/cache/CacheManager.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/QtConvert.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
    else { printf("  FAIL: %s\n", msg); g_fail++; } \
} while(0)

// 生成测试图：纯色 + 渐变
static QImage makeTestImage(int w, int h, QColor base, bool gradient = false) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (gradient) {
                int v = (x * 255) / w;
                img.setPixel(x, y, qRgb(v, v, v));
            } else {
                img.setPixel(x, y, base.rgb());
            }
        }
    }
    return img;
}

static void testROIStats()
{
    printf("\n[ROI Stats]\n");
    QImage img = makeTestImage(200, 200, QColor(100, 150, 200));
    ImageData data = mvcore::fromQImage(img);

    // 全图
    ImageStats full = AnalysisEngine::computeStats(data);
    CHECK(std::abs(full.lumMean - 140.75) < 1.0, "full image lumMean ~140.75");
    CHECK(full.pixelCount == 200*200, "full image pixelCount = 40000");

    // ROI: 中心 100x100
    mviewer::domain::Selection roi = {50, 50, 100, 100};
    ImageStats roiStats = AnalysisEngine::computeStatsROI(data, roi);
    CHECK(roiStats.pixelCount == 100*100, "ROI pixelCount = 10000");
    CHECK(std::abs(roiStats.lumMean - full.lumMean) < 0.01, "ROI lumMean matches full (uniform image)");

    // ROI 超出边界（应自动裁剪）
    mviewer::domain::Selection roiOut = {150, 150, 100, 100};
    ImageStats outStats = AnalysisEngine::computeStatsROI(data, roiOut);
    CHECK(outStats.pixelCount == 50*50, "out-of-bounds ROI clipped to 50x50 = 2500");

    // 空 ROI
    mviewer::domain::Selection roiEmpty = {0, 0, 0, 0};
    ImageStats emptyStats = AnalysisEngine::computeStatsROI(data, roiEmpty);
    CHECK(emptyStats.pixelCount == 0, "empty ROI pixelCount = 0");
}

static void testNoiseEstimate()
{
    printf("\n[Noise Estimate]\n");
    // 纯色图 → 噪声应接近 0
    QImage flat = makeTestImage(100, 100, QColor(128, 128, 128));
    ImageData flatData = mvcore::fromQImage(flat);
    double noiseFlat = AnalysisEngine::noiseEstimate(flatData);
    CHECK(noiseFlat < 1.0, "flat image noise ~0");

    // 渐变图 → 噪声应较低
    QImage grad = makeTestImage(100, 100, QColor(), true);
    ImageData gradData = mvcore::fromQImage(grad);
    double noiseGrad = AnalysisEngine::noiseEstimate(gradData);
    CHECK(noiseGrad < 50.0, "gradient image noise is low");
}

static void testEncoder()
{
    printf("\n[Encoder]\n");
    QImage img = makeTestImage(64, 64, QColor(200, 100, 50));
    ImageData data = mvcore::fromQImage(img);

    // 编码到内存缓冲
    auto buf = Encoder::encodeToBuffer(data, "png", {});
    CHECK(!buf.empty(), "encodeToBuffer(PNG) non-empty");
    CHECK(buf.size() > 100, "encodeToBuffer(PNG) size > 100");

    // 不同格式
    auto jpgBuf = Encoder::encodeToBuffer(data, "jpg", {90});
    CHECK(!jpgBuf.empty(), "encodeToBuffer(JPG) non-empty");

    // 空数据
    ImageData empty;
    auto emptyBuf = Encoder::encodeToBuffer(empty, "png", {});
    CHECK(emptyBuf.empty(), "encodeToBuffer(empty) returns empty");

    // 格式推断（jpg → jpeg 是 Qt 标准约定）
    CHECK(Encoder::formatForExtension("jpg") == "jpeg", "formatForExtension(jpg)==jpeg (Qt convention)");
    CHECK(Encoder::formatForExtension("JPEG") == "jpeg", "formatForExtension(JPEG)==jpeg");
    CHECK(Encoder::formatForExtension("png") == "png", "formatForExtension(png)==png");
    CHECK(Encoder::formatForExtension("bmp") == "bmp", "formatForExtension(bmp)==bmp");
    CHECK(Encoder::formatForExtension("webp") == "webp", "formatForExtension(webp)==webp");
    CHECK(Encoder::formatForExtension("unknown") == "png", "formatForExtension(unknown)==png (default)");
}

static void testCacheManager()
{
    printf("\n[CacheManager]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clear();

    QImage img = makeTestImage(32, 32, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    std::string key = "test_key_123";

    // 未命中
    ImageData miss;
    CHECK(!mgr.get(CacheLevel::FullImage, key, miss), "CacheManager miss returns false");

    // 写入
    mgr.put(CacheLevel::FullImage, key, data);
    CHECK(mgr.memoryUsageBytes() > 0, "memoryUsageBytes > 0 after put");

    // 命中
    ImageData hit;
    CHECK(mgr.get(CacheLevel::FullImage, key, hit), "CacheManager hit returns true");
    CHECK(hit.width == 32 && hit.height == 32, "hit data dimensions match");

    // 清除
    mgr.clearMemory();
    CHECK(mgr.memoryUsageBytes() == 0, "memoryUsageBytes == 0 after clearMemory");
}

static void testAnalyzerRegistry()
{
    printf("\n[AnalyzerRegistry]\n");
    auto &reg = AnalyzerRegistry::instance();

    // HistogramAnalyzer 应已自注册
    auto ids = reg.availableAnalyzers();
    bool hasHistogram = false;
    for (const auto &id : ids) {
        if (id == "histogram") { hasHistogram = true; break; }
    }
    CHECK(hasHistogram, "HistogramAnalyzer auto-registered as 'histogram'");

    // 创建实例
    auto analyzer = reg.create("histogram");
    CHECK(analyzer != nullptr, "create('histogram') returns non-null");
    if (analyzer) {
        CHECK(analyzer->name() == "histogram", "analyzer name == 'histogram'");
    }

    // 不存在的 ID
    CHECK(reg.create("nonexistent") == nullptr, "create('nonexistent') returns null");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M3/M4/M5 Unit Tests ===\n");

    testROIStats();
    testNoiseEstimate();
    testEncoder();
    testCacheManager();
    testAnalyzerRegistry();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
