#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/RawDecoder.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
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
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (0)

// Build a JPEG byte stream at the given size via Qt's encoder.
static QByteArray makeJpegBytes(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb((x * 255) / w, (y * 255) / h, 128));
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "JPEG");
    writer.write(img);
    buf.close();
    return ba;
}

// Wrap a JPEG inside junk bytes, mimicking a RAW container with an embedded
// preview (CR2/NEF/ARW/DNG all embed a preview JPEG this way).
static std::vector<uint8_t> makeFakeRaw(const QByteArray &jpeg)
{
    std::vector<uint8_t> v;
    for (int i = 0; i < 64; ++i)
        v.push_back(static_cast<uint8_t>(i * 7 + 3)); // leading junk
    v.insert(v.end(), reinterpret_cast<const uint8_t *>(jpeg.constData()),
             reinterpret_cast<const uint8_t *>(jpeg.constData()) + jpeg.size());
    for (int i = 0; i < 128; ++i)
        v.push_back(static_cast<uint8_t>(i * 3 + 1)); // trailing junk
    return v;
}

static std::vector<uint8_t> joinRawParts(std::initializer_list<QByteArray> parts)
{
    std::vector<uint8_t> v;
    for (const QByteArray &part : parts)
    {
        v.insert(v.end(), reinterpret_cast<const uint8_t *>(part.constData()),
                 reinterpret_cast<const uint8_t *>(part.constData()) + part.size());
        v.insert(v.end(), 37, static_cast<uint8_t>(v.size() & 0xff));
    }
    return v;
}

static void writeFile(const std::string &path, const std::vector<uint8_t> &data)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    DecoderRegistry::instance().resetToDefaults();

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");

    const QByteArray jpeg = makeJpegBytes(48, 32);
    CHECK(!jpeg.isEmpty(), "JPEG preview encoded");

    const std::string rawPath = tmp.path().toStdString() + "/sample.cr2";
    writeFile(rawPath, makeFakeRaw(jpeg));

    // canDecode gating.
    RawDecoder dec;
    CHECK(dec.canDecode(rawPath), "RawDecoder claims .cr2");
    CHECK(!dec.canDecode(tmp.path().toStdString() + "/x.jpg"), "RawDecoder rejects .jpg");

    // Full decode returns the embedded preview.
    ImageData d = DecoderRegistry::instance().decodeFull(rawPath);
    CHECK(!d.isNull(), "RAW decoded to non-null ImageData");
    CHECK(d.width == 48 && d.height == 32, "decoded dims match embedded preview (48x32)");
    CHECK(RawDecoder::lastPreviewFullFileCopyBytes() == 0,
          "RAW preview scan does not allocate a second full-file copy");

    // Scaled decode clamps to maxEdge.
    ImageData ds = DecoderRegistry::instance().decodeScaled(rawPath, 16);
    CHECK(!ds.isNull(), "RAW scaled decode non-null");
    CHECK(ds.width <= 16 && ds.height <= 16, "scaled clamped to maxEdge");

    // Graceful: a RAW with no embedded preview falls through to empty.
    const std::string brokenPath = tmp.path().toStdString() + "/broken.cr2";
    std::vector<uint8_t> junk(256);
    for (size_t i = 0; i < junk.size(); ++i)
        junk[i] = static_cast<uint8_t>((i * 5) & 0xFF);
    writeFile(brokenPath, junk);
    ImageData d2 = DecoderRegistry::instance().decodeFull(brokenPath);
    CHECK(d2.isNull(), "preview-less RAW returns empty (graceful fallthrough)");
    CHECK(RawDecoder::lastPreviewFullFileCopyBytes() == 0,
          "malformed/preview-less RAW scan does not copy the full file");

    // Cameras may carry several previews. The scanner must ignore the
    // truncated candidate and select the largest complete embedded JPEG.
    const QByteArray smallJpeg = makeJpegBytes(12, 8);
    const QByteArray largeJpeg = makeJpegBytes(64, 40);
    std::vector<uint8_t> multi = joinRawParts({smallJpeg, largeJpeg});
    const std::string multiPath = tmp.path().toStdString() + "/multi.nef";
    writeFile(multiPath, multi);
    ImageData multiImage = DecoderRegistry::instance().decodeFull(multiPath);
    CHECK(!multiImage.isNull() && multiImage.width == 64 && multiImage.height == 40,
          "multiple embedded JPEGs choose the largest complete preview");

    std::vector<uint8_t> truncated;
    truncated.insert(truncated.end(), 64, 0x5a);
    const int truncatedSize = largeJpeg.size() > 19 ? largeJpeg.size() - 19 : 0;
    truncated.insert(truncated.end(), reinterpret_cast<const uint8_t *>(largeJpeg.constData()),
                     reinterpret_cast<const uint8_t *>(largeJpeg.constData()) + truncatedSize);
    const std::string truncatedPath = tmp.path().toStdString() + "/truncated.arw";
    writeFile(truncatedPath, truncated);
    ImageData truncatedImage = DecoderRegistry::instance().decodeFull(truncatedPath);
    CHECK(truncatedImage.isNull(), "truncated embedded JPEG is rejected without a partial image");

    // Normal JPEG flow still works (RawDecoder must not steal non-RAW).
    const std::string jpgPath = tmp.path().toStdString() + "/normal.jpg";
    writeFile(jpgPath, std::vector<uint8_t>(jpeg.begin(), jpeg.end()));
    ImageData d3 = DecoderRegistry::instance().decodeFull(jpgPath);
    CHECK(!d3.isNull() && d3.width == 48, "plain JPEG still decoded via registry");

    printf("\n==== RAW decode test: %d passed, %d failed ====\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
