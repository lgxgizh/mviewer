// M3/M4/M5 unit tests: ROI stats, noise estimation, Encoder, CacheManager,
// AnalyzerRegistry, TaskScheduler (dep), CacheConfig/CacheManager stats +
// metadata, new analyzers (RGBMean, Noise, PSNR, SSIM, Entropy, Sharpness),
// RenderEngine backend.
#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/EntropyAnalyzer.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"
#include "core/analyzer/SharpnessAnalyzer.h"
#include "core/cache/CacheManager.h"
#include "core/compare/CompareEngine.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/Encoder.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/scheduler/TaskScheduler.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// test_m3m4m5 isn't given MVIEWER_SOURCE_DIR (see CMakeLists); derive it from the
// source path so golden-image tests don't need a build-definition change.
#ifndef MVIEWER_SOURCE_DIR
static std::string srcRootFromThisFile()
{
    std::string f = __FILE__;
    auto p = f.find("/src/core/test_m3m4m5.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_m3m4m5.cpp");
    return p == std::string::npos ? "." : f.substr(0, p);
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            printf("  PASS: %s\n", msg); \
            g_pass++;                    \
        }                                \
        else                             \
        {                                \
            printf("  FAIL: %s\n", msg); \
            g_fail++;                    \
        }                                \
    } while (0)

// 生成测试图：纯色 + 渐变
static QImage makeTestImage(int w, int h, QColor base, bool gradient = false)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (gradient)
            {
                int v = (x * 255) / w;
                img.setPixel(x, y, qRgb(v, v, v));
            }
            else
            {
                img.setPixel(x, y, base.rgb());
            }
        }
    }
    return img;
}

static void testROIStats()
{
    printf("\n[ROI Stats]\n");
    fflush(stdout);
    QImage img = makeTestImage(200, 200, QColor(100, 150, 200));
    ImageData data = mvcore::fromQImage(img);

    // 全图
    ImageStats full = AnalysisEngine::computeStats(data);
    CHECK(std::abs(full.lumMean - 140.75) < 1.0, "full image lumMean ~140.75");

    CHECK(full.pixelCount == 200 * 200, "full image pixelCount = 40000");

    // ROI: 中心 100x100
    mviewer::domain::Selection roi = {50, 50, 100, 100};
    ImageStats roiStats = AnalysisEngine::computeStatsROI(data, roi);
    CHECK(roiStats.pixelCount == 100 * 100, "ROI pixelCount = 10000");
    CHECK(std::abs(roiStats.lumMean - full.lumMean) < 0.01,
        "ROI lumMean matches full (uniform image)");

    // ROI 超出边界（应自动裁剪）
    mviewer::domain::Selection roiOut = {150, 150, 100, 100};
    ImageStats outStats = AnalysisEngine::computeStatsROI(data, roiOut);
    CHECK(outStats.pixelCount == 50 * 50, "out-of-bounds ROI clipped to 50x50 = 2500");

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
    CHECK(Encoder::formatForExtension("jpg") == "jpeg",
        "formatForExtension(jpg)==jpeg (Qt convention)");
    CHECK(Encoder::formatForExtension("JPEG") == "jpeg", "formatForExtension(JPEG)==jpeg");
    CHECK(Encoder::formatForExtension("png") == "png", "formatForExtension(png)==png");
    CHECK(Encoder::formatForExtension("bmp") == "bmp", "formatForExtension(bmp)==bmp");
    CHECK(Encoder::formatForExtension("webp") == "webp", "formatForExtension(webp)==webp");
    CHECK(Encoder::formatForExtension("unknown") == "png",
        "formatForExtension(unknown)==png (default)");
}

