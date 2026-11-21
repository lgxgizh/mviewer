// Data Model tests: domain Workspace/Folder/ImageSet value types + repository
// loadWorkspace building the hierarchy from a real directory tree.
#include "core/image/ImageRepository.h"
#include "domain/Image.h"
#include "domain/Workspace.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

using namespace mviewer::domain;

static std::string makeTempTree()
{
    std::error_code ec;
    const std::string base = std::filesystem::temp_directory_path(ec).string() + "/mviewer_dm_test";
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base + "/a", ec);
    std::filesystem::create_directories(base + "/b", ec);
    // Empty files with image extensions (FileSystem::listImages filters by ext).
    auto touch = [&](const std::string &p)
    {
        std::FILE *f = std::fopen(p.c_str(), "wb");
        if (f)
            std::fclose(f);
    };
    touch(base + "/root.png");
    touch(base + "/a/a1.jpg");
    touch(base + "/a/a2.jpg");
    touch(base + "/b/b1.png");
    return base;
}

int main()
{
    std::cout << "[Data Model tests]\n";

    // 1) Domain value-type logic with synthetic metadata.
    {
        Workspace ws;
        Folder f;
        f.path = "/tmp/x";
        f.name = "x";
        ImageSet set;
        ImageMetadata m1;
        m1.fileName = "one.png";
        m1.width = 10;
        m1.height = 20;
        ImageMetadata m2;
        m2.fileName = "two.jpg";
        set.images = {m1, m2};
        f.imageSet = set;
        ws.folders = {f};

        CHECK(ws.imageCount() == 2, "workspace imageCount == 2");
        CHECK(ws.folderCount() == 1, "workspace folderCount == 1");
        CHECK(ws.firstImageSet()->size() == 2, "firstImageSet has 2 images");
        CHECK(ws.firstImageSet()->indexOf("two.jpg") == 1, "indexOf finds two.jpg at 1");
        CHECK(ws.firstImageSet()->indexOf("nope.png") == -1, "indexOf returns -1 for missing");
        CHECK(!ws.empty(), "workspace not empty");
    }

    // 2) loadWorkspace builds the hierarchy from a real directory tree.
    {
        const std::string root = makeTempTree();
        const auto ws = ImageRepository::instance().loadWorkspace(root, 2000, true);
        CHECK(!ws.empty(), "loadWorkspace produced a non-empty workspace");
        CHECK(ws.rootPath == root, "workspace rootPath set");
        // root + a + b => 3 folders (root has root.png; a has 2; b has 1).
        CHECK(ws.folderCount() == 3, "3 folders discovered (root, a, b)");
        size_t total = 0;
        for (const auto &f : ws.folders)
            total += f.imageSet.size();
        CHECK(total == 4, "4 images total across folders");
        // Find folder 'a' and check its image set.
        const Folder *fa = nullptr;
        for (const auto &f : ws.folders)
            if (f.name == "a")
                fa = &f;
        CHECK(fa != nullptr, "folder 'a' present");
        CHECK(fa && fa->imageSet.size() == 2, "folder 'a' has 2 images");

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::cout << "Data Model tests done\n";
    return 0;
}
