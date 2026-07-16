// M7 Render Pipeline foundation: Viewport transform + TileGrid visibility math.
// These are domain-free (std only); this test verifies the pan/zoom/visible-tile
// computations that the Widget will drive, without requiring a display.
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"

#include <cstdio>

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

static void testViewportFit()
{
    printf("\n[Viewport::fit]\n");
    fflush(stdout);
    Viewport vp;
    vp.screenW = 800;
    vp.screenH = 600;
    vp.fit(1600, 1200, 1.0); // image is 2x screen in both axes
    CHECK(vp.scale > 0.49 && vp.scale < 0.51, "fit scale ~0.5 (image 2x screen)");
    // centered: image 1600*0.5=800 wide fills screen width
    CHECK(std::abs(vp.offsetX) < 1e-6, "fit centers horizontally (offsetX ~0)");
    CHECK(std::abs(vp.offsetY) < 1e-6, "fit centers vertically (offsetY ~0)");
}

static void testViewportZoomAt()
{
    printf("\n[Viewport::zoomAt]\n");
    fflush(stdout);
    Viewport vp(800, 600, 1.0, 0.0, 0.0);
    // Anchor at screen (400,300), zoom in 2x. The image point under the anchor
    // must stay fixed.
    const double imgXBefore = (400.0 - vp.offsetX) / vp.scale;
    const double imgYBefore = (300.0 - vp.offsetY) / vp.scale;
    vp.zoomAt(400.0, 300.0, 2.0);
    CHECK(vp.scale == 2.0, "zoomAt doubles scale");
    const double imgXAfter = (400.0 - vp.offsetX) / vp.scale;
    const double imgYAfter = (300.0 - vp.offsetY) / vp.scale;
    CHECK(std::abs(imgXAfter - imgXBefore) < 1e-6, "zoomAt keeps image point X fixed under anchor");
    CHECK(std::abs(imgYAfter - imgYBefore) < 1e-6, "zoomAt keeps image point Y fixed under anchor");

    // Clamp test
    vp.zoomAt(0, 0, 1000.0);
    CHECK(vp.scale <= 50.0, "scale clamped to max 50");
    vp.zoomAt(0, 0, 0.0001);
    CHECK(vp.scale >= 0.05, "scale clamped to min 0.05");
}

static void testViewportVisibleRect()
{
    printf("\n[Viewport::visibleImageRect]\n");
    fflush(stdout);
    // Image 1000x1000, scale 1, offset (-100,-100): widget shows image px [100,900]x[100,900]
    Viewport vp(800, 800, 1.0, -100.0, -100.0);
    int x, y, w, h;
    vp.visibleImageRect(1000, 1000, x, y, w, h);
    CHECK(x == 100 && y == 100, "visible rect origin clamped to image bounds");
    CHECK(w == 800 && h == 800, "visible rect size = widget / scale");

    // Fully off-image: offset far away -> empty
    Viewport vp2(800, 800, 1.0, 5000.0, 5000.0);
    vp2.visibleImageRect(1000, 1000, x, y, w, h);
    CHECK(w == 0 && h == 0, "off-image viewport reports empty visible rect");
}

static void testTileGrid()
{
    printf("\n[TileGrid]\n");
    fflush(stdout);
    // 1000x1000 image, 256px tiles -> 4x4 = 16 tiles total.
    TileGrid grid(1000, 1000, 256);
    CHECK(grid.cols() == 4 && grid.rows() == 4, "tile grid is 4x4 for 1000px / 256");
    CHECK(grid.visibleTiles(Viewport()).empty(), "no viewport -> no visible tiles");

    // Fully-zoomed-out fit in 800x800: whole image visible -> all 16 tiles.
    Viewport vp(800, 800, 0.8, 0.0, 0.0);
    auto tiles = grid.visibleTiles(vp);
    CHECK(tiles.size() == 16, "fully-visible image yields all 16 tiles");

    // Zoomed into top-left quarter: only tiles (0,0),(1,0),(0,1),(1,1) visible.
    Viewport vp2(800, 800, 2.0, 0.0, 0.0); // 2x zoom, image top-left at screen 0,0
    auto tiles2 = grid.visibleTiles(vp2);
    CHECK(tiles2.size() == 4, "2x zoom into corner yields 4 tiles");
    bool has00 = false, has33 = false;
    for (const auto &t : tiles2)
    {
        if (t.coord.col == 0 && t.coord.row == 0)
            has00 = true;
        if (t.coord.col == 3 && t.coord.row == 3)
            has33 = true; // bottom-right tile must NOT be visible when zoomed into corner
    }
    CHECK(has00, "corner zoom includes tile (0,0)");
    CHECK(!has33, "corner zoom excludes far tile (3,3)");

    // Last tile is clamped to image edge (1000 - 3*256 = 232 wide).
    Tile last;
    for (const auto &t : tiles)
        if (t.coord.col == 3 && t.coord.row == 3)
            last = t;
    CHECK(last.srcW == 232 && last.srcH == 232, "edge tile clamped to remaining pixels");
}

int main()
{
    printf("=== Render Pipeline foundation tests (M7) ===\n");
    fflush(stdout);
    testViewportFit();
    testViewportZoomAt();
    testViewportVisibleRect();
    testTileGrid();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