static void testCacheManager()
{
    printf("\n[CacheManager]\n");
    CacheManager& mgr = CacheManager::instance();
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
    fflush(stdout);
    auto& reg = AnalyzerRegistry::instance();

    // HistogramAnalyzer should be auto-registered
    auto ids = reg.availableAnalyzers();
    bool hasHistogram = false;
    for (const auto& id : ids)
    {
        if (id == "histogram")
        {
            hasHistogram = true;
            break;
        }
    }
    CHECK(hasHistogram, "HistogramAnalyzer auto-registered as 'histogram'");

    // Create instance
    auto analyzer = reg.create("histogram");
    CHECK(analyzer != nullptr, "create('histogram') returns non-null");
    if (analyzer)
    {
        CHECK(analyzer->name() == "histogram", "analyzer name == 'histogram'");
    }

    // Nonexistent ID
    CHECK(reg.create("nonexistent") == nullptr, "create('nonexistent') returns null");

    // M4: every built-in analyzer is reachable through the registry and produces
    // a human-readable result (single entry point; no QRect crosses the core API).
    QImage solid(64, 64, QImage::Format_RGB32);
    solid.fill(QColor(120, 160, 200));
    ImageFrame frame = ImageFrame::create("registry-check", mvcore::fromQImage(solid));
    frame.computeHistogram();
    const char* builtins[] = {"histogram", "noise", "entropy", "psnr",
                               "sharpness", "ssim", "rgbmean"};
    for (const char* id : builtins)
    {
        auto a = reg.create(id);
        CHECK(a != nullptr, ("registry creates '" + std::string(id) + "'").c_str());
        if (a)
        {
            // psnr/ssim are reference-based; point them at the same frame so the
            // region analysis has a valid reference.
            if (id == std::string("psnr"))
                dynamic_cast<PSNRAnalyzer*>(a.get())->setReference(frame);
            else if (id == std::string("ssim"))
                dynamic_cast<SSIMAnalyzer*>(a.get())->setReference(frame);
            mviewer::domain::Selection full{0, 0, 64, 64};
            const bool ok = a->analyzeRegion(frame, full);
            CHECK(ok, ("'" + std::string(id) + "' analyzes a region").c_str());
            CHECK(!a->resultText().empty(),
                  ("'" + std::string(id) + "' reports a result").c_str());
        }
    }
}

// M4 acceptance (AC2 + AC3): registry-driven ROI analysis on an arbitrary Selection
// must (a) actually respect the Selection (different regions -> different results)
// and (b) agree with the AnalysisEngine reference on the same region. This is the
// core claim of "Analyze(selection) replaces reading QRect".
static void testAnalyzerRegistryConsistency()
{
    printf("\n[AnalyzerRegistry consistency with AnalysisEngine (AC2/AC3)]\n");
    fflush(stdout);

    // Non-uniform image: left half dark, right half bright.
    const int W = 128, H = 128;
    QImage img(W, H, QImage::Format_RGB32);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            img.setPixel(x, y, x < W / 2 ? qRgb(40, 40, 40) : qRgb(200, 200, 200));
    ImageData data = mvcore::fromQImage(img);
    ImageFrame frame = ImageFrame::create("consistency", mvcore::fromQImage(img));

    auto& reg = AnalyzerRegistry::instance();

    // (AC3) ROI analysis respects the Selection: left vs right half differ.
    mviewer::domain::Selection left{0, 0, W / 2, H};
    mviewer::domain::Selection right{W / 2, 0, W / 2, H};
    auto al = reg.create("rgbmean");
    auto ar = reg.create("rgbmean");
    CHECK(al && ar, "registry creates two rgbmean analyzers");
    CHECK(al->analyzeRegion(frame, left) && ar->analyzeRegion(frame, right),
          "both regions analyze");
    const double lMean = dynamic_cast<RGBMeanAnalyzer*>(al.get())->result().rMean;
    const double rMean = dynamic_cast<RGBMeanAnalyzer*>(ar.get())->result().rMean;
    CHECK(std::abs(lMean - rMean) > 100.0,
          ("left ROI mean (" + std::to_string(lMean) + ") != right ROI mean (" +
           std::to_string(rMean) + "): Selection is honored").c_str());

    // (AC2/AC3) Full-frame RGBMean via registry agrees with AnalysisEngine reference.
    mviewer::domain::Selection full{0, 0, W, H};
    auto af = reg.create("rgbmean");
    CHECK(af->analyzeRegion(frame, full), "full-frame rgbmean analyzes");
    const double regMean = dynamic_cast<RGBMeanAnalyzer*>(af.get())->result().rMean;
    const ImageStats ref = AnalysisEngine::computeStatsROI(data, full);
    CHECK(std::abs(regMean - ref.rMean) < 1.0,
          ("registry rgbmean (" + std::to_string(regMean) +
           ") matches AnalysisEngine rMean (" + std::to_string(ref.rMean) + ")").c_str());

    // (AC2/AC3) Full-frame histogram lumMean via registry agrees with reference.
    auto ah = reg.create("histogram");
    CHECK(ah->analyzeRegion(frame, full), "full-frame histogram analyzes");
    const double regLum = dynamic_cast<HistogramAnalyzer*>(ah.get())->result().lumMean;
    const ImageStats refH = AnalysisEngine::computeStatsROI(data, full);
    CHECK(std::abs(regLum - refH.lumMean) < 1.0,
          ("registry histogram lumMean (" + std::to_string(regLum) +
           ") matches AnalysisEngine lumMean (" + std::to_string(refH.lumMean) + ")").c_str());
}

