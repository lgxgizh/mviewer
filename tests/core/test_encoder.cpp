// Encoder unit tests — format detection, supported formats list.
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include <QApplication>
#include <iostream>
#include <set>
#include <vector>

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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // formatForExtension (extensions passed WITHOUT the leading dot, as used
    // internally via QFileInfo::suffix()).
    CHECK(Encoder::formatForExtension("jpg") == "jpeg", "jpg -> jpeg");
    CHECK(Encoder::formatForExtension("jpeg") == "jpeg", "jpeg -> jpeg");
    CHECK(Encoder::formatForExtension("JPEG") == "jpeg", "JPEG case-insensitive");
    CHECK(Encoder::formatForExtension("png") == "png", "png -> png");
    CHECK(Encoder::formatForExtension("bmp") == "bmp", "bmp -> bmp");
    CHECK(Encoder::formatForExtension("webp") == "webp", "webp -> webp");

    // Unrecognized formats fall back to "png".
    CHECK(Encoder::formatForExtension("xyz") == "png", "unknown ext falls back to png");
    CHECK(Encoder::formatForExtension("") == "png", "empty falls back to png");

    // supportedOutputFormats
    auto fmts = Encoder::supportedOutputFormats();
    CHECK(!fmts.empty(), "supported formats non-empty");
    std::set<std::string> seen(fmts.begin(), fmts.end());
    CHECK(seen.size() == fmts.size(), "no duplicate format entries");
    CHECK(seen.count("jpeg") > 0 || seen.count("jpg") > 0, "JPEG supported");
    CHECK(seen.count("png") > 0, "PNG supported");

    // encodeToBuffer with null image
    {
        ImageData emptyData;
        std::vector<uint8_t> buf = Encoder::encodeToBuffer(emptyData, "png");
        CHECK(buf.empty(), "encode null ImageData returns empty");
    }

    std::cout << "\nEncoder: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
