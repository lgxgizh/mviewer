#include "core/project/ProjectSerializer.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <cassert>
#include <iostream>

// M15 Project (.mvproj) round-trip acceptance test.
int main()
{
    mviewer::domain::Project p;
    p.name = "ISP eval";
    p.appVersion = "1.0.0";
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
    folder.imageSet.images.push_back(img);
    ws.folders.push_back(folder);
    ws.comparedImages.push_back("/data/set1/a.png");
    ws.compareSessionJson = R"({"activeImage":"/data/set1/a.png"})";
    p.workspace = ws;

    const std::string json = mviewer::core::serializeProject(p);
    mviewer::domain::Project out;
    assert(mviewer::core::deserializeProject(json, out));
    assert(out.name == "ISP eval");
    assert(out.datasetRoots.size() == 2);
    assert(out.analyzerPipeline.size() == 3);
    assert(out.analyzerPipeline[0] == "histogram");
    assert(out.workspace.imageCount() == 1);
    assert(out.workspace.folders.front().imageSet.images.front().analysis == "peak=0.9");
    assert(!out.workspace.compareSessionJson.empty());

    // Negative cases must fail cleanly (no throw, false return).
    mviewer::domain::Project bad;
    assert(!mviewer::core::deserializeProject("{not valid json", bad));
    assert(!mviewer::core::deserializeProject(R"({"name":"x"})", bad)); // no workspace

    std::cout << "project round-trip OK\n";
    return 0;
}