// ─── New tests ──────────────────────────────────────────────────────────────

static QImage makeColorTest(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, c.rgb());
    return img;
}

static void testTaskSchedulerDependency()
{
    printf("\n[TaskScheduler Dependency]\n");
    auto& sched = TaskScheduler::instance();

    // Test TaskId auto-increment
    auto h1 =
        sched.submit(TaskScheduler::Priority::Background, [](const TaskScheduler::TaskContext&) {});
    auto h2 =
        sched.submit(TaskScheduler::Priority::Background, [](const TaskScheduler::TaskContext&) {});
    CHECK(h1->id != h2->id, "TaskId auto-increments");
    CHECK(h1->id > 0, "TaskId starts from 1");

    // Test submit with dependency (dep on non-existent id — should still accept)
    TaskScheduler::TaskId depId = 999999;
    auto h3 = sched.submit(
        TaskScheduler::Priority::Background, [](const TaskScheduler::TaskContext&) {}, {depId});
    CHECK(h3->id > 0, "Dep task accepted");
    CHECK(h3->dependencies.size() == 1, "Dep recorded");

    // Test handle lookup
    auto found = sched.handle(h3->id);
    CHECK(found != nullptr, "handle() finds task");

    // Test cancelTree (doesn't crash on missing)
    TaskScheduler::cancelTree(depId);

    // Compat submit
    auto h4 = sched.submit(TaskScheduler::DecodePool, []() {}, []() {});
    CHECK(h4->id > 0, "Compat submit works");

    // toPriority mapping
    CHECK(TaskScheduler::toPriority(TaskScheduler::DecodePool) == TaskScheduler::Priority::Decode,
        "toPriority(DecodePool) = Decode");
    CHECK(TaskScheduler::toPriority(TaskScheduler::MetadataPool) ==
              TaskScheduler::Priority::Background,
        "toPriority(MetadataPool) = Background");
}

