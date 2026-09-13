// ImageTransform unit tests — rename pattern.
#include "core/image/ImageTransform.h"
#include <iostream>

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

int main()
{
    // applyRenamePattern
    auto r1 = mviewer::core::applyRenamePattern("{name}.copy", "photo", "jpg", 0, 10);
    CHECK(r1 == "photo.copy", "{name}.copy");

    auto r2 = mviewer::core::applyRenamePattern("{name}_{seq:3}", "photo", "jpg", 4, 10);
    CHECK(r2 == "photo_005", "{seq:3} zero-pads");

    auto r3 = mviewer::core::applyRenamePattern("img_{n}", "photo", "jpg", 0, 10);
    CHECK(r3 == "img_1", "{n} is 1-based");

    auto r4 = mviewer::core::applyRenamePattern("{name}_of_{total}", "photo", "jpg", 0, 5);
    CHECK(r4 == "photo_of_5", "{total}");

    auto r5 = mviewer::core::applyRenamePattern("{name}_v2.{ext}", "photo", "png", 0, 10);
    CHECK(r5 == "photo_v2.png", "{ext}");

    // Empty pattern
    auto r6 = mviewer::core::applyRenamePattern("", "file", "txt", 0, 1);
    CHECK(!r6.empty() || r6 == "file", "empty pattern handled");

    // A pattern produces a FILE NAME, never a path: BatchProcessor and ExportJob
    // join the result straight onto the destination directory, so directory
    // components and parent references must not survive.
    auto t1 = mviewer::core::applyRenamePattern("../../{name}", "photo", "jpg", 0, 1);
    CHECK(t1 == "photo", "traversal prefix is stripped");
    auto t2 = mviewer::core::applyRenamePattern("{name}/../{name}", "photo", "jpg", 0, 1);
    CHECK(t2 == "photo", "parent reference is stripped");
    auto t3 = mviewer::core::applyRenamePattern("..", "photo", "jpg", 0, 1);
    CHECK(t3 == "photo", "a dot-only pattern falls back to the base name");
    auto t4 = mviewer::core::applyRenamePattern("sub/{name}_x", "photo", "jpg", 0, 1);
    CHECK(t4 == "photo_x", "directory prefix is dropped, the leaf survives");

    std::cout << "\nImageTransform: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
