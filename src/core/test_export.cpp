// M9-4 acceptance: the Export workflow must produce a compare analysis report
// (JSON + CSV) and a diff heatmap PNG from two images. This exercises the REAL
// export path (core::buildCompareReport + core::compareDiffImage + Encoder) —
// it does not fake the result.
//
// Scope is M9-4 ONLY. Browse / Compare / Analysis / Workspace / Polish are
// other phases and are NOT touched here.
#include "core/analysis/ExportReport.h"
#include "core/analysis/ReportHtml.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QImage>

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static QImage makeColorTest(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, c.rgb());
    return img;
}

static ImageFrame makeAdjustedFrame(const std::string &path, int width, int height, uint8_t offset)
{
    ImageData pixels = makeImageData(width, height, PixelFormat::RGB24);
    for (int y = 0; y < height; ++y)
    {
        uint8_t *row = pixels.buffer->data() + static_cast<size_t>(y) * pixels.stride();
        for (int x = 0; x < width; ++x)
        {
            const uint8_t base = static_cast<uint8_t>((x * 17 + y * 11 + offset) % 256);
            row[x * 3] = base;
            row[x * 3 + 1] = static_cast<uint8_t>((base + 23) % 256);
            row[x * 3 + 2] = static_cast<uint8_t>((base + 47) % 256);
        }
    }

    mviewer::domain::ImageMetadata metadata;
    metadata.filePath = path;
    metadata.fileName = path;
    metadata.width = width;
    metadata.height = height;
    return ImageFrame(metadata, pixels);
}

