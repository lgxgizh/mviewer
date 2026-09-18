// ImageTransform unit tests — rename pattern.
#include "core/image/ImageTransform.h"
#include <QGuiApplication>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << "\n";                                                  \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << "\n";                                                  \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // applyRenamePattern
    auto r1 = mviewer::core::applyRenamePattern("{name}.copy", "photo", "jpg", 0, 10);
    CHECK(r1 == "photo.copy", "{name}.copy");

    auto r2 = mviewer::core::applyRenamePattern("{name}_{seq:3}", "photo", "jpg", 4, 10);
    CHECK(r2 == "photo_005", "{seq:3} zero-pads");

    auto r3 = mviewer::core::applyRenamePattern("img_{n}", "photo", "jpg", 0, 10);
    CHECK(r3 == "img_1", "{n} is 1-based");

    auto r4 = mviewer::core::applyRenamePattern("{name}_of_{total}", "photo", "jpg", 0, 5);
    CHECK(r4 == "photo_of_5", "{total}");

    auto r5 = mviewer::core::applyRenamePattern("{name}_v2.{ext}", "photo", "png", 0, 10);
    CHECK(r5 == "photo_v2.png", "{ext}");

    // Empty pattern
    auto r6 = mviewer::core::applyRenamePattern("", "file", "txt", 0, 1);
    CHECK(!r6.empty() || r6 == "file", "empty pattern handled");

    // A pattern produces a FILE NAME, never a path: BatchProcessor and ExportJob
    // join the result straight onto the destination directory, so directory
    // components and parent references must not survive.
    auto t1 = mviewer::core::applyRenamePattern("../../{name}", "photo", "jpg", 0, 1);
    CHECK(t1 == "photo", "traversal prefix is stripped");
    auto t2 = mviewer::core::applyRenamePattern("{name}/../{name}", "photo", "jpg", 0, 1);
    CHECK(t2 == "photo", "parent reference is stripped");
    auto t3 = mviewer::core::applyRenamePattern("..", "photo", "jpg", 0, 1);
    CHECK(t3 == "photo", "a dot-only pattern falls back to the base name");
    auto t4 = mviewer::core::applyRenamePattern("sub/{name}_x", "photo", "jpg", 0, 1);
    CHECK(t4 == "photo_x", "directory prefix is dropped, the leaf survives");

    // resizeToFit
    ImageData src100x200 = makeImageData(100, 200, PixelFormat::RGB24);
    auto fit1 = mviewer::core::resizeToFit(src100x200, 50, 50);
    CHECK(!fit1.isNull() && fit1.width == 25 && fit1.height == 50,
          "resizeToFit aspect ratio preserved");

    auto fitNoUpscale = mviewer::core::resizeToFit(src100x200, 300, 400);
    CHECK(!fitNoUpscale.isNull() && fitNoUpscale.width == 100 && fitNoUpscale.height == 200,
          "resizeToFit does not upscale smaller images");

    auto fitNull = mviewer::core::resizeToFit(ImageData(), 50, 50);
    CHECK(fitNull.isNull(), "resizeToFit handles null input");

    // resizeByFactor
    auto rFactorHalf = mviewer::core::resizeByFactor(src100x200, 0.5);
    CHECK(!rFactorHalf.isNull() && rFactorHalf.width == 50 && rFactorHalf.height == 100,
          "resizeByFactor 0.5x halves dimensions");

    auto rFactorNan =
        mviewer::core::resizeByFactor(src100x200, std::numeric_limits<double>::quiet_NaN());
    CHECK(rFactorNan.isNull(), "resizeByFactor rejects NaN factor");

    auto rFactorInf =
        mviewer::core::resizeByFactor(src100x200, std::numeric_limits<double>::infinity());
    CHECK(rFactorInf.isNull(), "resizeByFactor rejects Inf factor");

    auto rFactorNeg = mviewer::core::resizeByFactor(src100x200, -1.0);
    CHECK(rFactorNeg.isNull(), "resizeByFactor rejects negative factor");

    auto rFactorHuge = mviewer::core::resizeByFactor(src100x200, 100000.0);
    CHECK(rFactorHuge.isNull(), "resizeByFactor rejects excessive pixel budget");

    // makeContactSheet
    std::vector<ImageData> sheetImgs;
    for (int i = 0; i < 4; ++i)
    {
        ImageData tile = makeImageData(32, 32, PixelFormat::RGB24);
        std::memset(tile.buffer->data(), 50 + i * 40, tile.buffer->size());
        sheetImgs.push_back(tile);
    }
    auto sheet = mviewer::core::makeContactSheet(sheetImgs, 2, 32);
    CHECK(!sheet.isNull() && sheet.width > 0 && sheet.height > 0,
          "makeContactSheet generates valid sheet");

    auto sheetZeroThumb = mviewer::core::makeContactSheet(sheetImgs, 2, 0);
    CHECK(!sheetZeroThumb.isNull(), "makeContactSheet safely clamps non-positive thumb size");

    auto sheetEmpty = mviewer::core::makeContactSheet({}, 2, 32);
    CHECK(sheetEmpty.isNull(), "makeContactSheet returns empty on empty inputs");

    // addTextWatermark
    auto wmZeroOp = mviewer::core::addTextWatermark(
        src100x200, "CONFIDENTIAL", mviewer::core::WatermarkPosition::Center, 0.0, 16);
    CHECK(!wmZeroOp.isNull() && wmZeroOp.width == 100 && wmZeroOp.height == 200,
          "addTextWatermark returns unchanged image on zero opacity");

    auto wmEmptyText = mviewer::core::addTextWatermark(
        src100x200, "", mviewer::core::WatermarkPosition::Center, 0.5, 16);
    CHECK(!wmEmptyText.isNull(), "addTextWatermark returns unchanged image on empty text");

    auto wmValid = mviewer::core::addTextWatermark(
        src100x200, "DRAFT", mviewer::core::WatermarkPosition::TopRight, 0.8, 16);
    CHECK(!wmValid.isNull() && wmValid.width == 100 && wmValid.height == 200,
          "addTextWatermark applies valid watermark");

    // writePdf: verify PDF specification compliance (xref table size & trailer /Size)
    std::vector<ImageData> pdfPages;
    for (int i = 0; i < 2; ++i)
    {
        ImageData pg = makeImageData(64, 64, PixelFormat::RGB24);
        std::memset(pg.buffer->data(), 100 + i * 50, pg.buffer->size());
        pdfPages.push_back(pg);
    }
    std::error_code ec;
    const auto tmpDir = std::filesystem::temp_directory_path(ec);
    CHECK(!ec && !tmpDir.empty(), "temp directory is available");
    const std::string pdfPath = (tmpDir / "mviewer_imagetransform_export.pdf").string();
    const bool pdfOk = mviewer::core::writePdf(pdfPath, pdfPages, 85);
    CHECK(pdfOk, "writePdf succeeds on multi-page export");

    // Read back and inspect PDF structure
    std::ifstream pdfIn(pdfPath, std::ios::binary);
    std::string pdfContent((std::istreambuf_iterator<char>(pdfIn)),
                           std::istreambuf_iterator<char>());
    pdfIn.close();

    CHECK(pdfContent.rfind("%PDF-1.3", 0) == 0, "PDF header matches %PDF-1.3");
    // For K=2 pages: M = 2 + 3*K = 8.
    // Xref subsection must be '0 9' (M+1 entries: obj 0 to obj 8)
    CHECK(pdfContent.find("xref\n0 9\n") != std::string::npos,
          "xref subsection header is '0 9' (M+1)");
    // Trailer /Size must be 9
    CHECK(pdfContent.find("/Size 9") != std::string::npos, "trailer /Size matches M+1 (9)");
    // Final image object (8 0 obj) must be defined and included in xref
    CHECK(pdfContent.find("8 0 obj") != std::string::npos, "final image object 8 0 obj is present");

    std::cout << "\nImageTransform: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
