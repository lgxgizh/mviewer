#include "Theme.h"
#include "ThemeIcons.h"
#include "analyzermodel.h"
#include "breadcrumbbar.h"
#include "directorymodel.h"
#include "imagelistmodel.h"
#include "selectionmodel.h"
#include "workspacemodel.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QSettings>
#include <QStringList>
#include <cstdio>
#include <memory>

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
    std::unique_ptr<QApplication> app;
    if (!QApplication::instance())
        app = std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("MViewer"));
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

    // ---- SelectionModel P0-2: focused / hovered / compared ----
    {
        SelectionModel sel;
        sel.setCompared({"a.jpg", "b.jpg", "c.jpg"});
        CHECK(sel.compared().size() == 3, "SelectionModel compared set");
        CHECK(sel.compared().at(1) == "b.jpg", "SelectionModel compared order");
        sel.setFocused("b.jpg");
        CHECK(sel.focused() == "b.jpg", "SelectionModel focused set");
        sel.setFocused(QString());
        CHECK(sel.focused().isEmpty(), "SelectionModel focused cleared");
        sel.setHovered("c.jpg");
        CHECK(sel.hovered() == "c.jpg", "SelectionModel hovered set");
        // clear() must also reset focus + compared, but not touch transient hover.
        sel.setCompared({"x.jpg"});
        sel.clear();
        CHECK(sel.compared().isEmpty(), "SelectionModel compared cleared on clear");
        CHECK(sel.focused().isEmpty(), "SelectionModel focused cleared on clear");
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
        int recentChanges = 0;
        QObject::connect(&dir, &DirectoryModel::recentFoldersChanged,
                         [&](const QStringList &) { ++recentChanges; });
        dir.removeRecentFolder(QStringLiteral("\\r1"));
        CHECK(dir.recentFolders().size() == 1,
              "DirectoryModel removeRecentFolder matches the other slash");
        CHECK(dir.recentFolders().front() == "/r2",
              "DirectoryModel removeRecentFolder kept remaining");
        CHECK(recentChanges == 1, "DirectoryModel removeRecentFolder emits change signal");
        dir.removeRecentFolder("/nonexistent");
        CHECK(recentChanges == 1, "DirectoryModel removeRecentFolder no-op does not emit signal");
        dir.setCurrentDirectory("/work");
        CHECK(dir.currentDirectory() == "/work", "DirectoryModel current");
        dir.addFavorite(QStringLiteral("C:/photos"));
        dir.addFavorite(QStringLiteral("C:\\photos"));
        CHECK(dir.favorites().size() == 3, "DirectoryModel favorite ignores slashes");
        CHECK(dir.hasFavorite(QStringLiteral("C:\\photos")),
              "DirectoryModel hasFavorite matches the other slash");
        dir.removeFavorite(QStringLiteral("C:/photos"));
        CHECK(!dir.hasFavorite(QStringLiteral("C:\\photos")),
              "DirectoryModel removeFavorite matches the other slash");
        int directoryChanges = 0;
        QObject::connect(&dir, &DirectoryModel::currentDirectoryChanged,
                         [&](const QString &) { ++directoryChanges; });
        dir.setCurrentDirectory(QStringLiteral("C:\\Photos"));
        dir.setCurrentDirectory(QStringLiteral("C:/Photos"));
        CHECK(dir.currentDirectory() == QStringLiteral("C:/Photos"),
              "DirectoryModel stores a forward-slash folder");
        CHECK(directoryChanges == 1, "slash-only folder change does not navigate again");
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
        list.setPaths({"C:/photos/a.jpg", "C:/photos/b.jpg"}, "/dir");
        CHECK(list.indexOf("C:\\photos\\b.jpg") == 1, "ImageListModel indexOf ignores slashes");
        list.removePaths({"C:\\photos\\a.jpg"});
        CHECK(list.count() == 1, "ImageListModel removePaths ignores slashes");
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

    // ---- Theme ----
    {
        {
            QSettings settings;
            settings.remove(QStringLiteral("uiTheme"));
            settings.sync();
        }
        mviewer::ui::Theme::initTheme();
        CHECK(mviewer::ui::Theme::currentTheme() == mviewer::ui::ThemeMode::Dark,
              "Default theme is Dark");
        QString darkSheet = qApp->styleSheet();
        CHECK(!darkSheet.isEmpty(), "dark application stylesheet is set");
        CHECK(darkSheet.contains(QStringLiteral("#4c8dff")), "dark stylesheet contains the accent");
        CHECK(darkSheet.contains(QStringLiteral("#1e1e1e")),
              "dark stylesheet uses the dark background");
        CHECK(!darkSheet.contains(QStringLiteral("{{")),
              "dark stylesheet has no unresolved placeholders");

        mviewer::ui::ThemeTokens darkTokens =
            mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::Dark, false);
        mviewer::ui::ThemeTokens systemDark =
            mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::System, true);
        mviewer::ui::ThemeTokens systemLight =
            mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::System, false);
        CHECK(darkTokens.bg0 == QStringLiteral("#1e1e1e"), "dark background token");
        CHECK(systemDark.bg0 == darkTokens.bg0, "System on a dark desktop uses dark tokens");
        CHECK(systemLight.bg0 != darkTokens.bg0, "System on a light desktop swaps tokens");
        CHECK(systemLight.accent == QStringLiteral("#4c8dff"), "light tokens keep the accent");
        QString lightSheet = mviewer::ui::Theme::buildStyleSheet(systemLight);
        CHECK(!lightSheet.isEmpty() && !lightSheet.contains(QStringLiteral("{{")),
              "light stylesheet has no unresolved placeholders");
        CHECK(lightSheet.contains(QStringLiteral("#4c8dff")),
              "light stylesheet contains the accent");

        mviewer::ui::Theme::applyTheme(mviewer::ui::ThemeMode::System);
        CHECK(mviewer::ui::Theme::currentTheme() == mviewer::ui::ThemeMode::System,
              "Theme switched to System");
        QString systemSheet = qApp->styleSheet();
        mviewer::ui::ThemeTokens resolved = mviewer::ui::Theme::tokensFor(
            mviewer::ui::ThemeMode::System, mviewer::ui::Theme::systemPrefersDark());
        CHECK(!systemSheet.isEmpty(), "system application stylesheet is set");
        CHECK(!systemSheet.contains(QStringLiteral("{{")),
              "system stylesheet has no unresolved placeholders");
        CHECK(systemSheet.contains(QStringLiteral("#4c8dff")),
              "system stylesheet contains the accent");
        CHECK(systemSheet.contains(resolved.bg0), "system stylesheet follows the desktop scheme");
        CHECK(!mviewer::ui::Theme::themeModeName(mviewer::ui::ThemeMode::Dark).isEmpty(),
              "ThemeModeName not empty for Dark");
        CHECK(!mviewer::ui::Theme::themeModeName(mviewer::ui::ThemeMode::System).isEmpty(),
              "ThemeModeName not empty for System");

        CHECK(mviewer::ui::toolbarIconIds().size() == 12, "toolbar icon catalog has 12 ids");
        for (const QString &id : mviewer::ui::toolbarIconIds())
        {
            QByteArray bytes = id.toUtf8();
            QPixmap pixmap = mviewer::ui::toolbarIconPixmap(bytes.constData());
            QImage image = pixmap.toImage();
            int ink = 0;
            for (int y = 0; y < image.height(); ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    if (qAlpha(image.pixel(x, y)) > 0)
                        ++ink;
                }
            }
            if (pixmap.isNull() || ink == 0)
                std::printf("FAIL icon: %s ink=%d\n", bytes.constData(), ink);
            CHECK(!pixmap.isNull() && ink > 0, "toolbar icon pixmap is visible");
        }
        CHECK(mviewer::ui::toolbarIcon("missing-toolbar-icon").isNull(),
              "unknown toolbar icon id is empty");

        mviewer::ui::Theme::applyTheme(mviewer::ui::ThemeMode::Dark);
        CHECK(mviewer::ui::Theme::currentTheme() == mviewer::ui::ThemeMode::Dark,
              "Theme switched back to Dark");
    }

    // ---- BreadcrumbBar ----
    {
        BreadcrumbBar bar;
        bar.setPath("C:/Users/Box/Photos");
        CHECK(bar.currentPath() == "C:/Users/Box/Photos", "BreadcrumbBar currentPath matches");
        QStringList segs = bar.segments();
        CHECK(segs.size() == 4, "BreadcrumbBar has 4 segments for C:/Users/Box/Photos");
        CHECK(bar.pathForIndex(0) == "C:/", "BreadcrumbBar drive segment maps to drive root C:/");
        CHECK(bar.pathForIndex(1) == "C:/Users", "BreadcrumbBar index 1 maps to C:/Users");
        CHECK(bar.pathForIndex(2) == "C:/Users/Box", "BreadcrumbBar index 2 maps to C:/Users/Box");
        CHECK(bar.pathForIndex(3) == "C:/Users/Box/Photos",
              "BreadcrumbBar index 3 maps to full path");

        // Drive root alone
        bar.setPath("D:/");
        CHECK(bar.segments().size() == 1, "BreadcrumbBar single segment for D:/");
        CHECK(bar.pathForIndex(0) == "D:/", "BreadcrumbBar single drive root gives D:/");

        // Unix path
        bar.setPath("/home/user/pictures");
        CHECK(bar.pathForIndex(0) == "/", "BreadcrumbBar Unix root gives /");
        CHECK(bar.pathForIndex(1) == "/home", "BreadcrumbBar Unix index 1 gives /home");
        CHECK(bar.pathForIndex(2) == "/home/user", "BreadcrumbBar Unix index 2 gives /home/user");
        CHECK(bar.pathForIndex(3) == "/home/user/pictures",
              "BreadcrumbBar Unix index 3 gives /home/user/pictures");

        // Path selection signal
        QString selectedPath;
        QObject::connect(&bar, &BreadcrumbBar::pathSelected,
                         [&selectedPath](const QString &p) { selectedPath = p; });
        emit bar.pathSelected(bar.pathForIndex(1));
        CHECK(selectedPath == "/home", "BreadcrumbBar signal emitted with segment path");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "HAS FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
