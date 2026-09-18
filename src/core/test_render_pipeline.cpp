// M7 Render Pipeline foundation: Viewport transform + TileGrid visibility math.
// These are domain-free (std only); this test verifies the pan/zoom/visible-tile
// computations that the Widget will drive, without requiring a display.
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

static void testFullscreenFitGeometry()
{
    printf("\n[Viewport::fullscreen fit geometry]\n");
    fflush(stdout);
    struct Case
    {
        int w;
        int h;
    };
    for (const auto &c :
         {Case{1600, 900}, Case{900, 1600}, Case{1000, 1000}, Case{2400, 400}, Case{400, 2400}})
    {
        Viewport vp(1200, 800, 1.0, 0.0, 0.0);
        vp.fit(c.w, c.h, FitPolicy::MaximizeClient);
        const double dw = c.w * vp.scale;
        const double dh = c.h * vp.scale;
        CHECK(dw <= vp.screenW + 1e-6 && dh <= vp.screenH + 1e-6,
              "max-fit stays inside the fullscreen client area");
        CHECK(std::abs(dw / dh - static_cast<double>(c.w) / c.h) < 1e-9,
              "max-fit preserves the source aspect ratio");
        CHECK(std::abs(vp.offsetX - (vp.screenW - dw) / 2.0) < 1e-9 &&
                  std::abs(vp.offsetY - (vp.screenH - dh) / 2.0) < 1e-9,
              "max-fit centers the image");
    }
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

    // Fully off-image: offset far positive -> empty
    Viewport vp2(800, 800, 1.0, 5000.0, 5000.0);
    vp2.visibleImageRect(1000, 1000, x, y, w, h);
    CHECK(w == 0 && h == 0, "off-image viewport (far positive) reports empty visible rect");

    // Fully off-image: offset far negative -> empty (prevents negative width/height)
    Viewport vp3(800, 800, 1.0, -5000.0, -5000.0);
    vp3.visibleImageRect(1000, 1000, x, y, w, h);
    CHECK(w == 0 && h == 0, "off-image viewport (far negative) reports empty visible rect");
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

static void testViewportEdgeAndOffscreen()
{
    printf("\n[Viewport edge and offscreen bounds]\n");
    fflush(stdout);

    // Negative / far off-screen right/bottom: visible rect must be empty, not negative.
    Viewport vpOff(800, 600, 1.0, -2000.0, -2000.0);
    int x = -1, y = -1, w = -1, h = -1;
    vpOff.visibleImageRect(1000, 1000, x, y, w, h);
    CHECK(w == 0 && h == 0 && x == 0 && y == 0, "far offscreen viewport returns empty w=0, h=0");

    // Invalid screen dimensions
    Viewport vpZeroScreen(0, 0, 1.0, 0.0, 0.0);
    vpZeroScreen.visibleImageRect(100, 100, x, y, w, h);
    CHECK(w == 0 && h == 0, "zero screen viewport returns empty");

    // Non-finite scale or translation
    Viewport vpNan(800, 600, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
    vpNan.visibleImageRect(100, 100, x, y, w, h);
    CHECK(w == 0 && h == 0, "NaN scale viewport returns empty");

    // ZoomAt with NaN factor must safely do nothing
    Viewport vpZoom(800, 600, 1.0, 0.0, 0.0);
    vpZoom.zoomAt(400.0, 300.0, std::numeric_limits<double>::quiet_NaN());
    CHECK(vpZoom.scale == 1.0, "NaN zoom factor ignored");

    // Fit with non-positive or NaN margin safely defaults
    Viewport vpFit(800, 600, 1.0, 0.0, 0.0);
    vpFit.fit(1600, 1200, -1.0);
    CHECK(vpFit.scale > 0.0 && std::isfinite(vpFit.scale),
          "negative margin in fit safely defaults");
}

static void testTileGridBoundsAndClamping()
{
    printf("\n[TileGrid bounds and overflow clamping]\n");
    fflush(stdout);

    // Negative image dimensions clamp to 0
    TileGrid negGrid(-500, -300, 256);
    CHECK(negGrid.cols() == 0 && negGrid.rows() == 0, "negative dimensions yield 0 cols and rows");

    // Zero tileSize defaults to 256
    TileGrid zeroTs(1000, 1000, 0);
    CHECK(zeroTs.tileSize == 256, "zero tileSize defaults to 256");

    // Extreme image dimensions do not overflow 32-bit integer arithmetic
    TileGrid largeGrid(100000000, 100000000, 256);
    CHECK(largeGrid.cols() > 0 && largeGrid.rows() > 0, "large grid computes valid row/col count");
}

static void testRenderEngineBoundsAndScaling()
{
    printf("\n[RenderEngine bounds and scaling]\n");
    fflush(stdout);
    auto &engine = RenderEngine::instance();

    // 1x1 image scaled via Bilinear mode: must not dereference out of bounds
    ImageData img1x1 = makeImageData(1, 1, PixelFormat::RGB24);
    img1x1.buffer->data()[0] = 42;
    img1x1.buffer->data()[1] = 84;
    img1x1.buffer->data()[2] = 126;
    ImageData scaledBilinear = engine.scale(img1x1, {50, 50}, RenderInterp::Bilinear);
    CHECK(!scaledBilinear.isNull() && scaledBilinear.width == 50 && scaledBilinear.height == 50,
          "bilinear scale on 1x1 image succeeds without out-of-bounds access");

    // 1x10 and 10x1 images scaled via Bilinear
    ImageData img1x10 = makeImageData(1, 10, PixelFormat::RGB24);
    ImageData scaled1x10 = engine.scale(img1x10, {20, 20}, RenderInterp::Bilinear);
    CHECK(!scaled1x10.isNull() && scaled1x10.width == 20 && scaled1x10.height == 20,
          "bilinear scale on 1x10 image succeeds");

    // Bounded display scaling via scaleBoundedStatic
    ImageData img100 = makeImageData(100, 100, PixelFormat::RGB24);
    for (int i = 0; i < 100 * 100 * 3; ++i)
        img100.buffer->data()[static_cast<size_t>(i)] = static_cast<uint8_t>(i % 256);
    ImageData bounded = RenderEngine::scaleBoundedStatic(img100, {25, 25});
    CHECK(!bounded.isNull() && bounded.width == 25 && bounded.height == 25,
          "scaleBoundedStatic scales image accurately");

    // Overlay difference alpha bounds fast paths
    ImageData ovZero = engine.overlayDifference(img100, img100, 0.0);
    CHECK(!ovZero.isNull() && ovZero.width == 100, "overlayDifference at alpha=0 succeeds");
}

int main()
{
    printf("=== Render Pipeline foundation tests (M7) ===\n");
    fflush(stdout);
    testViewportFit();
    testFullscreenFitGeometry();
    testViewportZoomAt();
    testViewportVisibleRect();
    testTileGrid();
    testViewportEdgeAndOffscreen();
    testTileGridBoundsAndClamping();
    testRenderEngineBoundsAndScaling();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