static int csvColumnCount(const std::string &line)
{
    int columns = 1;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
                ++i;
            else
                quoted = !quoted;
        }
        else if (line[i] == ',' && !quoted)
        {
            ++columns;
        }
    }
    return columns;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("\n[M9-4 Export: compare report JSON/CSV + diff PNG]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path outDir = fs::temp_directory_path() / "mviewer_m9_export";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);

    // Two slightly different images (B is darker) so PSNR/SSIM are finite.
    QImage a = makeColorTest(128, 96, QColor(200, 100, 50));
    QImage b = makeColorTest(128, 96, QColor(180, 90, 45));
    ImageData da = mvcore::fromQImage(a);
    ImageData db = mvcore::fromQImage(b);
    CHECK(!da.isNull() && !db.isNull(), "both ImageData valid");

    mviewer::domain::ImageMetadata ma, mb;
    ma.filePath = "a.png";
    ma.fileName = "a.png";
    ma.width = da.width;
    ma.height = da.height;
    mb.filePath = "b.png";
    mb.fileName = "b.png";
    mb.width = db.width;
    mb.height = db.height;
    ImageFrame fa(ma, da);
    ImageFrame fb(mb, db);

    const mviewer::core::CompareReport report = mviewer::core::buildCompareReport(fa, fb);
    printf("  PSNR=%.2f dB  SSIM=%.4f  diffMean=%.2f  noiseA=%.1f\n", report.psnr, report.ssim,
           report.diffMean, report.noiseA);
    CHECK(report.psnr > 0.0, "PSNR computed and positive");
    CHECK(report.ssim >= 0.0 && report.ssim <= 1.0, "SSIM in [0,1]");
    CHECK(report.diffMean >= 0.0, "diff summary mean non-negative");

    // JSON + CSV files.
    const std::string jsonPath = (outDir / "compare_report.json").string();
    const std::string csvPath = (outDir / "compare_report.csv").string();
    {
        std::ofstream jf(jsonPath);
        jf << report.toJson();
    }
    {
        std::ofstream cf(csvPath);
        cf << report.toCsv();
    }
    CHECK(fs::exists(jsonPath, ec), "compare_report.json written");
    CHECK(fs::exists(csvPath, ec), "compare_report.csv written");

    // JSON must contain the metric keys (basic parse check).
    {
        std::ifstream jf(jsonPath);
        std::string content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
        CHECK(content.find("psnr_dB") != std::string::npos, "JSON contains psnr_dB");
        CHECK(content.find("ssim") != std::string::npos, "JSON contains ssim");
        CHECK(content.find("meanRGB_A") != std::string::npos, "JSON contains meanRGB_A");
    }
    // CSV must have a header + data row (2 lines).
    {
        std::ifstream cf(csvPath);
        int lines = 0;
        std::string ln;
        while (std::getline(cf, ln))
            ++lines;
        CHECK(lines == 2, "CSV has header + one data row");
    }

    // M39 adversarial identity round-trip: structured serializers must not
    // emit malformed output for quotes, commas, newlines, HTML delimiters or
    // backslashes in user/file-derived names.
    {
        mviewer::core::CompareReport adversarial = report;
        adversarial.imageA = "a\"b,\n\xE4\xB8\xAD.png";
        adversarial.imageB = "<test>&\\capture.png";
        const QByteArray json = QByteArray::fromStdString(adversarial.toJson());
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(json, &parseError);
        CHECK(parseError.error == QJsonParseError::NoError && parsed.isObject(),
              "adversarial CompareReport JSON parses as structured JSON");
        const std::string csv = adversarial.toCsv();
        const size_t firstData = csv.find('\n');
        CHECK(firstData != std::string::npos && csvColumnCount(csv.substr(firstData + 1)) == 15,
              "adversarial CompareReport CSV remains parseable with quoted identity fields");

        mviewer::core::ReportContext htmlAdversarial;
        htmlAdversarial.title = "<Report & \"review\">";
        htmlAdversarial.imagePath = adversarial.imageB;
        const std::string html = mviewer::core::buildReportHtml(htmlAdversarial);
        CHECK(html.find("&lt;Report &amp; &quot;review&quot;&gt;") != std::string::npos &&
                  html.find("&lt;test&gt;&amp;\\capture.png") != std::string::npos,
              "adversarial report HTML escapes user-derived text");
    }

    // Diff PNG.
    const ImageData diffImg = mviewer::core::compareDiffImage(fa, fb);
    CHECK(!diffImg.isNull(), "diff heatmap produced");
    const std::string pngPath = (outDir / "compare_diff.png").string();
    const bool wrote = Encoder::encode(diffImg, pngPath, Encoder::Params{});
    CHECK(wrote, "compare_diff.png encoded via Encoder");
    CHECK(fs::exists(pngPath, ec), "compare_diff.png written to disk");

    // P0 bundle acceptance: four adjusted frames, with the third as reference.
    const std::vector<ImageFrame> adjustedImages = {
        makeAdjustedFrame("C:\\captures\\target,one.png", 8, 8, 5),
        makeAdjustedFrame("C:\\captures\\target-two.png", 8, 8, 0),
        makeAdjustedFrame("C:\\captures\\reference,\"hero\".png", 8, 8, 0),
        makeAdjustedFrame("C:\\captures\\different-size.png", 9, 8, 25),
    };
    const mviewer::domain::Selection roi{1, 1, 2, 2};
    const std::vector<mviewer::core::CompareAdjustmentState> adjustments = {
        mviewer::core::CompareAdjustmentState{.brightness = 7},
        mviewer::core::CompareAdjustmentState{},
        mviewer::core::CompareAdjustmentState{},
        mviewer::core::CompareAdjustmentState{.gamma = 1.25, .rotation = 90},
    };
    const mviewer::core::CompareReportBundle bundle =
        mviewer::core::buildCompareReportBundle(adjustedImages, 2, 3, roi, adjustments);

    CHECK(bundle.images.size() == 4, "bundle retains all four images in order");
    CHECK(bundle.referenceIndex == 2 && bundle.threshold == 3,
          "bundle retains third-image reference and threshold");
    CHECK(bundle.roi.x == 1 && bundle.roi.y == 1 && bundle.roi.width == 2 && bundle.roi.height == 2,
          "bundle retains ROI");
    CHECK(bundle.adjustments.size() == 4 && !bundle.adjustments[0].isIdentity(),
          "bundle retains four adjustment states including a non-identity state");
    CHECK(bundle.targets.size() == 3, "bundle emits three non-reference target pairs");

    bool foundMismatch = false;
    bool foundComparable = false;
    bool foundIdentical = false;
    for (const auto &pair : bundle.targets)
    {
        CHECK(pair.referenceIndex == 2 && pair.imageA == adjustedImages[2].metadata().filePath,
              "every pair uses the third image as reference imageA");
        if (pair.index == 3)
        {
            foundMismatch = true;
            CHECK(!pair.comparable, "dimension mismatch is explicitly comparable=false");
        }
        else if (pair.index == 0)
        {
            foundComparable = true;
            CHECK(pair.comparable && pair.fullDiffStats.totalPixels == 64,
                  "comparable pair has full threshold-aware DiffStats");
            CHECK(pair.roiDiffStats.has_value() && pair.roiDiffStats->totalPixels == 4,
                  "comparable pair has ROI DiffStats");
            CHECK(pair.fullDiffStats.diffPixels > 0 && pair.fullDiffStats.maxDiff > 0,
                  "DiffStats contain real differences");
        }
        else if (pair.index == 1)
        {
            foundIdentical = true;
            CHECK(pair.comparable && pair.fullDiffStats.diffPixels == 0,
                  "identical non-reference target is comparable with zero diff pixels");
        }
    }
    CHECK(foundComparable && foundIdentical && foundMismatch,
          "bundle covers different, identical, and mismatched targets");

    const std::string bundleJson = bundle.toJson();
    CHECK(bundleJson.find("\"images\"") != std::string::npos &&
              bundleJson.find("\"referenceIndex\": 2") != std::string::npos &&
              bundleJson.find("\"threshold\": 3") != std::string::npos,
          "bundle JSON contains ordered images, referenceIndex, and threshold");
    CHECK(bundleJson.find("\"roi\"") != std::string::npos &&
              bundleJson.find("\"adjustments\"") != std::string::npos &&
              bundleJson.find("\"targets\"") != std::string::npos,
          "bundle JSON contains ROI, adjustments, and targets");
    CHECK(bundleJson.find("\"fullDiffStats\"") != std::string::npos &&
              bundleJson.find("\"roiDiffStats\"") != std::string::npos,
          "bundle JSON serializes full and ROI DiffStats");
    CHECK(bundleJson.find("C:\\\\captures\\\\reference,\\\"hero\\\".png") !=
              std::string::npos,
          "bundle JSON escapes Windows backslashes and quotes");
    CHECK(bundleJson.find("\"comparable\": false") != std::string::npos,
          "bundle JSON serializes an explicit incomparable target");
    CHECK(bundleJson.find("\"comparable\": false, \"psnr_dB\": null, \"ssim\": null, "
                         "\"fullDiffStats\": null, \"roiDiffStats\": null") !=
              std::string::npos,
          "incomparable JSON metrics and stats are null");

    mviewer::core::CompareReportBundle nonFiniteBundle = bundle;
    for (auto &pair : nonFiniteBundle.targets)
    {
        if (pair.index == 1)
        {
            pair.psnr = std::numeric_limits<double>::infinity();
            pair.ssim = std::numeric_limits<double>::quiet_NaN();
        }
    }
    const std::string nonFiniteJson = nonFiniteBundle.toJson();
    CHECK(nonFiniteJson.find("\"psnr_dB\": null") != std::string::npos &&
              nonFiniteJson.find("inf") == std::string::npos &&
              nonFiniteJson.find("nan") == std::string::npos,
          "non-finite JSON metrics are serialized as null");

    const std::string bundleCsv = bundle.toCsv();
    int bundleCsvLines = 0;
    for (const char c : bundleCsv)
        if (c == '\n')
            ++bundleCsvLines;
    CHECK(bundleCsvLines == 4, "bundle CSV contains a header and three target rows");
    CHECK(bundleCsv.find("referenceIndex,imageA,index,path,comparable") != std::string::npos,
          "bundle CSV contains stable pair columns");
    CHECK(bundleCsv.find("\"C:\\captures\\reference,\"\"hero\"\".png\"") !=
              std::string::npos,
          "bundle CSV quotes a Windows path containing comma and quote");
    std::istringstream csvStream(bundleCsv);
    std::string csvLine;
    std::string comparableLine;
    std::string incomparableLine;
    while (std::getline(csvStream, csvLine))
    {
        CHECK(csvColumnCount(csvLine) == 17, "every bundle CSV row has 17 columns");
        if (csvLine.find(",true,") != std::string::npos)
            comparableLine = csvLine;
        if (csvLine.find(",false,") != std::string::npos)
            incomparableLine = csvLine;
    }
    CHECK(!comparableLine.empty() && !incomparableLine.empty(),
          "CSV column checks cover comparable and incomparable rows");
    CHECK(incomparableLine.find(",false,,,,,,,,,,,,") != std::string::npos,
          "incomparable CSV metric and stat cells are empty");

    const mviewer::core::CompareReportBundle outsideRoiBundle =
        mviewer::core::buildCompareReportBundle(
            adjustedImages, 2, 3, mviewer::domain::Selection{99, 99, 2, 2}, adjustments);
    CHECK(!outsideRoiBundle.targets[0].roiDiffStats.has_value(),
          "fully clipped ROI does not produce ROI stats");

    mviewer::core::ReportContext htmlContext;
    htmlContext.title = "Bundle <Report>";
    htmlContext.compareBundle = bundle;
    htmlContext.compareDiffPngs = {"cmVm", "", "dGFyZ2V0"};
    htmlContext.hasCompareBundle = true;
    const std::string bundleHtml = mviewer::core::buildReportHtml(htmlContext);
    CHECK(bundleHtml.find("Locked reference") != std::string::npos &&
              bundleHtml.find("image #3") != std::string::npos,
          "bundle HTML identifies the locked reference and index");
    CHECK(bundleHtml.find("C:\\captures\\reference,&quot;hero&quot;.png") != std::string::npos,
          "bundle HTML escapes the reference path");
    CHECK(bundleHtml.find("C:\\captures\\target,one.png") != std::string::npos &&
              bundleHtml.find("C:\\captures\\target-two.png") != std::string::npos &&
              bundleHtml.find("C:\\captures\\different-size.png") != std::string::npos,
          "bundle HTML contains every target identity");
    CHECK(bundleHtml.find("Threshold") != std::string::npos &&
              bundleHtml.find("x=1, y=1, width=2, height=2") != std::string::npos,
          "bundle HTML contains threshold and ROI provenance");
    CHECK(bundleHtml.find("brightness=7") != std::string::npos &&
              bundleHtml.find("gamma=1.25") != std::string::npos,
          "bundle HTML contains per-image adjustment provenance");
    CHECK(bundleHtml.find("Not comparable") != std::string::npos &&
              bundleHtml.find("N/A — not comparable") != std::string::npos,
          "bundle HTML clearly marks incomparable targets without fake metrics");
    CHECK(bundleHtml.find("data:image/png;base64,cmVm") != std::string::npos &&
              bundleHtml.find("data:image/png;base64,dGFyZ2V0") != std::string::npos,
          "bundle HTML embeds each available pair diff PNG");

    fs::remove_all(outDir, ec);

    printf("\n=== M9-4 Export acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
