// M59 adversarial color/presentation regressions.

#include "core/image/DisplayColorContext.h"
#include "core/image/FrameSequence.h"
#include "core/image/ImageBuffer.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/decoder/QtDecoder.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    }                                                                                              \
    while (false)

std::string fixture(const char *name)
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large/" + name;
}

bool samePixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return false;
    const QImage left = a.convertToFormat(QImage::Format_RGB888);
    const QImage right = b.convertToFormat(QImage::Format_RGB888);
    if (left.isNull() || right.isNull())
        return false;
    for (int y = 0; y < left.height(); ++y)
        if (std::memcmp(left.constScanLine(y), right.constScanLine(y),
                        static_cast<size_t>(left.width()) * 3) != 0)
            return false;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QtDecoder decoder;
    const std::string adobe = fixture("icc_adobe_12mp.jpg");

    mviewer::domain::ImageMetadata meta;
    const ImageData source = decoder.decodeFull(adobe, meta);
    CHECK(!source.isNull(), "Adobe source decodes for presentation tests");
    const std::vector<uint8_t> before = source.buffer ? *source.buffer : std::vector<uint8_t>();

    const QColorSpace p3Space(QColorSpace::DisplayP3);
    const QByteArray p3Icc = p3Space.iccProfile();
    auto p3Profile = std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(p3Icc.constData()),
                                          reinterpret_cast<const uint8_t *>(p3Icc.constData()) +
                                              p3Icc.size());
    const auto p3Target = mviewer::core::DisplayColorContext::fromIccProfile(
        std::move(p3Profile), 7, "display-p3-test");
    const auto p3Again = mviewer::core::DisplayColorContext::fromIccProfile(
        std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(p3Icc.constData()),
                             reinterpret_cast<const uint8_t *>(p3Icc.constData()) + p3Icc.size()),
        8, "display-p3-test");
    CHECK(p3Target.cacheKey() != p3Again.cacheKey(),
          "display profile generation participates in cache identity");
    CHECK(p3Target.fingerprint == "display-p3-test", "explicit display fingerprint is retained");

    const QImage srgb = mvcore::toDisplayQImage(source, meta);
    const QImage p3 = mvcore::toDisplayQImage(source, meta, p3Target);
    CHECK(srgb.colorSpace() == QColorSpace::SRgb, "legacy display API remains deterministic sRGB");
    CHECK(p3.colorSpace() == p3Space,
          "explicit target keeps the presentation target color space");
    CHECK(!samePixels(srgb, p3), "source AdobeRGB converts directly to distinct P3 output");
    CHECK(source.buffer && *source.buffer == before, "presentation conversion never mutates source bytes");

    const ImageData p3Data = mvcore::toDisplayImageData(source, meta, p3Target);
    CHECK(samePixels(p3, mvcore::toQImage(p3Data)),
          "CPU QImage and ImageData upload paths are byte-equivalent");

    mviewer::domain::ImageMetadata malformed;
    malformed.textKeys["MViewer.DisplayICC.Base64"] = "%%%not-an-icc%%%";
    ImageData tiny = makeImageData(2, 2, PixelFormat::RGB24);
    std::fill(tiny.buffer->begin(), tiny.buffer->end(), 127);
    const QImage safe = mvcore::toDisplayQImage(tiny, malformed, p3Target);
    CHECK(!safe.isNull(), "malformed source ICC does not crash or drop the raster");
    CHECK(safe.colorSpace() == p3Space,
          "malformed source ICC falls back to assumed sRGB before target conversion");

    // The real ICC-bearing JPEG fixture exercises both the ordinary decoder
    // and FrameSequenceReader paths.  They must expose one canonical profile
    // label and retain the same embedded profile bytes.
    const auto probed = mviewer::core::MetadataReader::read(adobe);
    mviewer::domain::ImageMetadata adobeDecodedMeta;
    const auto adobeDecoded = decoder.decodeFull(adobe, adobeDecodedMeta);
    const auto adobeFrame = mviewer::core::FrameSequenceReader::decodeFull(adobe, 0);
    CHECK(!adobeDecoded.isNull() && adobeFrame.ok, "ICC-bearing JPEG decodes through both paths");
    CHECK(probed.colorSpace == "AdobeRGB" && adobeDecodedMeta.colorSpace == "AdobeRGB",
          "static decoder paths identify the canonical AdobeRGB label");
    const auto sequenceIcc = adobeFrame.metadata.textKeys.find("MViewer.DisplayICC.Base64");
    const auto decodedIcc = adobeDecodedMeta.textKeys.find("MViewer.DisplayICC.Base64");
    CHECK(adobeFrame.metadata.colorSpace == "AdobeRGB" && adobeFrame.metadata.hasIccProfile &&
              sequenceIcc != adobeFrame.metadata.textKeys.end() &&
              decodedIcc != adobeDecodedMeta.textKeys.end() &&
              sequenceIcc->second == decodedIcc->second,
          "sequence reader preserves the AdobeRGB ICC profile bytes");

    mviewer::domain::ImageMetadata highBitProbe;
    const bool highBitReadable = decoder.probeMetadata(fixture("large_tiff_16bit.tiff"), highBitProbe);
    CHECK(highBitReadable, "16-bit TIFF remains probeable without full materialization");
    CHECK(highBitProbe.bitDepth == 16,
          "16-bit TIFF probe reports source bits per channel");

    std::printf("M59 color-managed presentation failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
