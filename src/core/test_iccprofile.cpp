// Regression tests for mviewer::core::parseIccProfile (src/core/image/IccProfile.cpp).
//
// Covers:
//  - correct BCD decoding of the profile version (e.g. 2.1.0, not 2.10.0)
//  - extraction of device class / color space / PCS / rendering intent
//  - extraction of desc / cprt text tags
//  - robustness against truncated and malformed input (no crash / no hang)
#include "core/image/IccProfile.h"
#include "core/image/QtConvert.h"

#include <QColorSpace>
#include <QImage>

#include <cstdint>
#include <cstring>
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

namespace
{
void put32(std::vector<unsigned char> &b, size_t off, uint32_t v)
{
    b[off] = (v >> 24) & 0xFF;
    b[off + 1] = (v >> 16) & 0xFF;
    b[off + 2] = (v >> 8) & 0xFF;
    b[off + 3] = v & 0xFF;
}

// Build a minimal but structurally valid v2.1.0 ICC profile in memory.
// When mlucDesc is true the "desc" tag is rewritten as a malformed "mluc"
// (recSize = 0) to exercise the anti-infinite-loop guard in parseTextType.
std::vector<unsigned char> makeProfile(bool mlucDesc)
{
    const std::string descStr = "sRGB IEC61966-2.1";
    const uint32_t descLen = uint32_t(descStr.size() + 1); // include null terminator
    std::vector<unsigned char> desc(12 + descLen, 0);
    desc[0] = 'd';
    desc[1] = 'e';
    desc[2] = 's';
    desc[3] = 'c';
    put32(desc, 8, descLen);
    std::memcpy(&desc[12], descStr.c_str(), descStr.size() + 1);

    const std::string cprtStr = "Copyright ACME";
    const uint32_t cprtLen = uint32_t(cprtStr.size() + 1);
    std::vector<unsigned char> cprt(12 + cprtLen, 0);
    // The cprt *tag payload* is a textDescriptionType, so it begins with "desc"
    // (the directory entry signature "cprt" only selects the copyright field).
    cprt[0] = 'd';
    cprt[1] = 'e';
    cprt[2] = 's';
    cprt[3] = 'c';
    put32(cprt, 8, cprtLen);
    std::memcpy(&cprt[12], cprtStr.c_str(), cprtStr.size() + 1);

    std::vector<unsigned char> descTag = desc;
    if (mlucDesc)
    {
        descTag[0] = 'm';
        descTag[1] = 'l';
        descTag[2] = 'u';
        descTag[3] = 'c';
        // mluc header: sig(4) reserved(4) recCount(4) recSize(4). Force recSize = 0.
        put32(descTag, 8, 1);  // recCount (irrelevant)
        put32(descTag, 12, 0); // recSize = 0 -> must NOT spin forever
    }

    const uint32_t tagCount = 2;
    const uint32_t headerSize = 128;
    const uint32_t dirSize = 4 + tagCount * 12;
    const uint32_t descOff = headerSize + dirSize;
    const uint32_t cprtOff = descOff + uint32_t(descTag.size());
    const uint32_t total = cprtOff + uint32_t(cprt.size());

    std::vector<unsigned char> p(total, 0);
    // version 2.1.0.0 -> byte9 = 0x10 so the minor nibble decodes to 1
    p[8] = 2;
    p[9] = 0x10;
    p[10] = 0;
    p[11] = 0;
    // device class "mntr"
    p[12] = 'm';
    p[13] = 'n';
    p[14] = 't';
    p[15] = 'r';
    // data color space "RGB "
    p[16] = 'R';
    p[17] = 'G';
    p[18] = 'B';
    p[19] = ' ';
    // PCS "XYZ "
    p[20] = 'X';
    p[21] = 'Y';
    p[22] = 'Z';
    p[23] = ' ';
    // rendering intent = 0 (perceptual) at bytes 64..67 (already zero)

    put32(p, 128, tagCount);
    // desc entry
    p[132] = 'd';
    p[133] = 'e';
    p[134] = 's';
    p[135] = 'c';
    put32(p, 136, descOff);
    put32(p, 140, uint32_t(descTag.size()));
    // cprt entry
    p[144] = 'c';
    p[145] = 'p';
    p[146] = 'r';
    p[147] = 't';
    put32(p, 148, cprtOff);
    put32(p, 152, uint32_t(cprt.size()));

    std::memcpy(&p[descOff], descTag.data(), descTag.size());
    std::memcpy(&p[cprtOff], cprt.data(), cprt.size());
    return p;
}
} // namespace

