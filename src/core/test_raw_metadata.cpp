// M14-2: RAW metadata parser test. Builds minimal DNG-like TIFF + EXIF IFD
// structures in temp files and verifies parseRawMetadata robustness and feature completeness.
#include "core/image/RawMetadata.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdint>
#include <cstdio>
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

// Write a minimal little-endian TIFF + one IFD containing the tags we parse.
static bool writeFakeDng(const std::string &path, uint16_t iso, uint32_t exposureNum,
                         uint32_t exposureDen, uint32_t focalNum, uint32_t focalDen)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;

    // TIFF header (little-endian): IFD at offset 8
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);

    // IFD: 5 entries (each 12 bytes) + 4-byte next-IFD offset = 64 bytes total
    // data starts at offset 8 + 2 + 5*12 + 4 = 74
    uint16_t count = 5;
    std::fwrite(&count, 2, 1, f);

    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val)
    {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };

    const long ifdStart = 8;
    const long dataOffset = ifdStart + 2 + static_cast<long>(count) * 12 + 4;

    // ISO (0x8827, SHORT, count=1) -> inline value
    writeEntry(0x8827, 3, 1, iso);
    // ExposureTime (0x829A, RATIONAL, count=1) -> offset to data
    writeEntry(0x829A, 5, 1, static_cast<uint32_t>(dataOffset));
    // FocalLength (0x920A, RATIONAL, count=1) -> offset to data + 8
    writeEntry(0x920A, 5, 1, static_cast<uint32_t>(dataOffset + 8));
    // Make / Model (ASCII, count=4): TIFF 6.0 stores values of <= 4 bytes
    // inline in the value field, in file order.
    auto inlineAscii = [](const char *s4)
    {
        uint32_t v = 0;
        std::memcpy(&v, s4, 4); // little-endian host and file: bytes stay in order
        return v;
    };
    writeEntry(0x010F, 2, 4, inlineAscii("SONY"));
    writeEntry(0x0110, 2, 4, inlineAscii("A7R")); // 3 chars + NUL = 4 bytes

    // Next IFD offset (0 = none)
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);

    // Data section
    std::fseek(f, dataOffset, SEEK_SET);
    std::fwrite(&exposureNum, 4, 1, f);
    std::fwrite(&exposureDen, 4, 1, f);
    std::fwrite(&focalNum, 4, 1, f);
    std::fwrite(&focalDen, 4, 1, f);
    // Decoy bytes where an offset-based reader of the inline strings would land.
    std::fwrite("XXXXXXXX", 1, 8, f);

    std::fclose(f);
    return true;
}

// Write a realistic TIFF/RAW where IFD0 has basic tags + ExifIFD pointer (0x8769),
// and all exposure metadata resides inside the Exif IFD (matching Sony/Canon/Nikon layout).
static bool writeFakeDngWithExifIfd(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;

    // Header: IFD0 at 8
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);

    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val)
    {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };

    // IFD0: 5 entries (ImageWidth, ImageLength, BitsPerSample, Make, ExifIFDPointer)
    const uint16_t ifd0Count = 5;
    std::fwrite(&ifd0Count, 2, 1, f);

    const uint32_t ifd0EntriesEnd = 8 + 2 + ifd0Count * 12;
    const uint32_t exifIfdOffset = ifd0EntriesEnd + 4; // 8 + 2 + 60 + 4 = 74

    // 0x0100: Width = 8192 (LONG inline)
    writeEntry(0x0100, 4, 1, 8192);
    // 0x0101: Height = 5464 (LONG inline)
    writeEntry(0x0101, 4, 1, 5464);
    // 0x0102: BitsPerSample = 14 (SHORT inline)
    writeEntry(0x0102, 3, 1, 14);
    // 0x010F: Make -> data at 256
    writeEntry(0x010F, 2, 6, 256);
    // 0x8769: ExifIFDPointer -> exifIfdOffset
    writeEntry(0x8769, 4, 1, exifIfdOffset);

    // Next IFD = 0
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);

    // Exif IFD at offset 74: 6 entries
    std::fseek(f, exifIfdOffset, SEEK_SET);
    const uint16_t exifCount = 6;
    std::fwrite(&exifCount, 2, 1, f);

    // 0x8827: ISO = 1600 (SHORT inline)
    writeEntry(0x8827, 3, 1, 1600);
    // 0x829A: ExposureTime -> rational at 280 (1/500)
    writeEntry(0x829A, 5, 1, 280);
    // 0x829D: FNumber -> rational at 288 (28/10 = 2.8)
    writeEntry(0x829D, 5, 1, 288);
    // 0x920A: FocalLength -> rational at 296 (50/1 = 50mm)
    writeEntry(0x920A, 5, 1, 296);
    // 0xA001: ColorSpace = 1 (sRGB, SHORT inline)
    writeEntry(0xA001, 3, 1, 1);
    // 0x828E: CFAPattern: 4 bytes (0, 1, 1, 2 = RGGB, inline type 1 count 4)
    writeEntry(0x828E, 1, 4, 0x02010100);

    // Next IFD for Exif IFD = 0
    std::fwrite(&nextIfd, 4, 1, f);

    // Data payloads
    std::fseek(f, 256, SEEK_SET);
    std::fwrite("Canon\0", 1, 6, f);

    std::fseek(f, 280, SEEK_SET);
    const uint32_t expNum = 1, expDen = 500;
    std::fwrite(&expNum, 4, 1, f);
    std::fwrite(&expDen, 4, 1, f);

    const uint32_t fNum = 28, fDen = 10;
    std::fwrite(&fNum, 4, 1, f);
    std::fwrite(&fDen, 4, 1, f);

    const uint32_t flNum = 50, flDen = 1;
    std::fwrite(&flNum, 4, 1, f);
    std::fwrite(&flDen, 4, 1, f);

    std::fclose(f);
    return true;
}

