#include "core/compare/CompareEngine.h"
#include "core/image/ImageBuffer.h"

#include <QCoreApplication>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;

    // 1) Layout rules
    {
        auto l2 = CompareLayout::forCount(2);
        if (!(l2.cols == 2 && l2.rows == 1))
        {
            printf("LAYOUT2_FAIL %dx%d\n", l2.cols, l2.rows);
            fails++;
        }
        auto l3 = CompareLayout::forCount(3);
        if (!(l3.cols == 3 && l3.rows == 1))
        {
            printf("LAYOUT3_FAIL %dx%d\n", l3.cols, l3.rows);
            fails++;
        }
        auto l4 = CompareLayout::forCount(4);
        if (!(l4.cols == 2 && l4.rows == 2))
        {
            printf("LAYOUT4_FAIL %dx%d\n", l4.cols, l4.rows);
            fails++;
        }
        auto l7 = CompareLayout::forCount(7);
        if (!(l7.cols == 4 && l7.rows == 2))
        {
            printf("LAYOUT7_FAIL %dx%d\n", l7.cols, l7.rows);
            fails++;
        }
        printf("LAYOUT_OK\n");
    }

    // 2) Cell pos/size math
    {
        auto l = CompareLayout::forCount(4);
        CellSize vp{800, 600};
        auto c0 = l.cellPos(0, vp);
        auto c3 = l.cellPos(3, vp);
        if (!(c0.x == 0 && c0.y == 0))
        {
            printf("CELL0_FAIL %d,%d\n", c0.x, c0.y);
            fails++;
        }
        if (!(c3.x == 400 && c3.y == 300))
        {
            printf("CELL3_FAIL %d,%d\n", c3.x, c3.y);
            fails++;
        }
        auto cs = l.cellSize(vp);
        if (!(cs.w == 400 && cs.h == 300))
        {
            printf("CELLSIZE_FAIL %dx%d\n", cs.w, cs.h);
            fails++;
        }
        printf("CELLMATH_OK\n");
    }

    // 3) CompareEngine basic ops with real images
    {
        CompareEngine eng;
        std::vector<std::string> paths;
        paths.push_back("D:/photos/pixnio-4080x3072.jpg");
        paths.push_back("D:/photos/pixnio-6000x4000.jpg");
        eng.setImages(paths);
        if (eng.imageCount() != 2)
        {
            printf("ENG_COUNT_FAIL %d\n", eng.imageCount());
            fails++;
        }
        else
            printf("ENG_COUNT_OK\n");

        eng.setSyncEnabled(true);
        eng.setScale(0.5);
        if (!eng.syncEnabled() || std::abs(eng.syncTransform().scale - 0.5) > 1e-6)
        {
            printf("SYNC_FAIL\n");
            fails++;
        }
        else
            printf("SYNC_OK\n");

        eng.setBlinkIndex(1);
        if (eng.blinkIndex() != 1)
        {
            printf("BLINK_FAIL\n");
            fails++;
        }
        else
            printf("BLINK_OK\n");

        ImageData diff = eng.differenceMap(1, 0);
        if (diff.isNull())
        {
            printf("DIFF_FAIL null\n");
            fails++;
        }
        else if (diff.format != PixelFormat::Grayscale8)
        {
            printf("DIFF_FAIL fmt=%d\n", static_cast<int>(diff.format));
            fails++;
        }
        else
            printf("DIFF_OK %dx%d\n", diff.width, diff.height);

        eng.removeImage(0);
        if (eng.imageCount() != 1)
        {
            printf("REMOVE_FAIL %d\n", eng.imageCount());
            fails++;
        }
        else
            printf("REMOVE_OK\n");

        eng.clear();
        if (eng.imageCount() != 0)
        {
            printf("CLEAR_FAIL %d\n", eng.imageCount());
            fails++;
        }
        else
            printf("CLEAR_OK\n");
    }

    printf(fails == 0 ? "ALL_COMPARE_OK=%d\n" : "FAILS=%d\n", fails);
    return fails == 0 ? 0 : 1;
}
