// P4 — Batch export pipeline acceptance tests.
// Covers resize, watermark, contact sheet, batch rename and PDF export.
#include "core/image/ImageBuffer.h"
#include "core/image/ImageTransform.h"

#include <QGuiApplication>

#include <cstdio>
#include <filesystem>
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

namespace fs = std::filesystem;

static ImageData makeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    ImageData img = makeImageData(w, h, PixelFormat::RGB24);
    auto *p = img.buffer->data();
    for (int i = 0; i < w * h; ++i)
    {
        p[i * 3 + 0] = r;
        p[i * 3 + 1] = g;
        p[i * 3 + 2] = b;
    }
    return img;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    printf("\n[P4 Export pipeline]\n");

    // --- resizeToFit (downscale) ---
    {
        ImageData img = makeSolid(100, 100, 200, 100, 50);
        ImageData r = mviewer::core::resizeToFit(img, 50, 50);
        CHECK(!r.isNull(), "resizeToFit: result not null");
        CHECK(r.width == 50 && r.height == 50, "resizeToFit: 100x100 -> 50x50");
    }

    // --- resizeToFit (no upscale) ---
    {
        ImageData img = makeSolid(100, 100, 10, 20, 30);
        ImageData r = mviewer::core::resizeToFit(img, 200, 200);
        CHECK(r.width == 100 && r.height == 100, "resizeToFit: no upscale of small image");
    }

    // --- resizeByFactor ---
    {
        ImageData img = makeSolid(80, 40, 1, 2, 3);
        ImageData r = mviewer::core::resizeByFactor(img, 0.5);
        CHECK(r.width == 40 && r.height == 20, "resizeByFactor 0.5: 80x40 -> 40x20");
    }

    // --- addTextWatermark (dims preserved) ---
    {
        ImageData img = makeSolid(120, 90, 255, 0, 0);
        ImageData w = mviewer::core::addTextWatermark(
            img, "MViewer", mviewer::core::WatermarkPosition::BottomRight, 0.5, 32);
        CHECK(!w.isNull(), "addTextWatermark: result not null");
        CHECK(w.width == 120 && w.height == 90, "addTextWatermark: dimensions preserved");
    }

    // --- addTextWatermark (empty text -> copy) ---
    {
        ImageData img = makeSolid(30, 30, 0, 0, 0);
        ImageData w = mviewer::core::addTextWatermark(
            img, "", mviewer::core::WatermarkPosition::Center, 0.5, 16);
        CHECK(w.width == 30 && w.height == 30, "addTextWatermark empty: unchanged");
    }

    // --- makeContactSheet ---
    {
        std::vector<ImageData> imgs;
        for (int i = 0; i < 4; ++i)
            imgs.push_back(makeSolid(20, 20, static_cast<uint8_t>(i * 40), 0, 0));
        ImageData sheet = mviewer::core::makeContactSheet(imgs, 2, 20);
        CHECK(!sheet.isNull(), "makeContactSheet: not null");
        // cell = 20 + 2*6 = 32; 2 cols/rows; W=H=2*32+6=70
        CHECK(sheet.width == 70 && sheet.height == 70, "makeContactSheet: 2x2 grid 70x70");
    }

    // --- applyRenamePattern ---
    {
        std::string r1 = mviewer::core::applyRenamePattern("{name}_{seq:3}", "img", "jpg", 4, 10);
        CHECK(r1 == "img_005", "applyRenamePattern {name}_{seq:3} -> img_005");
        std::string r2 =
            mviewer::core::applyRenamePattern("{name}_{n}_of_{total}", "photo", "png", 0, 7);
        CHECK(r2 == "photo_1_of_7", "applyRenamePattern {n}/{total}");
        std::string r3 = mviewer::core::applyRenamePattern("", "keep", "jpg", 0, 1);
        CHECK(r3 == "keep", "applyRenamePattern empty -> original base name");
    }

    // --- writePdf ---
    {
        std::vector<ImageData> imgs;
        imgs.push_back(makeSolid(16, 16, 255, 255, 255));
        imgs.push_back(makeSolid(32, 24, 0, 0, 0));
        const std::string path = "p4_tmp.pdf";
        bool ok = mviewer::core::writePdf(path, imgs, 90);
        CHECK(ok, "writePdf: returned true");
        std::error_code ec;
        CHECK(fs::exists(path, ec), "writePdf: file exists");
        CHECK(fs::file_size(path, ec) > 0, "writePdf: file non-empty");
        {
            FILE *f = fopen(path.c_str(), "rb");
            char hdr[5] = {0, 0, 0, 0, 0};
            if (f)
            {
                size_t n = fread(hdr, 1, 4, f);
                (void)n;
                fclose(f);
            }
            CHECK(std::string(hdr) == "%PDF", "writePdf: starts with %PDF");
        }
        fs::remove(path, ec);
    }

    printf("\n=== P4 export pipeline: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