int main()
{
    using namespace mviewer::core;

    printf("IccProfile: parseIccProfile regression\n");

    // 1) Well-formed v2.1.0 profile
    {
        const std::vector<unsigned char> p = makeProfile(false);
        const IccProfile info = parseIccProfile(p.data(), p.size());
        CHECK(info.valid, "valid profile reports valid=true");
        CHECK(info.version == "2.1.0", "version decodes as 2.1.0 (BCD high nibble)");
        CHECK(info.deviceClass == "显示器", "device class 'mntr' -> 显示器");
        CHECK(info.colorSpace == "RGB", "color space 'RGB ' -> RGB");
        CHECK(info.pcs == "XYZ", "PCS 'XYZ ' -> XYZ");
        CHECK(info.renderingIntent == "感知 (Perceptual)", "rendering intent 0 -> Perceptual");
        CHECK(info.description == "sRGB IEC61966-2.1", "desc tag extracted");
        CHECK(info.copyright == "Copyright ACME", "cprt tag extracted");
    }

    // 2) Malformed mluc desc (recSize = 0) must not hang and yields empty description
    {
        const std::vector<unsigned char> p = makeProfile(true);
        const IccProfile info = parseIccProfile(p.data(), p.size());
        CHECK(info.valid, "mluc-malformed profile still reports valid=true");
        CHECK(info.description.empty(), "recSize=0 mluc yields empty description (no hang)");
        CHECK(info.copyright == "Copyright ACME",
              "cprt tag still extracted alongside malformed desc");
    }

    // 3) Truncated buffer (< 128-byte header) is rejected
    {
        std::vector<unsigned char> p(100, 0);
        const IccProfile info = parseIccProfile(p.data(), p.size());
        CHECK(!info.valid, "truncated buffer (100 bytes) reports valid=false");
    }

    // 4) Oversized tag count with a tiny buffer must not read out of bounds
    {
        std::vector<unsigned char> p = makeProfile(false);
        p.resize(140);              // only the header + partial tag directory
        put32(p, 128, 0xFFFFFFFFu); // lie about the tag count
        const IccProfile info = parseIccProfile(p.data(), p.size());
        CHECK(info.valid, "header-only buffer (140 bytes) reports valid=true");
        CHECK(info.description.empty(),
              "huge tag count with small buffer yields no desc (bounds-checked)");
    }

    // 5) Display conversion consumes ICC on a copy and never mutates analysis bytes.
    {
        ImageData pixels = makeImageData(1, 1, PixelFormat::RGB24);
        (*pixels.buffer)[0] = 180;
        (*pixels.buffer)[1] = 90;
        (*pixels.buffer)[2] = 40;
        const std::vector<uint8_t> before = *pixels.buffer;
        mviewer::domain::ImageMetadata meta;
        const QByteArray adobe = QColorSpace(QColorSpace::AdobeRgb).iccProfile();
        const QByteArray encoded = adobe.toBase64();
        meta.textKeys["MViewer.DisplayICC.Base64"] =
            std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
        meta.hasIccProfile = true;

        QImage expected(1, 1, QImage::Format_RGB888);
        expected.setPixelColor(0, 0, QColor(180, 90, 40));
        expected.setColorSpace(QColorSpace::AdobeRgb);
        expected.convertToColorSpace(QColorSpace::SRgb);
        const QImage actual = mvcore::toDisplayQImage(pixels, meta);
        CHECK(!actual.isNull() && actual.colorSpace() == QColorSpace::SRgb,
              "display copy is tagged/converted to sRGB");
        CHECK(actual.pixelColor(0, 0) == expected.pixelColor(0, 0),
              "embedded AdobeRGB display conversion matches Qt reference");
        CHECK(*pixels.buffer == before, "display conversion leaves analysis-domain bytes unchanged");

        meta.textKeys.erase("MViewer.DisplayICC.Base64");
        meta.hasIccProfile = false;
        const QImage unprofiled = mvcore::toDisplayQImage(pixels, meta);
        CHECK(unprofiled.colorSpace() == QColorSpace::SRgb,
              "unprofiled images use deterministic sRGB display fallback");
        CHECK(unprofiled.pixelColor(0, 0) == QColor(180, 90, 40),
              "unprofiled fallback preserves decoded numeric values");
    }

    printf("\nIccProfile tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
