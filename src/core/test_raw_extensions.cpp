// One RAW extension catalog, agreed by every call site.

#include "core/image/ImageFormats.h"
#include "core/image/decoder/RawDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

static int g_failures = 0;

static void check(bool condition, const char *message)
{
    if (condition)
    {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

static std::string upperExt(std::string_view ext)
{
    std::string out(ext);
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

static void testCatalog()
{
    const auto &exts = mviewer::core::rawExtensions();
    check(exts.size() == 25, "RAW catalog has the union of the old lists");
    check(std::is_sorted(exts.begin(), exts.end()), "RAW catalog is sorted");
    const std::vector<std::string_view> required = {
        "3fr", "arw", "cr2", "cr3", "crw", "dcr", "dng", "erf", "fff", "iiq", "kdc", "mef", "mos",
        "mrw", "nef", "nrw", "orf", "pef", "raf", "raw", "rw2", "rwz", "sr2", "srw", "x3f"};
    check(exts.size() == required.size(), "required union size matches");
    for (std::size_t i = 0; i < required.size() && i < exts.size(); ++i)
        check(exts[i] == required[i], "catalog entry matches the union");

    const std::string joined = mviewer::core::rawTypeFilterData();
    check(joined.find("srw") != std::string::npos && joined.find("nrw") != std::string::npos,
          "type-filter payload includes srw and nrw");
    check(mviewer::core::suffixMatchesTypeFilter("srw", joined), ".srw passes the RAW filter");
    check(mviewer::core::suffixMatchesTypeFilter("nrw", joined), ".nrw passes the RAW filter");
    check(mviewer::core::suffixMatchesTypeFilter("SRW", joined), "SRW passes the RAW filter");
    check(mviewer::core::suffixMatchesTypeFilter("jpg", joined) == false,
          "jpg is not a RAW filter hit");
    check(mviewer::core::suffixMatchesTypeFilter("srw", "raw"), "raw alias matches srw");
    check(mviewer::core::suffixMatchesTypeFilter("nrw", "raw"), "raw alias matches nrw");
    check(mviewer::core::suffixMatchesTypeFilter("tif", "tiff"), "tiff alias matches tif");
    check(mviewer::core::suffixMatchesTypeFilter("tiff", "tiff"), "tiff alias matches tiff");
    check(mviewer::core::suffixMatchesTypeFilter("jpg", ""), "empty filter matches every suffix");
    check(mviewer::core::suffixMatchesTypeFilter("jpg", "png,jpeg") == false,
          "unlisted suffix fails");
}

static void testCallSitesAgree()
{
    const RawDecoder decoder;
    const auto listed = decoder.extensions();
    check(listed.size() == mviewer::core::rawExtensions().size(),
          "RawDecoder::extensions matches the catalog");
    const std::string joined = mviewer::core::rawTypeFilterData();
    for (const std::string_view ext : mviewer::core::rawExtensions())
    {
        const std::string lower(ext);
        const std::string upper = upperExt(ext);
        const std::string dotted = "." + upper;
        const bool known = mviewer::core::isRawExtension(lower);
        check(known, "catalog extension is recognized");
        check(mviewer::core::isRawExtension(upper) == known, "uppercase extension agrees");
        check(mviewer::core::isRawExtension(dotted) == known, "dotted extension agrees");
        check(mviewer::core::isRawPath("folder/shot." + lower) == known, "isRawPath agrees");
        check(mviewer::core::isRawPath("folder/shot." + upper) == known,
              "isRawPath uppercase agrees");
        check(decoder.canDecode("folder/shot." + lower) == known, "RawDecoder::canDecode agrees");
        check(decoder.canDecode("folder/shot." + upper) == known,
              "RawDecoder::canDecode uppercase agrees");
        check(mviewer::core::suffixMatchesTypeFilter(lower, joined) == known,
              "joined RAW filter agrees");
        check(mviewer::core::suffixMatchesTypeFilter(upper, "raw") == known, "raw alias agrees");
        check(std::find(listed.begin(), listed.end(), lower) != listed.end(),
              "extensions() lists the catalog entry");
    }
    check(mviewer::core::isRawExtension("jpg") == false, "jpeg is not RAW");
    check(decoder.canDecode("folder/shot.jpg") == false, "RawDecoder rejects jpeg");
    check(mviewer::core::isRawPath("folder.cr2/shot.jpg") == false,
          "directory names do not make a jpeg RAW");
}

int main()
{
    testCatalog();
    testCallSitesAgree();
    std::printf("test_raw_extensions: %s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
