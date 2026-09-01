// M59 Phase 0 — metadata/presentation contract baseline.
//
// This test is intentionally RED against the pre-M59 implementation.  It
// freezes the source metadata truth that the implementation must make
// authoritative across the cheap probe, full decoder, and frame-sequence
// paths.  The assertions remain a release gate after the closure lands.

#include "core/image/FrameSequence.h"
#include "core/image/MetadataReader.h"
#include "core/image/decoder/QtDecoder.h"

#include <QCoreApplication>
#include <QDir>

#include <cstdio>
#include <string>

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
    } while (false)

std::string fixture(const char *name)
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large/" + name;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string adobe = fixture("icc_adobe_12mp.jpg");
    if (!QDir(QString::fromStdString(fixture("."))).exists())
    {
        std::printf("fixture root missing\n");
        return 2;
    }

    QtDecoder decoder;
    const auto fileMeta = mviewer::core::MetadataReader::read(adobe);
    mviewer::domain::ImageMetadata decodedMeta;
    const auto decoded = decoder.decodeFull(adobe, decodedMeta);

    CHECK(!decoded.isNull(), "Adobe fixture decodes through QtDecoder");
    CHECK(fileMeta.channels == 3, "probe reports RGB channel count");
    CHECK(decodedMeta.channels == 3, "full decode reports RGB channel count");
    CHECK(fileMeta.bitDepth == 8, "probe reports bits per channel, not packed depth");
    CHECK(decodedMeta.bitDepth == 8, "full decode reports bits per channel, not packed depth");
    CHECK(fileMeta.colorSpace == "AdobeRGB", "probe reports canonical AdobeRGB label");
    CHECK(decodedMeta.colorSpace == "AdobeRGB", "full decode reports canonical AdobeRGB label");
    CHECK(fileMeta.hasIccProfile, "probe preserves the embedded ICC profile");
    CHECK(decodedMeta.hasIccProfile, "full decode preserves the embedded ICC profile");
    const auto probeIcc = fileMeta.textKeys.find("MViewer.DisplayICC.Base64");
    const auto decodedIcc = decodedMeta.textKeys.find("MViewer.DisplayICC.Base64");
    CHECK(probeIcc != fileMeta.textKeys.end() && decodedIcc != decodedMeta.textKeys.end() &&
              probeIcc->second == decodedIcc->second,
          "probe and full decode expose identical ICC bytes");

    // EXIF 5/7 are the two transpose/transverse combinations that are easily
    // lost when a Qt transformation bitmask is interpreted with bitwise ORs.
    for (int orientation = 2; orientation <= 8; ++orientation)
    {
        const std::string path = fixture((std::string("exif_orient") + std::to_string(orientation) +
                                          "_non_square.jpg")
                                             .c_str());
        const auto probed = mviewer::core::MetadataReader::read(path);
        mviewer::domain::ImageMetadata full;
        const auto pixels = decoder.decodeFull(path, full);
        CHECK(!pixels.isNull(), "orientation fixture decodes");
        CHECK(probed.orientation == orientation, "probe preserves exact EXIF orientation 1-8");
        CHECK(full.orientation == orientation, "full decode preserves exact EXIF orientation 1-8");
        CHECK(probed.orientation == full.orientation,
              "probe and full decode agree on EXIF orientation");
    }

    const auto sequence = mviewer::core::FrameSequenceReader::decodeFull(adobe, 0);
    CHECK(sequence.ok, "static source is readable through FrameSequenceReader");
    CHECK(sequence.metadata.bitDepth == decodedMeta.bitDepth,
          "frame-sequence metadata uses the same bit-depth contract");
    CHECK(sequence.metadata.colorSpace == decodedMeta.colorSpace,
          "frame-sequence metadata uses the same color-space label");
    CHECK(sequence.metadata.hasIccProfile,
          "frame-sequence metadata preserves the embedded ICC profile");
    const auto sequenceIcc = sequence.metadata.textKeys.find("MViewer.DisplayICC.Base64");
    CHECK(sequenceIcc != sequence.metadata.textKeys.end() && decodedIcc != decodedMeta.textKeys.end() &&
              sequenceIcc->second == decodedIcc->second,
          "frame-sequence metadata exposes identical ICC bytes");

    for (int orientation : {5, 6, 7})
    {
        const std::string path = fixture((std::string("exif_orient") + std::to_string(orientation) +
                                          "_non_square.jpg")
                                             .c_str());
        const auto frame = mviewer::core::FrameSequenceReader::decodeFull(path, 0);
        CHECK(frame.ok, "orientation fixture decodes through FrameSequenceReader");
        CHECK(frame.metadata.orientation == orientation,
              "frame-sequence preserves exact EXIF orientation");
    }

    std::printf("M59 Phase 0 failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
