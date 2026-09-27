// Viewport region-tile selection and RAW LOD intent. No display, no LibRaw.

#include "core/image/decoder/RawDecodePlan.h"
#include "core/render/RegionTileSelect.h"

#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (0)

static void testTileFill()
{
    using mviewer::core::chooseTileFill;
    using mviewer::core::TileFillPath;
    const auto native = chooseTileFill(true, true, false, false, false, false);
    CHECK(native.path == TileFillPath::NativeRegion, "zoomed-in, no frame: native region");
    CHECK(!native.decodesFullRaster, "native region does not full-decode");

    const auto bounded = chooseTileFill(true, false, true, false, false, false);
    CHECK(bounded.path == TileFillPath::BoundedCrop, "zoomed-in, no frame: bounded crop");

    const auto resident = chooseTileFill(true, true, true, false, true, false);
    CHECK(resident.path == TileFillPath::FullFrameScale,
          "1:1 tile with a resident frame scales that frame");

    const auto coarse = chooseTileFill(true, false, true, false, true, true);
    CHECK(coarse.path == TileFillPath::BoundedCrop, "coarse tile prefers bounded crop");

    const auto lod = chooseTileFill(true, true, true, true, false, true);
    CHECK(lod.path == TileFillPath::ReducedLod, "near-full tile uses decodeLod");

    const auto skip = chooseTileFill(true, false, false, false, false, false);
    CHECK(skip.path == TileFillPath::Skip, "no partial path and no frame: skip");
}

static void testRing()
{
    const auto tiles = mviewer::core::planViewportTiles(1, 1, 2, 2, 8, 8, 1, 32);
    int visible = 0;
    int extra = 0;
    for (const auto &tile : tiles)
    {
        if (tile.visible)
            ++visible;
        else
            ++extra;
    }
    CHECK(visible == 4, "2x2 visible tiles");
    CHECK(extra > 0 && extra <= 32, "ring is non-empty and capped");

    Viewport vp(100, 80, 2.0, 10.0, 4.0);
    const Viewport wide = mviewer::core::inflateViewportForTileRing(vp, 256, 1);
    CHECK(wide.scale == vp.scale, "ring keeps zoom (same LOD key)");
    CHECK(wide.screenW > vp.screenW && wide.screenH > vp.screenH, "ring grows the viewport");
    CHECK(wide.offsetX < vp.offsetX && wide.offsetY < vp.offsetY, "ring shifts origin out");
}

static void testRawPlan()
{
    using mviewer::core::chooseRawFull;
    using mviewer::core::chooseRawLod;
    using mviewer::core::RawFullIntent;
    using mviewer::core::rawHalfCoversRequest;
    using mviewer::core::RawLodIntent;

    CHECK(chooseRawLod(2000, 512, true) == RawLodIntent::Preview, "preview covers the request");
    CHECK(chooseRawLod(160, 1024, true) == RawLodIntent::TryHalf, "short preview tries half-size");
    CHECK(chooseRawLod(160, 1024, false) == RawLodIntent::Preview,
          "without LibRaw a short preview is still the fast path");
    CHECK(chooseRawLod(0, 1024, false) == RawLodIntent::Empty,
          "no preview and no LibRaw: empty LOD");
    CHECK(chooseRawLod(800, 0, true) == RawLodIntent::Preview,
          "full ask keeps an embedded preview");
    CHECK(chooseRawLod(0, 0, true) == RawLodIntent::Empty,
          "full ask without preview is not an LOD");
    CHECK(rawHalfCoversRequest(6000, 1024), "half-size covers maxEdge << sensor");
    CHECK(!rawHalfCoversRequest(6000, 4000), "half-size does not cover a near-full request");
    CHECK(chooseRawFull(1200, true, 8'000'000) == RawFullIntent::Preview,
          "full decode keeps a usable preview");
    CHECK(chooseRawFull(0, true, 8'000'000) == RawFullIntent::FullDemosaic,
          "no preview: full demosaic");
    CHECK(chooseRawFull(0, true, 256) == RawFullIntent::Empty, "tiny fixture is not demosaiced");
    CHECK(chooseRawFull(0, false, 8'000'000) == RawFullIntent::Empty,
          "no LibRaw: preview-less stays empty");
}

int main()
{
    printf("\n[region tile selection]\n");
    testTileFill();
    testRing();
    testRawPlan();
    printf("\n=== region tiles: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
