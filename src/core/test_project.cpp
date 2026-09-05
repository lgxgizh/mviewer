#include "MViewerVersion.h" // M24 version SSOT (generated from CMake project VERSION)
#include "core/project/ProjectSerializer.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <cstdio>
#include <iostream>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace
{
int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)
} // namespace

// M15 Project (.mvproj) round-trip acceptance test.
int main()
{
    mviewer::domain::Project p;
    p.name = "ISP eval";
    p.appVersion = MVIEWER_VERSION_STRING;
    p.createdIso = "2026-07-22T00:00:00Z";
    p.datasetRoots = {"/data/set1", "/data/set2"};
    p.analyzerPipeline = {"histogram", "noise", "psnr"};
    p.reviewNotes = "golden vs current";

    mviewer::domain::Workspace ws;
    ws.rootPath = "/data/set1";
    mviewer::domain::Folder folder;
    folder.path = "/data/set1";
    folder.name = "set1";
    mviewer::domain::ImageMetadata img;
    img.filePath = "/data/set1/a.png";
    img.fileName = "a.png";
    img.roiW = 10;
    img.roiH = 10;
    img.analysis = "peak=0.9";
    img.analysisAnalyzerId = "brightness";
    folder.imageSet.images.push_back(img);
    ws.folders.push_back(folder);
    ws.comparedImages.push_back("/data/set1/a.png");
    ws.compareSessionJson = R"({"activeImage":"/data/set1/a.png"})";
    p.workspace = ws;

    const std::string json = mviewer::core::serializeProject(p);
    mviewer::domain::Project out;
    CHECK(mviewer::core::deserializeProject(json, out), "project JSON deserializes");
    CHECK(out.name == "ISP eval", "project name round-trips");
    CHECK(out.appVersion == MVIEWER_VERSION_STRING, "appVersion must round-trip");
    CHECK(out.datasetRoots.size() == 2, "dataset roots round-trip");
    CHECK(out.analyzerPipeline.size() == 3, "analyzer pipeline size round-trips");
    CHECK(out.analyzerPipeline[0] == "histogram", "analyzer pipeline entries round-trip");
    CHECK(out.workspace.imageCount() == 1, "workspace image count round-trips");
    CHECK(out.workspace.folders.front().imageSet.images.front().analysis == "peak=0.9",
          "workspace analysis round-trips");
    CHECK(out.workspace.folders.front().imageSet.images.front().analysisAnalyzerId == "brightness",
          "workspace analyzer id round-trips");
    CHECK(!out.workspace.compareSessionJson.empty(), "compare session JSON round-trips");

    // Negative cases must fail cleanly (no throw, false return).
    mviewer::domain::Project bad;
    CHECK(!mviewer::core::deserializeProject("{not valid json", bad), "invalid JSON is rejected");
    CHECK(!mviewer::core::deserializeProject(R"({"name":"x"})", bad),
          "JSON without a workspace is rejected");

    std::printf("project round-trip failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
