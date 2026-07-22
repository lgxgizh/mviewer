// ImageTransform unit tests — rename pattern.
#include "core/image/ImageTransform.h"
#include <iostream>

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++g_fail; } \
        else { std::cout << "PASS: " << msg << "\n"; } \
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

    std::cout << "\nImageTransform: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
