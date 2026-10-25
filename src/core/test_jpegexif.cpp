// JpegExif orientation math and JPEG APP1 segment editing.
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/JpegExif.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

using mviewer::core::composeOrientation;
using mviewer::core::extractJpegApp1Exif;
using mviewer::core::FilePixelOp;
using mviewer::core::readJpegOrientation;
using mviewer::core::spliceJpegApp1Exif;
using mviewer::core::writeJpegOrientationInPlace;

static void appendBe16(std::vector<uint8_t> &out, int value)
{
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

static std::vector<uint8_t> segment(uint8_t marker, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> out;
    out.push_back(0xFF);
    out.push_back(marker);
    appendBe16(out, static_cast<int>(payload.size() + 2));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static std::vector<uint8_t> exifApp1(bool motorola, int orientation, const char *tail)
{
    std::vector<uint8_t> tiff;
    if (motorola)
    {
        tiff.insert(tiff.end(), {0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x01,
                                 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01});
        appendBe16(tiff, orientation);
        tiff.insert(tiff.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    }
    else
    {
        tiff.insert(tiff.end(), {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12,
                                 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00});
        tiff.push_back(static_cast<uint8_t>(orientation));
        tiff.insert(tiff.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    }
    if (tail)
        tiff.insert(tiff.end(), tail, tail + std::strlen(tail));
    std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
    payload.insert(payload.end(), tiff.begin(), tiff.end());
    return segment(0xE1, payload);
}

static std::vector<uint8_t> wrapSoi(const std::vector<std::vector<uint8_t>> &parts)
{
    std::vector<uint8_t> out = {0xFF, 0xD8};
    for (const auto &part : parts)
        out.insert(out.end(), part.begin(), part.end());
    return out;
}

static int diffCount(const std::vector<uint8_t> &left, const std::vector<uint8_t> &right)
{
    if (left.size() != right.size())
        return -1;
    int diffs = 0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (left[i] != right[i])
            ++diffs;
    }
    return diffs;
}

// IFD0 Orientation is a SHORT. Values 1..8 keep the high byte at 0, so
// rewriting that 2-byte field changes exactly one byte and nothing else.
// A rebuilt segment or a re-encode changes far more than one byte.

static bool containsText(const std::vector<uint8_t> &bytes, const char *text)
{
    const auto *raw = reinterpret_cast<const char *>(bytes.data());
    const size_t length = std::strlen(text);
    return std::search(raw, raw + bytes.size(), text, text + length) != raw + bytes.size();
}

static int countExif(const std::vector<uint8_t> &jpeg)
{
    int count = 0;
    size_t pos = 0;
    const uint8_t signature[] = {'E', 'x', 'i', 'f', 0, 0};
    while (pos + 6 < jpeg.size())
    {
        const auto found = std::search(jpeg.begin() + static_cast<std::ptrdiff_t>(pos), jpeg.end(),
                                       signature, signature + 6);
        if (found == jpeg.end())
            break;
        ++count;
        pos = static_cast<size_t>(found - jpeg.begin()) + 6;
    }
    return count;
}

static void testCompose()
{
    const int rot90[] = {0, 6, 7, 8, 5, 2, 3, 4, 1};
    const int rot180[] = {0, 3, 4, 1, 2, 7, 8, 5, 6};
    const int rot270[] = {0, 8, 5, 6, 7, 4, 1, 2, 3};
    const int flipH[] = {0, 2, 1, 4, 3, 6, 5, 8, 7};
    const int flipV[] = {0, 4, 3, 2, 1, 8, 7, 6, 5};
    for (int current = 1; current <= 8; ++current)
    {
        CHECK(composeOrientation(current, FilePixelOp::Rotate, 90) == rot90[current], "rot90");
        CHECK(composeOrientation(current, FilePixelOp::Rotate, 180) == rot180[current], "rot180");
        CHECK(composeOrientation(current, FilePixelOp::Rotate, 270) == rot270[current], "rot270");
        CHECK(composeOrientation(current, FilePixelOp::FlipH, 0) == flipH[current], "flipH");
        CHECK(composeOrientation(current, FilePixelOp::FlipV, 0) == flipV[current], "flipV");
    }
    CHECK(composeOrientation(0, FilePixelOp::Rotate, 90) == rot90[1], "current 0 acts as 1");
    CHECK(composeOrientation(9, FilePixelOp::Rotate, 90) == rot90[1], "current 9 acts as 1");
    CHECK(composeOrientation(6, FilePixelOp::Rotate, 45) == 6, "non multiple of 90 is unchanged");
    CHECK(composeOrientation(6, FilePixelOp::Rotate, -90) == rot270[6], "-90 matches 270");
}

static void testSegments()
{
    auto tagged = wrapSoi({exifApp1(false, 6, "CanonX 20200101 50mm GPS")});
    tagged.insert(tagged.end(), {0xFF, 0xD9});
    CHECK(readJpegOrientation(tagged) == 6, "II orientation reads 6");
    auto edited = tagged;
    CHECK(writeJpegOrientationInPlace(edited, 3), "II tag rewrite");
    CHECK(diffCount(tagged, edited) == 1, "II rewrite changes only the orientation byte");
    CHECK(readJpegOrientation(edited) == 3, "II orientation reads 3");
    CHECK(containsText(edited, "CanonX 20200101 50mm GPS"), "II metadata bytes stay");

    auto motorola = wrapSoi({exifApp1(true, 6, "GPS")});
    CHECK(readJpegOrientation(motorola) == 6, "MM orientation reads 6");
    auto mmEdit = motorola;
    CHECK(writeJpegOrientationInPlace(mmEdit, 8), "MM tag rewrite");
    CHECK(diffCount(motorola, mmEdit) == 1, "MM rewrite changes only the orientation byte");
    CHECK(readJpegOrientation(mmEdit) == 8, "MM orientation reads 8");

    auto missing = wrapSoi({segment(0xE0, {'J', 'F', 'I', 'F', 0, 1, 1})});
    auto missingCopy = missing;
    CHECK(!readJpegOrientation(missing).has_value(), "no APP1 has no orientation");
    CHECK(!writeJpegOrientationInPlace(missing, 6), "missing tag is not created");
    CHECK(missing == missingCopy, "failed write leaves the buffer unchanged");
    CHECK(extractJpegApp1Exif(missing).empty(), "no APP1 extracts empty");

    std::vector<uint8_t> truncated = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x20, 'E', 'x', 'i', 'f'};
    auto truncatedCopy = truncated;
    CHECK(!readJpegOrientation(truncated).has_value(), "truncated JPEG has no orientation");
    CHECK(!writeJpegOrientationInPlace(truncated, 1), "truncated write fails");
    CHECK(truncated == truncatedCopy, "truncated write does not extend the buffer");

    CHECK(!writeJpegOrientationInPlace(edited, 0), "orientation 0 is rejected");
    CHECK(!writeJpegOrientationInPlace(edited, 9), "orientation 9 is rejected");
    CHECK(readJpegOrientation(edited) == 3, "rejected write keeps the previous tag");

    const auto jfif = segment(0xE0, {'J', 'F', 'I', 'F', 0, 1, 1});
    const auto app2 = segment(0xE2, {'I', 'C', 'C', '_', 'P'});
    const auto exif = exifApp1(false, 6, "CanonX");
    const auto xmp =
        segment(0xE1, {'h', 't', 't', 'p', ':', 'X', 'M', 'P', '-', 'K', 'E', 'E', 'P'});
    std::vector<uint8_t> sos = {0xFF, 0xDA, 0x00, 0x02};
    auto multi = wrapSoi({jfif, app2, exif, xmp, sos});
    const auto replacement = exifApp1(false, 1, "NEW");
    auto spliced = spliceJpegApp1Exif(multi, replacement, false);
    CHECK(containsText(spliced, "JFIF"), "JFIF survives splice");
    CHECK(containsText(spliced, "ICC_P"), "APP2 survives splice");
    CHECK(containsText(spliced, "XMP-KEEP"), "non-Exif APP1 survives splice");
    CHECK(containsText(spliced, "NEW"), "replacement Exif is inserted");
    CHECK(!containsText(spliced, "CanonX"), "previous Exif APP1 is not duplicated");
    const auto firstExif = extractJpegApp1Exif(spliced);
    CHECK(!firstExif.empty() && readJpegOrientation(firstExif) == 1, "spliced Exif is first APP1");
    auto again = spliceJpegApp1Exif(spliced, replacement, true);
    CHECK(countExif(again) == 1, "a second splice does not stack Exif segments");

    auto bare = std::vector<uint8_t>{0xFF, 0xD8, 0xFF, 0xD9};
    auto inserted = spliceJpegApp1Exif(bare, exif, true);
    CHECK(readJpegOrientation(inserted) == 1, "reset forces an existing tag to 1");
    CHECK(inserted.size() == bare.size() + exif.size(), "Exif is inserted after SOI");
}

static std::vector<uint8_t> readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    return std::vector<uint8_t>(data.begin(), data.end());
}

static void testRotateFile()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/mviewer_jpegexif");
    QDir().mkpath(dir);
    const QString taggedPath = dir + QStringLiteral("/tagged.jpg");
    const char *tail = "CanonX DateTime:2020:01:01 12:00:00 Lens:50mm GPS:1,2";
    auto tagged = wrapSoi({exifApp1(false, 6, tail)});
    tagged.insert(tagged.end(), {0xFF, 0xD9});
    {
        QFile file(taggedPath);
        CHECK(file.open(QIODevice::WriteOnly), "write tagged fixture");
        file.write(reinterpret_cast<const char *>(tagged.data()),
                   static_cast<qint64>(tagged.size()));
    }
    const auto result = mviewer::core::rotateImageFile(taggedPath.toStdString(), 90);
    CHECK(result.ok && result.method == mviewer::core::ImageRotateMethod::ExifOrientation,
          "tagged JPEG uses the orientation tag");
    const auto rotated = readAll(taggedPath);
    CHECK(diffCount(tagged, rotated) == 1, "rotate changes only the orientation byte");
    CHECK(readJpegOrientation(rotated) == 3, "orientation 6 rotated 90 becomes 3");
    CHECK(containsText(rotated, "CanonX"), "camera make survives");
    CHECK(containsText(rotated, "DateTime:2020:01:01 12:00:00"), "capture time survives");
    CHECK(containsText(rotated, "Lens:50mm"), "lens survives");
    CHECK(containsText(rotated, "GPS:1,2"), "GPS survives");

    ImageData src = makeImageData(6, 4, PixelFormat::RGB24);
    std::fill(src.buffer->data(), src.buffer->data() + static_cast<size_t>(6 * 4 * 3), 40);
    const QString plainPath = dir + QStringLiteral("/plain.jpg");
    Encoder::Params params;
    params.quality = 95;
    CHECK(Encoder::encode(src, plainPath.toStdString(), params), "encode untagged JPEG");
    auto encoded = readAll(plainPath);
    // Make-only IFD: no 0x0112 tag, so a missing orientation stays implicit 1.
    std::vector<uint8_t> tiff = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0F,
                                 0x01, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 'C',  'a',  'n',  'o',  'n',  'X',  0};
    std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
    payload.insert(payload.end(), tiff.begin(), tiff.end());
    const auto makeOnly = segment(0xE1, payload);
    auto untagged = spliceJpegApp1Exif(encoded, makeOnly, true);
    if (readJpegOrientation(encoded))
        untagged = spliceJpegApp1Exif(encoded, makeOnly, false);
    CHECK(!readJpegOrientation(untagged).has_value(), "fixture JPEG has no orientation tag");
    {
        QFile file(plainPath);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite untagged fixture");
        file.write(reinterpret_cast<const char *>(untagged.data()),
                   static_cast<qint64>(untagged.size()));
    }
    const auto pixel = mviewer::core::rotateImageFile(plainPath.toStdString(), 90);
    CHECK(pixel.ok && pixel.method == mviewer::core::ImageRotateMethod::PixelRewrite,
          "untagged JPEG is re-encoded");
    const auto after = readAll(plainPath);
    CHECK(containsText(after, "CanonX"), "re-encode keeps the original APP1");
    CHECK(!readJpegOrientation(after).has_value(), "missing orientation stays missing");
    QImageReader reader(plainPath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    CHECK(!image.isNull() && image.width() == 4 && image.height() == 6,
          "re-encoded pixels are rotated 90 degrees");
    QDir(dir).removeRecursively();
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    testCompose();
    testSegments();
    testRotateFile();
    if (g_fail > 0)
    {
        std::cerr << "jpegexif_tests: FAIL (" << g_fail << ")\n";
        return 1;
    }
    std::cout << "jpegexif_tests: PASS\n";
    return 0;
}
