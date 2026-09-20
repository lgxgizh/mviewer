// ImageFileRotate + ImageBuffer rotate90CW/CCW/180 unit tests.
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/QtConvert.h"

#include <QGuiApplication>
#include <QImage>
#include <QImageReader>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

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

static ImageData makeMarker(int w, int h)
{
    // Unique colour per pixel: R=x, G=y, B=128 — makes rotate mapping checkable.
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = d.buffer->data() + (static_cast<size_t>(y) * static_cast<size_t>(w) +
                                             static_cast<size_t>(x)) *
                                                3;
            p[0] = static_cast<uint8_t>(x & 0xFF);
            p[1] = static_cast<uint8_t>(y & 0xFF);
            p[2] = 128;
        }
    }
    return d;
}

static void sample(const ImageData &img, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b)
{
    const uint8_t *p = img.buffer->data() +
                       (static_cast<size_t>(y) * static_cast<size_t>(img.width) +
                        static_cast<size_t>(x)) *
                           3;
    r = p[0];
    g = p[1];
    b = p[2];
}

static void testPixelRotates()
{
    std::cout << "\n[ImageBuffer rotate90CW/CCW/180]\n";
    ImageData src = makeMarker(4, 2); // (0,0)=(0,0,128), (3,1)=(3,1,128)
    uint8_t r, g, b;

    ImageData cw = rotate90CW(src);
    CHECK(cw.width == 2 && cw.height == 4, "CW90 dims 4x2 -> 2x4");
    // src(0,0) -> dst(h-1-0, 0) = (1,0)
    sample(cw, 1, 0, r, g, b);
    CHECK(r == 0 && g == 0 && b == 128, "CW90 maps (0,0) -> (1,0)");
    // src(3,1) -> dst(h-1-1, 3) = (0,3)
    sample(cw, 0, 3, r, g, b);
    CHECK(r == 3 && g == 1 && b == 128, "CW90 maps (3,1) -> (0,3)");

    ImageData ccw = rotate90CCW(src);
    CHECK(ccw.width == 2 && ccw.height == 4, "CCW90 dims 4x2 -> 2x4");
    // src(0,0) -> dst(0, w-1-0) = (0,3)
    sample(ccw, 0, 3, r, g, b);
    CHECK(r == 0 && g == 0 && b == 128, "CCW90 maps (0,0) -> (0,3)");

    ImageData half = rotate180(src);
    CHECK(half.width == 4 && half.height == 2, "180 keeps 4x2");
    sample(half, 3, 1, r, g, b);
    CHECK(r == 0 && g == 0 && b == 128, "180 maps (0,0) -> (3,1)");

    // Four CW steps return to original dims + corner pixel.
    ImageData round = rotate90CW(rotate90CW(rotate90CW(rotate90CW(src))));
    CHECK(round.width == 4 && round.height == 2, "4x CW restores dims");
    sample(round, 0, 0, r, g, b);
    CHECK(r == 0 && g == 0 && b == 128, "4x CW restores (0,0)");
}

static void testFileRoundtrip(const std::string &ext)
{
    std::cout << "\n[rotateImageFile ." << ext << "]\n";
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_imagerotate_test";
    fs::create_directories(dir);
    const fs::path path = dir / ("fixture." + ext);
    const std::string utf8 = path.string();

    ImageData src = makeMarker(6, 4);
    Encoder::Params params;
    params.quality = 95;
    CHECK(Encoder::encode(src, utf8, params), "encode fixture ." + ext);

    auto r1 = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(r1.ok, "CW90 ok ." + ext + " err=" + r1.error);
    CHECK(r1.width == 4 && r1.height == 6, "CW90 dims swapped ." + ext);

    // Reload and verify corner mapping under auto-transform.
    QImageReader reader(QString::fromStdString(utf8));
    reader.setAutoTransform(true);
    QImage img = reader.read();
    CHECK(!img.isNull(), "reload after CW90 ." + ext);
    CHECK(img.width() == 4 && img.height() == 6, "reloaded dims ." + ext);

    ImageData after = mvcore::fromQImage(img);
    uint8_t r, g, b;
    // Original (0,0) after CW90 -> (h-1, 0) = (3, 0) in 4x6? wait src was 6x4,
    // CW: dst w=h_src=4, h=w_src=6; (0,0)->(4-1-0, 0)=(3,0)
    sample(after, 3, 0, r, g, b);
    CHECK(r == 0 && g == 0 && b == 128, "reloaded CW90 corner ." + ext);

    auto r2 = mviewer::core::rotateImageFile(utf8, -90); // back
    CHECK(r2.ok, "CCW90 ok ." + ext + " err=" + r2.error);
    CHECK(r2.width == 6 && r2.height == 4, "CCW90 restores dims ." + ext);

    CHECK(!mviewer::core::isWritableRotateFormat("cr2"), "RAW not writable");
    CHECK(mviewer::core::isWritableRotateFormat("png"), "png writable");
    CHECK(mviewer::core::isWritableRotateFormat(".JPG"), "JPG writable");

    auto bad = mviewer::core::rotateImageFile(utf8, 45);
    CHECK(!bad.ok, "non-90 angle rejected");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    testPixelRotates();
    testFileRoundtrip("png");
    testFileRoundtrip("jpg");

    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "\nAll imagerotate tests passed.\n";
    return 0;
}
