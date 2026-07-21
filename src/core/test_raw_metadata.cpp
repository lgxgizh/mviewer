// M14-2: RAW metadata parser test. Builds a minimal DNG-like TIFF + EXIF IFD
// in a temp file and verifies parseRawMetadata reads ISO/exposure/focal.
#include "core/image/RawMetadata.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                                                                                       \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                              \
    } while (0)

// Write a minimal little-endian TIFF + one IFD containing the tags we parse.
static bool writeFakeDng(const std::string &path, uint16_t iso, uint32_t exposureNum,
                         uint32_t exposureDen, uint32_t focalNum, uint32_t focalDen)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;

    // TIFF header (little-endian)
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00}; // IFD at offset 8
    std::fwrite(hdr, 1, 8, f);

    // IFD: 5 entries
    uint16_t count = 5;
    std::fwrite(&count, 2, 1, f);

    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val) {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };

    // Entry offsets (after IFD: 8 + 2 + 5*12 + 4 = 8+2+60+4 = 74)
    // We'll lay entries inline for simplicity (count=1, fits in 4 bytes).
    long ifdStart = 8;
    long dataOffset = ifdStart + 2 + count * 12 + 4; // after IFD

    // ISO (0x8827, SHORT, 1) -> inline
    writeEntry(0x8827, 3, 1, iso);
    // ExposureTime (0x829A, RATIONAL, 1) -> [num, den] at dataOffset
    writeEntry(0x829A, 5, 1, dataOffset);
    // FocalLength (0x920A, RATIONAL, 1) -> [num, den] after exposure
    writeEntry(0x920A, 5, 1, dataOffset + 8);
    // Make (0x010F, ASCII, 4) -> "SONY" at dataOffset+16
    writeEntry(0x010F, 2, 4, dataOffset + 16);
    // Model (0x0110, ASCII, 3) -> "A7R" + null at dataOffset+20
    writeEntry(0x0110, 2, 4, dataOffset + 20);

    // Next IFD offset (0 = none)
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);

    // Data section
    std::fseek(f, dataOffset, SEEK_SET);
    std::fwrite(&exposureNum, 4, 1, f);
    std::fwrite(&exposureDen, 4, 1, f);
    std::fwrite(&focalNum, 4, 1, f);
    std::fwrite(&focalDen, 4, 1, f);
    const char *make = "SONY";
    std::fwrite(make, 1, 4, f);
    const char *model = "A7R\0";
    std::fwrite(model, 1, 4, f);

    std::fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");

    const std::string rawPath = tmp.path().toStdString() + "/test.dng";
    CHECK(writeFakeDng(rawPath, 800, 1, 250, 85, 1), "fake DNG written");

    auto rm = mviewer::core::parseRawMetadata(rawPath);
    CHECK(rm.parsed, "RAW file parsed (extension recognized)");
    CHECK(rm.iso == 800, "ISO = 800");
    CHECK(rm.exposureSec > 0.0039 && rm.exposureSec < 0.0041, "exposure = 1/250s ~= 0.004");
    CHECK(rm.focalLength > 84.0 && rm.focalLength < 86.0, "focal = 85mm");
    CHECK(rm.make == "SONY", "make = SONY");
    CHECK(rm.model.find("A7R") == 0, "model starts with A7R");

    // Non-RAW should return parsed=false
    const std::string jpgPath = tmp.path().toStdString() + "/test.jpg";
    auto rm2 = mviewer::core::parseRawMetadata(jpgPath);
    CHECK(!rm2.parsed, "non-RAW returns parsed=false");

    printf("\n==== RAW metadata test: %d/%d passed ====\n", g_pass, g_pass + g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