// Write a degenerate TIFF where IFD0 links to IFD1 which links back to IFD0 (cycle).
static bool writeCyclicTiff(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;

    // Header: IFD0 at 8
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);

    // IFD0 at 8: 1 dummy entry, next IFD = 32
    uint16_t c0 = 1;
    std::fwrite(&c0, 2, 1, f);
    uint8_t entry0[12] = {0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00};
    std::fwrite(entry0, 1, 12, f);
    uint32_t nextIfd1 = 32;
    std::fwrite(&nextIfd1, 4, 1, f);

    // Padding to 32
    std::fseek(f, 32, SEEK_SET);
    // IFD1 at 32: 1 dummy entry, next IFD = 8 (cycle back!)
    uint16_t c1 = 1;
    std::fwrite(&c1, 2, 1, f);
    uint8_t entry1[12] = {0x01, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};
    std::fwrite(entry1, 1, 12, f);
    uint32_t nextIfd0 = 8;
    std::fwrite(&nextIfd0, 4, 1, f);

    std::fclose(f);
    return true;
}

// Big-endian ("MM") TIFF writer. Every multi-byte field is written high byte
// first, exactly as a Motorola-order camera file stores it.
struct BeWriter
{
    std::vector<uint8_t> bytes;
    void u16(uint16_t v)
    {
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    void u32(uint32_t v)
    {
        u16(static_cast<uint16_t>(v >> 16));
        u16(static_cast<uint16_t>(v & 0xFFFF));
    }
    void entry(uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
    {
        u16(tag);
        u16(type);
        u32(count);
        u32(value);
    }
    void entryRaw(uint16_t tag, uint16_t type, uint32_t count, const uint8_t raw[4])
    {
        u16(tag);
        u16(type);
        u32(count);
        bytes.insert(bytes.end(), raw, raw + 4);
    }
    void padTo(size_t offset)
    {
        if (bytes.size() < offset)
            bytes.resize(offset, 0);
    }
};

// MM TIFF: IFD0 { Make (offset), Model (inline, 3 bytes), ISO SHORT inline,
// CFAPattern BYTE x4 inline, ExposureTime RATIONAL, ExposureBias SRATIONAL,
// SubIFDs LONG x2 -> array } and two SubIFDs, each carrying one field.
static bool writeBigEndianTiff(const std::string &path)
{
    BeWriter w;
    const uint8_t hdr[4] = {'M', 'M', 0x00, 0x2A};
    w.bytes.insert(w.bytes.end(), hdr, hdr + 4);
    w.u32(8); // IFD0 at 8

    const uint16_t n0 = 7;
    w.u16(n0);
    const uint32_t data = 8 + 2 + n0 * 12 + 4; // 98
    const uint32_t makeOff = data;             // "Nikon\0" (6)
    const uint32_t expOff = data + 8;          // 1/125
    const uint32_t biasOff = data + 16;        // -2/3
    const uint32_t subArr = data + 24;         // 2 x LONG
    const uint32_t sub1 = data + 32;
    const uint32_t sub2 = sub1 + 2 + 12 + 4;

    w.entry(0x010F, 2, 6, makeOff);
    const uint8_t model[4] = {'Z', '9', 0, 0};
    w.entryRaw(0x0110, 2, 3, model);
    // SHORT count 1 inline: the value sits in the FIRST two bytes (MM order).
    const uint8_t iso[4] = {0x06, 0x40, 0x00, 0x00}; // 1600
    w.entryRaw(0x8827, 3, 1, iso);
    const uint8_t cfa[4] = {0, 1, 1, 2}; // R G G B in file order
    w.entryRaw(0x828E, 1, 4, cfa);
    w.entry(0x829A, 5, 1, expOff);
    w.entry(0x9204, 10, 1, biasOff);
    w.entry(0x014A, 4, 2, subArr);
    w.u32(0); // next IFD

    w.padTo(makeOff);
    const char make[8] = {'N', 'i', 'k', 'o', 'n', 0, 0, 0};
    w.bytes.insert(w.bytes.end(), make, make + 8);
    w.u32(1);
    w.u32(125);
    w.u32(static_cast<uint32_t>(-2));
    w.u32(3);
    w.u32(sub1);
    w.u32(sub2);

    w.padTo(sub1); // SubIFD #1: BitsPerSample = 14
    w.u16(1);
    const uint8_t bits[4] = {0x00, 0x0E, 0x00, 0x00};
    w.entryRaw(0x0102, 3, 1, bits);
    w.u32(0);

    w.padTo(sub2); // SubIFD #2: ImageWidth = 6048 (LONG)
    w.u16(1);
    w.entry(0x0100, 4, 1, 6048);
    w.u32(0);

    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(w.bytes.data(), 1, w.bytes.size(), f) == w.bytes.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");

    // 1) Direct IFD0 fake DNG
    const std::string rawPath = tmp.path().toStdString() + "/test.dng";
    CHECK(writeFakeDng(rawPath, 800, 1, 250, 85, 1), "fake DNG written");

    auto rm = mviewer::core::parseRawMetadata(rawPath);
    CHECK(rm.parsed, "RAW file parsed (extension recognized)");
    CHECK(rm.isoSpeed == 800, "ISO = 800");
    CHECK(rm.exposureSec > 0.0039 && rm.exposureSec < 0.0041, "exposure = 1/250s ~= 0.004");
    CHECK(rm.focalLength > 84.0 && rm.focalLength < 86.0, "focal = 85mm");
    CHECK(rm.make == "SONY", "make = SONY");
    CHECK(rm.model.starts_with("A7R"), "model starts with A7R");

    // 2) Exif IFD (0x8769) Pointer + Sensor Metadata + CFA Bayer pattern
    const std::string exifRawPath = tmp.path().toStdString() + "/exif_nested.cr2";
    CHECK(writeFakeDngWithExifIfd(exifRawPath), "Exif IFD nested CR2 written");

    auto rmExif = mviewer::core::parseRawMetadata(exifRawPath);
    CHECK(rmExif.parsed, "Nested Exif IFD parsed successfully");
    CHECK(rmExif.make == "Canon", "Make = Canon from IFD0");
    CHECK(rmExif.width == 8192, "Sensor active width = 8192");
    CHECK(rmExif.height == 5464, "Sensor active height = 5464");
    CHECK(rmExif.bitsPerSample == 14, "Sensor bit depth = 14");
    CHECK(rmExif.iso == 1600, "ISO = 1600 extracted from Exif IFD");
    CHECK(rmExif.exposureSec > 0.0019 && rmExif.exposureSec < 0.0021,
          "exposure = 1/500s from Exif IFD");
    CHECK(rmExif.fNumber > 2.79 && rmExif.fNumber < 2.81, "fNumber = 2.8 from Exif IFD");
    CHECK(rmExif.focalLength > 49.9 && rmExif.focalLength < 50.1, "focal = 50mm from Exif IFD");
    CHECK(rmExif.colorSpace == "sRGB", "ColorSpace = sRGB");
    CHECK(rmExif.bayerPattern == "RGGB", "Bayer CFA pattern = RGGB");

    // 2b) Big-endian (Motorola) file: inline BYTE / ASCII / SHORT values,
    // SRATIONAL exposure bias and a two-entry SubIFD array.
    const std::string bePath = tmp.path().toStdString() + "/motorola.nef";
    CHECK(writeBigEndianTiff(bePath), "big-endian NEF written");
    const auto be = mviewer::core::parseRawMetadata(bePath);
    CHECK(be.parsed, "big-endian TIFF parsed");
    CHECK(be.make == "Nikon", "MM Make read from offset");
    CHECK(be.model == "Z9", "MM 3-byte Model read inline, not as an offset");
    CHECK(be.iso == 1600, "MM inline SHORT ISO = 1600");
    CHECK(be.bayerPattern == "RGGB", "MM inline CFAPattern keeps file order (RGGB, not BGGR)");
    CHECK(be.exposureSec > 0.0079 && be.exposureSec < 0.0081, "MM exposure = 1/125s");
    CHECK(be.exposureCompensation < -0.66 && be.exposureCompensation > -0.67,
          "MM SRATIONAL exposure bias = -2/3 (signed)");
    CHECK(be.bitsPerSample == 14, "first SubIFD of a two-entry array parsed");
    CHECK(be.width == 6048, "second SubIFD of a two-entry array parsed");

    // 3) Hostile cyclic IFD -> must terminate cleanly without deadlock/infinite recursion
    const std::string cyclicPath = tmp.path().toStdString() + "/cyclic.nef";
    CHECK(writeCyclicTiff(cyclicPath), "Cyclic IFD written");
    auto rmCyclic = mviewer::core::parseRawMetadata(cyclicPath);
    CHECK(true, "Cyclic IFD terminated safely without hanging");

    // 4) Hostile out-of-bounds IFD offset (0xFFFFFF00)
    const std::string badOffsetPath = tmp.path().toStdString() + "/badoffset.arw";
    {
        FILE *bf = std::fopen(badOffsetPath.c_str(), "wb");
        if (bf)
        {
            uint8_t evilHdr[8] = {'I', 'I', 0x2A, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
            std::fwrite(evilHdr, 1, 8, bf);
            std::fclose(bf);
        }
    }
    auto rmBad = mviewer::core::parseRawMetadata(badOffsetPath);
    CHECK(!rmBad.parsed, "Wild IFD offset past EOF reports parsed=false without crash");

    // 4b) JPEG: the same TIFF/EXIF structure inside an APP1 "Exif" segment.
    {
        FILE *tf = std::fopen(exifRawPath.c_str(), "rb");
        std::string tiff;
        if (tf)
        {
            char buf[4096];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), tf)) > 0)
                tiff.append(buf, n);
            std::fclose(tf);
        }
        const std::string jpegExifPath = tmp.path().toStdString() + "/exif_camera.jpg";
        FILE *jf = std::fopen(jpegExifPath.c_str(), "wb");
        if (jf)
        {
            const size_t segLen = 2 + 6 + tiff.size();
            const uint8_t head[] = {0xFF,
                                    0xD8,
                                    0xFF,
                                    0xE0,
                                    0x00,
                                    0x04,
                                    0x00,
                                    0x00, // SOI, APP0
                                    0xFF,
                                    0xE1,
                                    static_cast<uint8_t>(segLen >> 8),
                                    static_cast<uint8_t>(segLen & 0xFF)};
            std::fwrite(head, 1, sizeof(head), jf);
            std::fwrite("Exif\0\0", 1, 6, jf);
            std::fwrite(tiff.data(), 1, tiff.size(), jf);
            const uint8_t eoi[] = {0xFF, 0xD9};
            std::fwrite(eoi, 1, 2, jf);
            std::fclose(jf);
        }
        const auto cap = mviewer::core::parseCaptureMetadata(jpegExifPath);
        CHECK(cap.parsed, "JPEG APP1 Exif parsed by parseCaptureMetadata");
        CHECK(cap.make == rmExif.make && cap.make == "Canon", "JPEG Make = Canon");
        CHECK(cap.iso == 1600, "JPEG ISO = 1600 from nested Exif IFD");
        CHECK(cap.focalLength > 49.9 && cap.focalLength < 50.1, "JPEG focal = 50mm");
        CHECK(!mviewer::core::parseRawMetadata(jpegExifPath).parsed,
              "parseRawMetadata stays RAW-only for JPEG");
        const auto capRaw = mviewer::core::parseCaptureMetadata(exifRawPath);
        CHECK(capRaw.parsed && capRaw.make == "Canon", "parseCaptureMetadata handles RAW too");
        CHECK(!mviewer::core::parseCaptureMetadata(badOffsetPath).parsed,
              "parseCaptureMetadata rejects wild offsets");
    }

    // 5) Non-RAW should return parsed=false
    const std::string jpgPath = tmp.path().toStdString() + "/test.jpg";
    auto rm2 = mviewer::core::parseRawMetadata(jpgPath);
    CHECK(!rm2.parsed, "non-RAW returns parsed=false");

    // 6) Empty path
    auto rm3 = mviewer::core::parseRawMetadata("");
    CHECK(!rm3.parsed, "empty path returns parsed=false");

    printf("\n==== RAW metadata test: %d/%d passed ====\n", g_pass, g_pass + g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
