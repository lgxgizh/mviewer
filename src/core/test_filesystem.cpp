// M7 unit tests: FileSystem (Directory Scanner responsibility of ImageRepository).
// Review P0-① / ②: directory enumeration is an easy-regression area and must be
// covered directly, not only through the heavier ImageRepository::loadDirectory path.
#include "core/filesystem/FileSystem.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
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

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            ++g_pass;                    \
        }                                \
        else                             \
        {                                \
            ++g_fail;                    \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

// QFile takes a QString; write a tiny file and report success.
static bool writeFile(const std::string& path)
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
    for (const auto& p : images)
    {
        CHECK(FileSystem::isImage(p), ("isImage true for " + p).c_str());
    }
    CHECK(!FileSystem::isImage(root + "/notes.txt"), "isImage false for .txt");

    // max limit is honored.
    const std::vector<std::string> limited = FileSystem::listImages(root, 2);
    CHECK(limited.size() == 2, "max limit honored (2 of 3)");

    // imageFilters lists the supported suffixes.
    const std::vector<std::string> filters = FileSystem::imageFilters();
    CHECK(!filters.empty(), "imageFilters non-empty");
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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    printf("=== FileSystem Tests (M7) ===\n");
    fflush(stdout);

    testFileSystemScan();
    testFileSystemEmptyDir();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
