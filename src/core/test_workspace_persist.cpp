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

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            printf("  PASS: %s\n", msg); \
            g_pass++;                    \
        }                                \
        else                             \
        {                                \
            printf("  FAIL: %s\n", msg); \
            g_fail++;                    \
        }                                \
        fflush(stdout);                  \
    } while (0)

int main(int argc, char** argv)
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

    mviewer::domain::Workspace back;
    CHECK(mviewer::core::deserializeWorkspace(json, back), "workspace deserialized");
    if (!back.folders.empty())
    {
        CHECK(back.rootPath == ws.rootPath, "root path survives round-trip");
        CHECK(back.folders.size() == ws.folders.size(), "folder count survives round-trip");
        CHECK(back.imageCount() == ws.imageCount(), "image count survives round-trip");
        CHECK(back.folders[0].imageSet.images[1].width == 4080,
              "nested image metadata (width) survives round-trip");
        CHECK(back.folders[1].imageSet.images[0].fileName == "3.png",
              "nested image filename survives round-trip");
    }
    else
    {
        CHECK(false, "deserialized workspace is non-empty");
    }

    // Re-parse the exact string to be sure deserialization is deterministic.
    mviewer::domain::Workspace back2;
    CHECK(mviewer::core::deserializeWorkspace(json, back2), "second parse of same JSON succeeds");

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
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
