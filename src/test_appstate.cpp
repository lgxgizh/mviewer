// Headless unit test for the P0 app-state persistence layer (no display needed).
// Verifies: AppState favorites + lastImage + lastThumbScroll round-trip via JSON,
// and core::RecentFiles serialize/deserialize round-trip. Run offscreen.
#include "appstate.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Redirect AppConfigLocation into a temp dir so the test is hermetic and
    // does not clobber the developer's real mviewer.json.
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());
    qputenv("APPDATA", tmp.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);

    // ---- AppState round-trip ----
    {
        AppState a;
        a.addFavorite("C:/imgs/a");
        a.addFavorite("C:/imgs/b");
        a.addFavorite("C:/imgs/a"); // dedupe
        a.lastDir = "C:/imgs/a";
        a.lastImage = "C:/imgs/a/001.jpg";
        a.lastThumbScroll = 432;
        CHECK(a.favorites.size() == 2, "favorites dedupe to 2");
        CHECK(a.save(), "AppState::save succeeds");

        AppState b = AppState::load();
        CHECK(b.favorites.size() == 2, "favorites reload count == 2");
        CHECK(b.lastDir == "C:/imgs/a", "lastDir round-trips");
        CHECK(b.lastImage == "C:/imgs/a/001.jpg", "lastImage round-trips");
        CHECK(b.lastThumbScroll == 432, "lastThumbScroll round-trips");
        CHECK(b.isFavorite("C:/imgs/b"), "isFavorite true after reload");
    }

    // ---- RecentFiles round-trip (core) ----
    {
        mviewer::core::RecentFiles rf(5);
        rf.add("C:/r/1");
        rf.add("C:/r/2");
        rf.add("C:/r/1"); // dedupe (moved to front)
        const std::string json = rf.serialize();
        mviewer::core::RecentFiles rf2(5);
        CHECK(rf2.deserialize(json), "RecentFiles::deserialize succeeds");
        const auto items = rf2.items();
        CHECK(items.size() == 2, "RecentFiles reload count == 2");
        CHECK(items.front() == "C:/r/1", "most-recent (deduped) is at front");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "HAS FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
