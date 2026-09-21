// ImageFileRotate + ImageBuffer rotate90CW/CCW/180 unit tests.
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/QtConvert.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << (msg) << "\n";                                                \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << (msg) << "\n";                                                \
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
            uint8_t *p =
                d.buffer->data() +
                (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            p[0] = static_cast<uint8_t>(x & 0xFF);
            p[1] = static_cast<uint8_t>(y & 0xFF);
            p[2] = 128;
        }
    }
    return d;
}

static void sample(const ImageData &img, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b)
{
    const uint8_t *p =
        img.buffer->data() +
        (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 3;
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
    // Unique per-run file so Windows share locks / leftover handles cannot
    // poison the next rotateImageFile overwrite.
    const fs::path path = dir / ("fixture_" + ext + "_" +
                                 std::to_string(static_cast<long long>(
                                     std::chrono::steady_clock::now().time_since_epoch().count())) +
                                 "." + ext);
    const std::string utf8 = path.string();

    ImageData src = makeMarker(6, 4);
    Encoder::Params params;
    params.quality = 95;
    CHECK(Encoder::encode(src, utf8, params), "encode fixture ." + ext);

    auto r1 = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(r1.ok, "CW90 ok ." + ext + " err=" + r1.error);
    CHECK(r1.width == 4 && r1.height == 6, "CW90 dims swapped ." + ext);

    // Reload and verify corner mapping under auto-transform. Scope the
    // QImageReader so Windows releases the share lock before the next overwrite.
    {
        QImageReader reader(QString::fromStdString(utf8));
        reader.setAutoTransform(true);
        QImage img = reader.read();
        CHECK(!img.isNull(), "reload after CW90 ." + ext);
        CHECK(img.width() == 4 && img.height() == 6, "reloaded dims ." + ext);

        ImageData after = mvcore::fromQImage(img);
        uint8_t r, g, b;
        // Original (0,0) after CW90 -> (h-1, 0) = (3, 0); src was 6x4,
        // CW: dst w=h_src=4, h=w_src=6; (0,0)->(4-1-0, 0)=(3,0)
        // Exact corner pixels are only asserted for lossless formats; JPEG
        // re-encode at quality 95 can shift the marker colours slightly.
        if (ext == "png" || ext == "bmp")
        {
            sample(after, 3, 0, r, g, b);
            CHECK(r == 0 && g == 0 && b == 128, "reloaded CW90 corner ." + ext);
        }
    }

    auto r2 = mviewer::core::rotateImageFile(utf8, -90); // back
    CHECK(r2.ok, "CCW90 ok ." + ext + " err=" + r2.error);
    CHECK(r2.width == 6 && r2.height == 4, "CCW90 restores dims ." + ext);

    CHECK(!mviewer::core::isWritableRotateFormat("cr2"), "RAW not writable");
    CHECK(mviewer::core::isWritableRotateFormat("png"), "png writable");
    CHECK(mviewer::core::isWritableRotateFormat(".JPG"), "JPG writable");

    auto bad = mviewer::core::rotateImageFile(utf8, 45);
    CHECK(!bad.ok, "non-90 angle rejected");
    CHECK(bad.errorCode == mviewer::core::ImageRotateError::InvalidAngle, "non-90 angle errorCode");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

static std::filesystem::path uniqueRotateDir()
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "mviewer_imagerotate_errors";
    std::filesystem::create_directories(dir);
    return dir;
}

static std::string uniquePath(const std::filesystem::path &dir, const std::string &name)
{
    return (dir / (name + "_" +
                   std::to_string(static_cast<long long>(
                       std::chrono::steady_clock::now().time_since_epoch().count()))))
        .string();
}

static void testErrorCodes()
{
    std::cout << "\n[rotateImageFile error codes]\n";
    namespace fs = std::filesystem;
    using mviewer::core::ImageRotateError;
    using mviewer::core::rotateImageFile;

    auto empty = rotateImageFile("", 90);
    CHECK(!empty.ok && empty.errorCode == ImageRotateError::EmptyPath, "empty path code");
    CHECK(empty.error == "empty path", "empty path English");

    auto missing = rotateImageFile(uniquePath(uniqueRotateDir(), "missing") + ".png", 90);
    CHECK(!missing.ok && missing.errorCode == ImageRotateError::NotFound, "missing file code");
    CHECK(missing.error == "file not found", "missing file English");

    const fs::path dir = uniqueRotateDir();
    const std::string tif = uniquePath(dir, "not_a_tiff") + ".tif";
    {
        std::ofstream out(tif);
        out << "not a tiff";
    }
    auto unsupported = rotateImageFile(tif, 90);
    CHECK(!unsupported.ok && unsupported.errorCode == ImageRotateError::UnsupportedFormat,
          "tif rejected as unsupported");
    CHECK(unsupported.error.find("unsupported format") != std::string::npos,
          "tif English mentions unsupported format");
    CHECK(fs::file_size(tif) > 0, "unsupported format leaves source in place");

    const std::string png = uniquePath(dir, "readonly") + ".png";
    ImageData src = makeMarker(6, 4);
    Encoder::Params params;
    params.quality = 95;
    CHECK(Encoder::encode(src, png, params), "encode readonly fixture");
    const auto bytesBefore = fs::file_size(png);
    QFile qf(QString::fromStdString(png));
    const auto readOnlyPerms =
        QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup;
    CHECK(qf.setPermissions(readOnlyPerms), "chmod readonly");
    auto readonly = rotateImageFile(png, 90);
    if (QFileInfo(QString::fromStdString(png)).isWritable())
    {
        std::cout << "SKIP: readonly check (process can still write)\n";
        qf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    else
    {
        CHECK(!readonly.ok && readonly.errorCode == ImageRotateError::NotWritable,
              "readonly file not writable");
        CHECK(readonly.error == "file not writable", "readonly English");
        CHECK(fs::file_size(png) == bytesBefore, "readonly leaves bytes unchanged");
        qf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

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
    testErrorCodes();

    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "\nAll imagerotate tests passed.\n";
    return 0;
}
