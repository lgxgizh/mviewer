// M19: unit tests for the new UI models (Selection, Directory, ImageList,
// Workspace, Analyzer). These models are Qt-free in header (only QObjects) and
// are safe to test headless.
#include "analyzermodel.h"
#include "directorymodel.h"
#include "imagelistmodel.h"
#include "selectionmodel.h"
#include "workspacemodel.h"

#include <QDebug>
#include <QStringList>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL: %s\n", msg);                                                        \
            ++g_failures;                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::printf("PASS: %s\n", msg);                                                        \
        }                                                                                          \
    } while (0)

int main(int, char **)
{
    // ---- SelectionModel ----
    {
        SelectionModel sel;
        CHECK(sel.currentImage().isEmpty(), "SelectionModel starts empty");
        sel.setCurrentImage("a.jpg");
        CHECK(sel.currentImage() == "a.jpg", "SelectionModel currentImage set");
        CHECK(sel.selection().size() == 1, "SelectionModel selection size 1");
        sel.setSelection({"a.jpg", "b.jpg"}, "b.jpg");
        CHECK(sel.currentImage() == "b.jpg", "SelectionModel multi-select current");
        CHECK(sel.selection().size() == 2, "SelectionModel multi-select size 2");
        // Moving current within multi-select must NOT collapse it.
        sel.setCurrentImage("a.jpg");
        CHECK(sel.currentImage() == "a.jpg", "SelectionModel multi current move");
        CHECK(sel.selection().size() == 2, "SelectionModel multi preserved on setCurrent");
        // Outside multi → collapses to single.
        sel.setCurrentImage("c.jpg");
        CHECK(sel.selection().size() == 1, "SelectionModel collapses when leaving multi");
        CHECK(sel.currentImage() == "c.jpg", "SelectionModel new single current");
        sel.clear();
        CHECK(sel.isEmpty(), "SelectionModel cleared");
    }

    // ---- DirectoryModel ----
    {
        DirectoryModel dir;
        dir.setFavorites({"/a", "/b"});
        CHECK(dir.favorites().size() == 2, "DirectoryModel favorites set");
        dir.addFavorite("/c");
        CHECK(dir.favorites().size() == 3, "DirectoryModel addFavorite");
        dir.removeFavorite("/a");
        CHECK(dir.favorites().size() == 2, "DirectoryModel removeFavorite");
        dir.addRecentFolder("/r1");
        dir.addRecentFolder("/r2");
        CHECK(dir.recentFolders().size() == 2, "DirectoryModel recent size");
        // duplicate -> move to front
        dir.addRecentFolder("/r1");
        CHECK(dir.recentFolders().front() == "/r1", "DirectoryModel recent LRU dedupe");
        dir.setCurrentDirectory("/work");
        CHECK(dir.currentDirectory() == "/work", "DirectoryModel current");
    }

    // ---- ImageListModel ----
    {
        ImageListModel list;
        CHECK(list.isDirty(), "ImageListModel starts dirty");
        list.setPaths({"a.jpg", "b.jpg"}, "/dir");
        CHECK(!list.isDirty(), "ImageListModel clean after setPaths");
        CHECK(list.count() == 2, "ImageListModel count");
        CHECK(list.indexOf("b.jpg") == 1, "ImageListModel indexOf");
        CHECK(list.pathAt(0) == "a.jpg", "ImageListModel pathAt");
        list.markDirty();
        CHECK(list.isDirty(), "ImageListModel markDirty");
        list.removePaths({"a.jpg"});
        CHECK(list.count() == 1, "ImageListModel removePaths");
    }

    // ---- WorkspaceModel ----
    {
        WorkspaceModel ws;
        ws.setRootPath("/root");
        CHECK(ws.rootPath() == "/root", "WorkspaceModel rootPath");
        ws.setComparedImages({"a.jpg", "b.jpg"});
        CHECK(ws.comparedImages().size() == 2, "WorkspaceModel comparedImages");
        ws.setAnalysisVisible(true);
        ws.setAnalysisPage(3);
        CHECK(ws.analysisPage() == 3, "WorkspaceModel analysisPage");
    }

    // ---- AnalyzerModel ----
    {
        AnalyzerModel am;
        am.setResult("a.jpg", "hist: 1,2,3");
        CHECK(am.resultText("a.jpg") == "hist: 1,2,3", "AnalyzerModel setResult");
        CHECK(am.history().contains("a.jpg"), "AnalyzerModel history updated");
        am.pinResult("a.jpg");
        CHECK(am.isPinned("a.jpg"), "AnalyzerModel pin");
        am.unpinResult("a.jpg");
        CHECK(!am.isPinned("a.jpg"), "AnalyzerModel unpin");
        am.setCurrentAnalyzer("histogram");
        CHECK(am.currentAnalyzerId() == "histogram", "AnalyzerModel currentAnalyzer");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "HAS FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
