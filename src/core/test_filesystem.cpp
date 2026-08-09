// M7 unit tests: FileSystem (Directory Scanner responsibility of ImageRepository).
// Review P0-① / ②: directory enumeration is an easy-regression area and must be
// covered directly, not only through the heavier ImageRepository::loadDirectory path.
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
#include <cstdlib>
static std::string srcRootFromThisFile()
{
    // Fallback: derive repo root from this file's location.
    return std::filesystem::path(__FILE__).parent_path().parent_path().string();
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            ++g_fail;                                                                              \
            printf("  FAIL: %s\n", msg);                                                           \
        }                                                                                          \
    } while (0)

// QFile takes a QString; write a tiny file and report success.
static bool writeFile(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    return f.open(QIODevice::WriteOnly);
}

static void testFileSystemScan()
{
    printf("\n[FileSystemScan]\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const std::string root = dir.path().toStdString();

    // Mixed set: 3 images (jpg/png/tiff) + 2 non-images (txt/json) + a subdir.
    CHECK(writeFile(root + "/a.jpg"), "write a.jpg");
    CHECK(writeFile(root + "/b.png"), "write b.png");
    CHECK(writeFile(root + "/c.tiff"), "write c.tiff");
    CHECK(writeFile(root + "/notes.txt"), "write notes.txt");
    CHECK(writeFile(root + "/meta.json"), "write meta.json");
    QDir(root.c_str()).mkdir("subdir");
    CHECK(writeFile(root + "/subdir/d.bmp"), "write subdir/d.bmp");

    const std::vector<std::string> images = FileSystem::listImages(root, 2000);
    // Non-recursive: should find a/b/c but NOT subdir/d.bmp.
    CHECK(images.size() == 3, "exactly 3 top-level images listed");
    for (const auto &p : images)
    {
        CHECK(FileSystem::isImage(p), ("isImage true for " + p).c_str());
    }
    CHECK(!FileSystem::isImage(root + "/notes.txt"), "isImage false for .txt");

    // max limit is honored.
    const std::vector<std::string> limited = FileSystem::listImages(root, 2);
    CHECK(limited.size() == 2, "max limit honored (2 of 3)");

    // max=0 means "no limit" (used by large-corpus scans).
    const std::vector<std::string> unlimited = FileSystem::listImages(root, 0);
    CHECK(unlimited.size() == 3, "max=0 means no limit (all 3 listed)");

    // Default cap must NOT truncate large directories (review gap: >2000 images
    // were silently truncated at the old 2000 default). Build 2500 images and
    // confirm the default listImages() returns all of them.
    QTemporaryDir big;
    CHECK(big.isValid(), "big temp dir created");
    const std::string bigRoot = big.path().toStdString();
    for (int i = 0; i < 2500; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "/img_%04d.jpg", i);
        CHECK(writeFile(bigRoot + name), "write big image");
    }
    const std::vector<std::string> bigDefault = FileSystem::listImages(bigRoot); // no max arg
    CHECK(bigDefault.size() == 2500, "default cap does not truncate 2500 images");

    // imageFilters lists the supported suffixes.
    const std::vector<std::string> filters = FileSystem::imageFilters();
    CHECK(!filters.empty(), "imageFilters non-empty");
}

// M25 phase 3 — supported-format SSOT. The Browse workflow must treat exactly
// the shipped decoder formats as "images" everywhere: RAW + WebP + GIF are
// decodable formats and must be listed/navigable like the historical six.
static void testSupportedFormatSSOT()
{
    printf("\n[SupportedFormatSSOT]\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const std::string root = dir.path().toStdString();

    CHECK(writeFile(root + "/r1.cr2"), "write r1.cr2");
    CHECK(writeFile(root + "/r2.dng"), "write r2.dng");
    CHECK(writeFile(root + "/r3.nef"), "write r3.nef");
    CHECK(writeFile(root + "/r4.arw"), "write r4.arw");
    CHECK(writeFile(root + "/r5.raf"), "write r5.raf");
    CHECK(writeFile(root + "/w.webp"), "write w.webp");
    CHECK(writeFile(root + "/g.gif"), "write g.gif");
    CHECK(writeFile(root + "/a.jpg"), "write a.jpg");
    CHECK(writeFile(root + "/notes.txt"), "write notes.txt");

    const std::vector<std::string> images = FileSystem::listImages(root, 0);
    int rawCount = 0;
    bool hasWebp = false;
    bool hasGif = false;
    bool hasJpg = false;
    for (const auto &p : images)
    {
        if (FileSystem::isImage(p))
        {
            if (p.find(".cr2") != std::string::npos || p.find(".dng") != std::string::npos ||
                p.find(".nef") != std::string::npos || p.find(".arw") != std::string::npos ||
                p.find(".raf") != std::string::npos)
                ++rawCount;
            if (p.find(".webp") != std::string::npos)
                hasWebp = true;
            if (p.find(".gif") != std::string::npos)
                hasGif = true;
            if (p.find(".jpg") != std::string::npos)
                hasJpg = true;
        }
        else
        {
            CHECK(false, ("every listed path passes isImage: " + p).c_str());
        }
    }
    CHECK(rawCount == 5, "RAW files are listed by FileSystem (cr2/dng/nef/arw/raf)");
    CHECK(hasWebp, "WebP files are listed by FileSystem");
    CHECK(hasGif, "GIF files are listed by FileSystem");
    CHECK(hasJpg, "JPEG files still listed by FileSystem");
    CHECK(images.size() == 8, "all 8 image formats listed, txt excluded");

    // The decoders must actually claim every suffix FileSystem lists (a listed
    // image that no decoder accepts would never produce a thumbnail).
    for (const auto &p : images)
    {
        const std::string ext = std::filesystem::path(p).extension().string();
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.size() > 1)
            lower = lower.substr(1);
        const auto decoderExts = Decoder::supportedExtensions(); // "*.ext"
        bool claimed = false;
        for (const auto &d : decoderExts)
            if (d == "*." + lower)
            {
                claimed = true;
                break;
            }
        CHECK(claimed, ("decoder claims every listed suffix: " + lower).c_str());
    }
}

static void testFileSystemEmptyDir()
{
    printf("\n[FileSystemEmptyDir]\n");
    QTemporaryDir dir;
    const std::vector<std::string> images = FileSystem::listImages(dir.path().toStdString(), 2000);
    CHECK(images.empty(), "empty directory yields no images");
    CHECK(FileSystem::listImages("/path/that/does/not/exist", 10).empty(),
          "missing directory yields no images (no throw)");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== FileSystem Tests (M7) ===\n");
    fflush(stdout);

    testFileSystemScan();
    testFileSystemEmptyDir();
    testSupportedFormatSSOT();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
