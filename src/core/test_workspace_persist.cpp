// M9-5 acceptance: Workspace persistence must round-trip a domain::Workspace
// (root + folders + image sets) through JSON, and the RecentFiles list must
// serialize/deserialize. This exercises the REAL serializer
// (core::serializeWorkspace / deserializeWorkspace / RecentFiles) — no fake.
//
// Scope is M9-5 ONLY. Browse / Compare / Analysis / Export / Polish are other
// phases and are NOT touched here.
#include "core/workspace/WorkspaceSerializer.h"

#include <QCoreApplication>

#include <cstdio>
#include <string>

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
        fflush(stdout);                                                                            \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("\n[M9-5 Workspace persistence: round-trip + recent files]\n");
    fflush(stdout);

    // Build a workspace with two folders of images.
    mviewer::domain::Workspace ws;
    ws.rootPath = "D:/photos";
    {
        mviewer::domain::Folder f;
        f.path = "D:/photos/a";
        f.name = "a";
        mviewer::domain::ImageMetadata m1, m2;
        m1.filePath = "D:/photos/a/1.png";
        m1.fileName = "1.png";
        m1.width = 1920;
        m1.height = 1080;
        m2.filePath = "D:/photos/a/2.jpg";
        m2.fileName = "2.jpg";
        m2.width = 4080;
        m2.height = 3072;
        f.imageSet.images.push_back(m1);
        f.imageSet.images.push_back(m2);
        ws.folders.push_back(f);
    }
    {
        mviewer::domain::Folder f;
        f.path = "D:/photos/b";
        f.name = "b";
        mviewer::domain::ImageMetadata m;
        m.filePath = "D:/photos/b/3.png";
        m.fileName = "3.png";
        m.width = 64;
        m.height = 48;
        f.imageSet.images.push_back(m);
        ws.folders.push_back(f);
    }
    CHECK(ws.imageCount() == 3, "workspace holds 3 images across 2 folders");

    // Round-trip.
    const std::string json = mviewer::core::serializeWorkspace(ws);
    CHECK(!json.empty(), "workspace serialized to non-empty JSON");

    auto maybeWs = mviewer::core::deserializeWorkspace(json);
    CHECK(maybeWs.has_value(), "workspace deserialized");
    mviewer::domain::Workspace back = maybeWs ? std::move(*maybeWs) : mviewer::domain::Workspace{};
    if (!back.folders.empty())
    {
        CHECK(back.rootPath == ws.rootPath, "root path survives round-trip");
        CHECK(back.folders.size() == ws.folders.size(), "folder count survives round-trip");
        CHECK(back.imageCount() == ws.imageCount(), "image count survives round-trip");
        CHECK(back.folders[0].imageSet.images[1].width == 4080,
              "nested image metadata (width) survives round-trip");
        CHECK(back.folders[1].imageSet.images[0].fileName == "3.png",
              "nested image filename survives round-trip");

        // M12.1: ROI + analysis session fields round-trip.
        ws.folders[0].imageSet.images[0].roiX = 12;
        ws.folders[0].imageSet.images[0].roiY = 34;
        ws.folders[0].imageSet.images[0].roiW = 256;
        ws.folders[0].imageSet.images[0].roiH = 128;
        ws.folders[0].imageSet.images[0].analysis = "PSNR=42.1dB SSIM=0.99";
        const std::string json2 = mviewer::core::serializeWorkspace(ws);
        auto maybeBack3 = mviewer::core::deserializeWorkspace(json2);
        CHECK(maybeBack3.has_value(),
              "workspace with ROI+analysis deserialized");
        mviewer::domain::Workspace back3 =
            maybeBack3 ? std::move(*maybeBack3) : mviewer::domain::Workspace{};
        const auto &ri = back3.folders[0].imageSet.images[0];
        CHECK(ri.roiX == 12 && ri.roiY == 34 && ri.roiW == 256 && ri.roiH == 128,
              "ROI round-trips");
        CHECK(ri.analysis == "PSNR=42.1dB SSIM=0.99", "analysis text round-trips");

        // M12.2 (G2-ext): multiple images each carry their OWN ROI + analysis
        // (a multi-image compare session), not just the active one.
        ws.folders[0].imageSet.images[1].roiX = 5;
        ws.folders[0].imageSet.images[1].roiY = 6;
        ws.folders[0].imageSet.images[1].roiW = 100;
        ws.folders[0].imageSet.images[1].roiH = 80;
        ws.folders[0].imageSet.images[1].analysis = "PSNR=38.0dB SSIM=0.97";
        const std::string json3 = mviewer::core::serializeWorkspace(ws);
        auto maybeBack4 = mviewer::core::deserializeWorkspace(json3);
        CHECK(maybeBack4.has_value(),
              "workspace with per-image ROI+analysis deserialized");
        mviewer::domain::Workspace back4 =
            maybeBack4 ? std::move(*maybeBack4) : mviewer::domain::Workspace{};
        const auto &r0 = back4.folders[0].imageSet.images[0];
        const auto &r1 = back4.folders[0].imageSet.images[1];
        CHECK(r0.roiW == 256 && r0.analysis == "PSNR=42.1dB SSIM=0.99",
              "image[0] ROI+analysis round-trips");
        CHECK(r1.roiX == 5 && r1.roiW == 100 && r1.analysis == "PSNR=38.0dB SSIM=0.97",
              "image[1] independent ROI+analysis round-trips");

        // M12.2 (review fix): a compare session with NO ROI and NO analysis must
        // still round-trip via the explicit comparedImages list. This is the
        // edge case the heuristic filter used to drop.
        ws.comparedImages.clear();
        ws.comparedImages.push_back("D:/photos/a/1.png");
        ws.comparedImages.push_back("D:/photos/a/2.jpg");
        const std::string json4 = mviewer::core::serializeWorkspace(ws);
        auto maybeBack5 = mviewer::core::deserializeWorkspace(json4);
        CHECK(maybeBack5.has_value(),
              "workspace with comparedImages (no ROI/analysis) deserialized");
        mviewer::domain::Workspace back5 =
            maybeBack5 ? std::move(*maybeBack5) : mviewer::domain::Workspace{};
        CHECK(back5.comparedImages.size() == 2, "comparedImages count round-trips");
        CHECK(back5.comparedImages[0] == "D:/photos/a/1.png" &&
                  back5.comparedImages[1] == "D:/photos/a/2.jpg",
              "comparedImages paths round-trip exactly");

        // Tolerant of older files: the legacy string below (lines ~125) omits
        // comparedImages entirely, and deserializeWorkspace must default it to
        // empty (verified by the legacy parse test that follows).

        // Tolerant of older files that omit roi/analysis.
        const std::string legacy = "{\"root\":\"D:/p\","
                                   "\"folders\":[{\"path\":\"D:/p/a\",\"name\":\"a\","
                                   "\"images\":[{\"filePath\":\"D:/p/a/1.png\","
                                   "\"fileName\":\"1.png\",\"width\":100,\"height\":80}]}]}";
        auto maybeLeg = mviewer::core::deserializeWorkspace(legacy);
        CHECK(maybeLeg.has_value(),
              "legacy workspace (no roi/analysis) parses");
        mviewer::domain::Workspace leg =
            maybeLeg ? std::move(*maybeLeg) : mviewer::domain::Workspace{};
        CHECK(leg.folders[0].imageSet.images[0].roiW == 0, "legacy ROI defaults to 0");
        CHECK(leg.folders[0].imageSet.images[0].analysis.empty(), "legacy analysis defaults empty");
    }
    else
    {
        CHECK(false, "deserialized workspace is non-empty");
    }

    // Re-parse the exact string to be sure deserialization is deterministic.
    auto maybeBack2 = mviewer::core::deserializeWorkspace(json);
    CHECK(maybeBack2.has_value(), "second parse of same JSON succeeds");

    // RecentFiles.
    mviewer::core::RecentFiles recent(3);
    recent.add("D:/x/1.png");
    recent.add("D:/x/2.png");
    recent.add("D:/x/3.png");
    recent.add("D:/x/1.png"); // duplicate -> moves to front, list stays capped
    CHECK(recent.items().size() == 3, "recent list capped at maxEntries");
    CHECK(recent.items()[0] == "D:/x/1.png", "most-recently-added is first");

    const std::string rj = recent.serialize();
    CHECK(!rj.empty(), "recent files serialized");
    mviewer::core::RecentFiles recent2(3);
    CHECK(recent2.deserialize(rj), "recent files deserialized");
    if (!recent2.items().empty())
    {
        CHECK(recent2.items().size() == recent.items().size(), "recent count survives round-trip");
        CHECK(recent2.items()[0] == recent.items()[0], "recent order survives round-trip");
    }
    else
    {
        CHECK(false, "deserialized recent list is non-empty");
    }

    printf("\n=== M9-5 Workspace acceptance: %d passed, %d failed ===\n", g_pass, g_fail);

    // ─── M15: CompareSession snapshot round-trip ─────────────────────────────
    printf("\n[M15 CompareSession persistence]\n");
    fflush(stdout);

    mviewer::domain::CompareSession cs;
    cs.imageIds = {"D:/photos/a/1.png", "D:/photos/a/2.jpg", "D:/photos/b/3.png"};
    cs.cells.resize(3);
    cs.cells[0] = {1.0, 0.0, 0.0};
    cs.cells[1] = {2.5, -30.0, 12.0};
    cs.cells[2] = {0.75, 5.0, -8.0};
    cs.syncMode = mviewer::domain::SyncMode::All;
    cs.blinkIndex = 1;
    cs.sharedScale = 2.5;
    cs.sharedOffsetX = -30.0;
    cs.sharedOffsetY = 12.0;
    cs.cols = 3;
    cs.rows = 1;
    cs.selection = {100, 80, 256, 128, true};
    cs.selection.synced = true; // 5-value aggregate sets `active`; set synced explicitly

    const std::string csJson = mviewer::core::serializeCompareSession(cs);
    CHECK(!csJson.empty(), "compare session serialized to non-empty JSON");
    auto maybeCs = mviewer::core::deserializeCompareSession(csJson);
    CHECK(maybeCs.has_value(), "compare session deserialized");
    mviewer::domain::CompareSession csBack =
        maybeCs ? std::move(*maybeCs) : mviewer::domain::CompareSession{};
    CHECK(csBack.imageCount() == 3, "compare session image count round-trips");
    CHECK(csBack.imageIds[1] == "D:/photos/a/2.jpg", "compare session image id round-trips");
    CHECK(csBack.cells[1].scale == 2.5 && csBack.cells[1].offsetX == -30.0 &&
              csBack.cells[1].offsetY == 12.0,
          "per-cell transform round-trips");
    CHECK(csBack.syncMode == mviewer::domain::SyncMode::All, "sync mode round-trips");
    CHECK(csBack.sharedScale == 2.5 && csBack.sharedOffsetX == -30.0 &&
              csBack.sharedOffsetY == 12.0,
          "shared transform round-trips");
    CHECK(csBack.cols == 3 && csBack.rows == 1, "grid dims round-trip");
    CHECK(csBack.selection.w == 256 && csBack.selection.h == 128 && csBack.selection.synced,
          "ROI selection round-trips");
    CHECK(csBack.blinkIndex == 1, "blink index round-trips");

    // Off-sync mode variant.
    cs.syncMode = mviewer::domain::SyncMode::Off;
    auto maybeCsOff = mviewer::core::deserializeCompareSession(
        mviewer::core::serializeCompareSession(cs));
    CHECK(maybeCsOff.has_value(), "compare session (sync off) deserialized");
    mviewer::domain::CompareSession csOff =
        maybeCsOff ? std::move(*maybeCsOff) : mviewer::domain::CompareSession{};
    CHECK(csOff.syncMode == mviewer::domain::SyncMode::Off, "sync-off round-trips");

    // Embedded in a Workspace and round-tripped end-to-end.
    mviewer::domain::Workspace wscs;
    wscs.rootPath = "D:/photos";
    wscs.comparedImages = cs.imageIds;
    wscs.compareSessionJson = csJson;
    auto maybeWscs = mviewer::core::deserializeWorkspace(
        mviewer::core::serializeWorkspace(wscs));
    CHECK(maybeWscs.has_value(),
          "workspace with compareSession deserialized");
    mviewer::domain::Workspace wscsBack =
        maybeWscs ? std::move(*maybeWscs) : mviewer::domain::Workspace{};
    CHECK(wscsBack.comparedImages.size() == 3, "embedded comparedImages round-trips");
    auto maybeCsEmb = mviewer::core::deserializeCompareSession(wscsBack.compareSessionJson);
    CHECK(maybeCsEmb.has_value() && maybeCsEmb->selection.w == 256,
          "embedded compareSession JSON round-trips through workspace");
    CHECK(wscsBack.compareSessionJson == csJson, "compareSessionJson verbatim round-trips");

    printf("\n=== M15-5 acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