static void testCacheConfig()
{
    printf("\n[CacheConfig]\n");
    CacheManager& mgr = CacheManager::instance();
    mgr.clear();

    CacheConfig cfg;
    cfg.metadataCacheSize = 1024;
    cfg.thumbnailCacheSize = 2048;
    cfg.previewCacheSize = 4096;
    cfg.viewerCacheSize = 8192;
    cfg.maxDiskCacheEntries = 100;
    mgr.configure(cfg);
    CHECK(mgr.config().viewerCacheSize == 8192, "CacheConfig applied");

    // Stats
    CacheLevelStats s = mgr.levelStats(CacheLevel::FullImage);
    CHECK(s.bytes == 0, "Empty stats show 0 bytes");

    // Metadata store
    mviewer::domain::ImageMetadata meta;
    meta.filePath = "/test.png";
    meta.fileSize = 100;
    mgr.putMetadata("key1", meta);
    CHECK(mgr.hasMetadata("key1"), "Metadata stored");
    mviewer::domain::ImageMetadata meta2;
    CHECK(mgr.getMetadata("key1", meta2), "Metadata retrieved");
    CHECK(meta2.fileSize == 100, "Metadata preserved");

    // Erase clears everything
    QImage img = makeColorTest(16, 16, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    mgr.put(CacheLevel::FullImage, "eraseKey", data);
    mgr.putMetadata("eraseKey", meta);
    mgr.erase("eraseKey");
    CHECK(!mgr.hasMetadata("eraseKey"), "erase clears metadata");

    // Invalidate
    mgr.put(CacheLevel::Thumbnail, "invKey", data);
    mgr.putMetadata("invKey", meta);
    mgr.invalidate("invKey");
    CHECK(!mgr.hasMetadata("invKey"), "invalidate clears metadata");
}

static void testRGBMean()
{
    printf("\n[RGBMeanAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("rgbmean");
    CHECK(a != nullptr, "rgbmean registered");
    if (!a)
        return;

    QImage img = makeColorTest(100, 100, QColor(50, 100, 150));
    ImageData data = mvcore::fromQImage(img);
    ImageFrame frame = ImageFrame::create("/rgbmean.png", data);

    bool ok = a->analyze(frame);
    CHECK(ok, "rgbmean analyze ok");
    auto& ra = static_cast<RGBMeanAnalyzer&>(*a);
    CHECK(std::abs(ra.result().rMean - 50) < 0.5, "rgbmean rMean ~50");
    CHECK(std::abs(ra.result().gMean - 100) < 0.5, "rgbmean gMean ~100");
    CHECK(std::abs(ra.result().bMean - 150) < 0.5, "rgbmean bMean ~150");
    CHECK(ra.result().rStd == 0.0, "rgbmean rStd uniform");

    // Region ROI
    mviewer::domain::Selection roi = {0, 0, 50, 50};
    CHECK(a->analyzeRegion(frame, roi), "rgbmean analyzeRegion ok");
}

static void testNoiseAnalyzer()
{
    printf("\n[NoiseAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("noise");
    CHECK(a != nullptr, "noise registered");
    if (!a)
        return;

    QImage flat = makeColorTest(100, 100, QColor(128, 128, 128));
    ImageData data = mvcore::fromQImage(flat);
    ImageFrame frame = ImageFrame::create("/noise.png", data);

    CHECK(a->analyze(frame), "noise analyze ok");
    auto& na = static_cast<NoiseAnalyzer&>(*a);
    CHECK(na.noiseLevel() < 1.0, "noise flat image ~0");
}

static void testPSNR()
{
    printf("\n[PSNRAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("psnr");
    CHECK(a != nullptr, "psnr registered");
    if (!a)
        return;

    QImage imgA = makeColorTest(64, 64, QColor(100, 100, 100));
    QImage imgB = makeColorTest(64, 64, QColor(110, 110, 110));
    ImageFrame frameA = ImageFrame::create("/a.png", mvcore::fromQImage(imgA));
    ImageFrame frameB = ImageFrame::create("/b.png", mvcore::fromQImage(imgB));

    auto& pa = static_cast<PSNRAnalyzer&>(*a);
    pa.setReference(frameA);
    CHECK(pa.analyze(frameB), "psnr analyze ok");
    CHECK(pa.psnrValue() > 0, "psnr > 0");
    CHECK(pa.psnrValue() < 100, "psnr < 100 (imperfect)");

    // Identical → high PSNR
    CHECK(pa.analyze(frameA), "psnr identical ok");
    CHECK(pa.psnrValue() >= 99, "psnr identical ~100");
}

static void testSSIM()
{
    printf("\n[SSIMAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("ssim");
    CHECK(a != nullptr, "ssim registered");
    if (!a)
        return;

    QImage imgA = makeColorTest(64, 64, QColor(100, 100, 100));
    ImageFrame frameA = ImageFrame::create("/a.png", mvcore::fromQImage(imgA));

    auto& sa = static_cast<SSIMAnalyzer&>(*a);
    sa.setReference(frameA);
    CHECK(sa.analyze(frameA), "ssim identical ok");
    CHECK(sa.ssimValue() >= 0.99, "ssim identical ~1");
}

static void testEntropy()
{
    printf("\n[EntropyAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("entropy");
    CHECK(a != nullptr, "entropy registered");
    if (!a)
        return;

    // Low entropy: uniform image
    QImage flat = makeColorTest(100, 100, QColor(128, 128, 128));
    ImageFrame frameFlat = ImageFrame::create("/flat.png", mvcore::fromQImage(flat));

    auto& ea = static_cast<EntropyAnalyzer&>(*a);
    CHECK(ea.analyze(frameFlat), "entropy flat ok");
    CHECK(ea.entropyValue() == 0.0, "entropy uniform = 0");

    // High entropy: random
    QImage img(100, 100, QImage::Format_RGB32);
    srand(42);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            img.setPixel(x, y, qRgb(rand() % 256, rand() % 256, rand() % 256));
    ImageFrame frameRng = ImageFrame::create("/rng.png", mvcore::fromQImage(img));
    CHECK(ea.analyze(frameRng), "entropy random ok");
    CHECK(ea.entropyValue() > 7.0, "entropy random high");
}

static void testSharpness()
{
    printf("\n[SharpnessAnalyzer]\n");
    auto a = AnalyzerRegistry::instance().create("sharpness");
    CHECK(a != nullptr, "sharpness registered");
    if (!a)
        return;

    QImage flat = makeColorTest(100, 100, QColor(128, 128, 128));
    ImageFrame frameFlat = ImageFrame::create("/flat.png", mvcore::fromQImage(flat));

    auto& sa = static_cast<SharpnessAnalyzer&>(*a);
    CHECK(sa.analyze(frameFlat), "sharpness flat ok");
    CHECK(sa.sharpnessValue() == 0.0, "sharpness uniform = 0");
}

static void testRenderEngine()
{
    printf("\n[RenderEngine]\n");
    auto& engine = RenderEngine::instance();

    // Backend default
    CHECK(engine.scale(ImageData(), {10, 10}).isNull(), "scale null input ok");
    CHECK(engine.overlayDifference(ImageData(), ImageData(), 0.5).isNull(), "overlay null ok");
    CHECK(engine.scaleRegion(ImageData(), {0, 0, 10, 10}, {5, 5}).isNull(), "scaleRegion null ok");

    // Normal scale
    QImage img(100, 100, QImage::Format_RGB32);
    img.fill(qRgb(200, 100, 50));
    ImageData data = mvcore::fromQImage(img);

    ImageData scaled = engine.scale(data, {50, 50});
    CHECK(scaled.width == 50 && scaled.height == 50, "scale 100→50 ok");

    ImageData scaledRoi = engine.scaleRegion(data, {10, 10, 40, 40}, {20, 20});
    CHECK(scaledRoi.width == 20 && scaledRoi.height == 20, "scaleRegion ok");

    ImageData ov = engine.overlayDifference(data, data, 0.5);
    CHECK(!ov.isNull() && ov.width == 100, "overlay ok");
}

static void testCompareControllers()
{
    printf("\n[CompareControllers]\n");
    CompareEngine engine;

    // Image count 0 initially
    CHECK(engine.imageCount() == 0, "CompareEngine starts empty");

    // Load 2 images
    QImage imgA = makeColorTest(64, 64, QColor(100, 100, 100));
    QImage imgB = makeColorTest(64, 64, QColor(200, 200, 200));
    ImageData dataA = mvcore::fromQImage(imgA);
    ImageData dataB = mvcore::fromQImage(imgB);

    // Since CompareEngine loads from disk, we test the struct math directly.
    // (The compare core tests in core_tests already verify the real flow.)

    CompareLayout layout = CompareLayout::forCount(2);
    CHECK(layout.cols == 2 && layout.rows == 1, "Layout for 2 = 2x1");
    CompareLayout layout4 = CompareLayout::forCount(4);
    CHECK(layout4.cols == 2 && layout4.rows == 2, "Layout for 4 = 2x2");
    CompareLayout layout6 = CompareLayout::forCount(6);
    CHECK(layout6.cols == 4 && layout6.rows == 2, "Layout for 6 = 4x2");

    CellSize vp{800, 600};
    CellPoint p0 = layout.cellPos(0, vp);
    CellPoint p1 = layout.cellPos(1, vp);
    CHECK(p0.x == 0 && p1.x == 400, "cellPos 2-cell horizontal");

    CellSize cs = layout.cellSize(vp);
    CHECK(cs.w == 400 && cs.h == 600, "cellSize 2-cell");

    // SyncTransform defaults
    SyncTransform sync;
    CHECK(sync.scale == 1.0, "SyncTransform default scale");
    CHECK(sync.enabled == true, "SyncTransform default enabled");

    // DifferenceEngine standalone
    ImageData diff = DifferenceEngine::differenceMap(dataA, dataB);
    CHECK(!diff.isNull() && diff.width == 64, "DifferenceEngine diff map");
    ImageData heat = DifferenceEngine::heatMap(diff);
    CHECK(!heat.isNull() && heat.format == PixelFormat::RGB24, "DifferenceEngine heatmap");
}

static void testImageFrameExtras()
{
    printf("\n[ImageFrame Selection/Tags/Cache]\n");

    QImage img = makeColorTest(64, 64, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    ImageFrame frame = ImageFrame::create("/tmp.png", data);

    // Selection
    mviewer::domain::Selection sel = {10, 20, 30, 40};
    frame.setSelection(sel);
    CHECK(frame.selection().x == 10, "Selection set");
    CHECK(frame.selection().width == 30, "Selection width");
    frame.clearSelection();
    CHECK(frame.selection().isEmpty(), "Selection cleared");

    // Tags
    frame.addTag("favorite");
    frame.addTag("checked");
    frame.addTag("favorite"); // duplicate → no-op
    CHECK(frame.hasTag("favorite"), "Tag added");
    CHECK(frame.hasTag("checked"), "Tag added");
    CHECK(!frame.hasTag("missing"), "Tag missing");
    frame.removeTag("favorite");
    CHECK(!frame.hasTag("favorite"), "Tag removed");

    // Analysis cache
    frame.setAnalysisResult("rgbmean", true);
    frame.setAnalysisResult("entropy", true);
    auto* e1 = frame.findAnalysis("rgbmean");
    CHECK(e1 != nullptr && e1->ok, "Analysis cache hit");
    CHECK(frame.findAnalysis("missing") == nullptr, "Analysis cache miss");
    frame.clearAnalysisCache();
    CHECK(frame.findAnalysis("rgbmean") == nullptr, "Analysis cache cleared");

    // Render cache
    ImageData thumb = mvcore::fromQImage(makeColorTest(32, 32, QColor(50, 50, 50)));
    RenderCacheEntry rce;
    rce.tag = RenderCacheEntry::Tag::ScaledView;
    rce.data = thumb;
    rce.srcWidth = 1920;
    rce.srcHeight = 1080;
    frame.setRenderCache(rce);
    auto* found = frame.findRenderCache(RenderCacheEntry::Tag::ScaledView);
    CHECK(found != nullptr, "Render cache hit");
    CHECK(found->srcWidth == 1920, "Render cache meta");
    frame.clearRenderCache();
    CHECK(frame.findRenderCache(RenderCacheEntry::Tag::ScaledView) == nullptr,
        "Render cache cleared");
}

static void testCompareSession()
{
    printf("\n[CompareSession]\n");

    mviewer::domain::CompareSession s;
    CHECK(!s.isValid(), "Empty session invalid");
    CHECK(!s.isComparing(), "Empty session not comparing");

    s.imageIds.push_back("/a.png");
    s.imageIds.push_back("/b.png");
    s.imageIds.push_back("/c.png");
    CHECK(s.isValid(), "3-image session valid");
    CHECK(s.isComparing(), "Session is comparing");
    CHECK(s.isSyncOn(), "Default sync on");

    s.syncMode = mviewer::domain::SyncMode::Off;
    CHECK(!s.isSyncOn(), "Sync mode off");

    s.blinkIndex = 1;
    CHECK(s.isBlinking(), "Blinking active");
    s.blinkIndex = -1;
    CHECK(!s.isBlinking(), "Blinking inactive");

    // Viewport defaults
    s.viewport = {1920, 1080, 960, 540, 2, 1};
    CHECK(s.viewport.width == 1920, "Viewport assigned");

    s.selection = {100, 100, 200, 200, true, false};
    CHECK(s.selection.active, "Selection active");
}

static void testRenderCommand()
{
    printf("\n[RenderCommand]\n");

    ImageData dummy;
    auto c1 = RenderCommand::drawImage(dummy, {100, 100}, RenderInterp::Bilinear);
    CHECK(c1.type == RenderCommandType::DrawImage, "DrawImage factory");
    CHECK(c1.interp == static_cast<int>(RenderInterp::Bilinear), "DrawImage interp");

    auto c2 = RenderCommand::drawOverlay(dummy, 0.5);
    CHECK(c2.type == RenderCommandType::DrawOverlay, "DrawOverlay factory");
    CHECK(c2.alpha == 0.5, "DrawOverlay alpha");

    int bins[256] = {0};
    bins[128] = 100;
    auto c3 = RenderCommand::drawHistogram(bins, 256, {0, 0, 256, 100});
    CHECK(c3.type == RenderCommandType::DrawHistogram, "DrawHistogram factory");

    auto c4 = RenderCommand::drawSelection({10, 10, 50, 50}, 0xff00ff);
    CHECK(c4.type == RenderCommandType::DrawSelection, "DrawSelection factory");

    auto c5 = RenderCommand::drawPixelMarker(100, 200, 0xffffff);
    CHECK(c5.type == RenderCommandType::DrawPixelMarker, "DrawPixelMarker factory");
}

static void testAnalyzerCapabilityFramework()
{
    printf("\n[AnalyzerCapability]\n");
    auto& reg = AnalyzerRegistry::instance();

    // All expected analyzers registered
    const char* expected[] = {
        "histogram", "rgbmean", "noise", "psnr", "ssim", "entropy", "sharpness"};
    auto ids = reg.availableAnalyzers();
    for (const char* id : expected)
    {
        bool found = false;
        for (const auto& x : ids)
            if (x == id)
            {
                found = true;
                break;
            }
        CHECK(found, id);
    }

    // Capability query works
    auto cap = reg.capabilitiesOf("histogram");
    CHECK(hasCapability(cap, AnalyzerCapability::SingleImage), "histogram has SingleImage");
    CHECK(!hasCapability(cap, AnalyzerCapability::GPU), "histogram no GPU");

    // Unknown analyzer returns None
    CHECK(reg.capabilitiesOf("nonexistent") == AnalyzerCapability::None, "unknown → None");

    // Capability bitwise ops work
    auto combined = AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
                    AnalyzerCapability::Streaming;
    CHECK(hasCapability(combined, AnalyzerCapability::SingleImage), "bitwise-or keeps SingleImage");
    CHECK(hasCapability(combined, AnalyzerCapability::Streaming), "bitwise-or keeps Streaming");
    CHECK(!hasCapability(combined, AnalyzerCapability::MultiImage), "bitwise-or not MultiImage");
}

// M4 deliverable: difference heatmap overlay in the compare workspace.
// Guards the exact data path CompareWorkspace uses to build the overlay QImage
// (DifferenceEngine::differenceMap -> heatMap), without a QWidget.
static void testCompareDiffOverlay()
{
    printf("\n[Compare diff heatmap overlay (M4)]\n");
    fflush(stdout);

    CompareEngine eng;
    const std::string a = std::string(MVIEWER_SOURCE_DIR) +
                          "/testdata/golden/256x256/checker_256x256.jpg";
    const std::string b = std::string(MVIEWER_SOURCE_DIR) +
                          "/testdata/golden/256x256/flat_color_256x256.jpg";
    eng.setImages({a, b});
    CHECK(eng.imageCount() == 2, "engine loaded 2 images for overlay");

    // Same path CompareWorkspace::rebuildCells uses.
    ImageData diff = eng.differenceMap(1);
    CHECK(!diff.isNull(), "differenceMap(1) non-null");
    CHECK(diff.format == PixelFormat::Grayscale8, "diff is Grayscale8");
    if (!diff.isNull())
    {
        ImageData heat = DifferenceEngine::heatMap(diff);
        CHECK(!heat.isNull(), "heatMap(diff) non-null");
        CHECK(heat.format == PixelFormat::RGB24, "heatmap is RGB24");
        CHECK(heat.width == diff.width && heat.height == diff.height,
              "heatmap preserves diff dimensions");
        // Convert to QImage exactly as the workspace does (core->Qt, not decode).
        QImage q = mvcore::toQImage(heat);
        CHECK(!q.isNull() && q.width() == heat.width, "toQImage(heat) valid");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M3/M4/M5 Unit Tests ===\n");
    fflush(stdout);

    testROIStats();
    testNoiseEstimate();
    testEncoder();
    testCacheManager();
    testAnalyzerRegistry();
    testAnalyzerRegistryConsistency();
    testTaskSchedulerDependency();
    testCacheConfig();
    testRGBMean();
    testNoiseAnalyzer();
    testPSNR();
    testSSIM();
    testEntropy();
    testSharpness();
    testRenderEngine();
    testCompareControllers();
    testImageFrameExtras();
    testCompareSession();
    testRenderCommand();
    testAnalyzerCapabilityFramework();

    testCompareDiffOverlay();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
