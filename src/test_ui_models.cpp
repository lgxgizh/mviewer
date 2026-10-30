#include "Theme.h"
#include "ThemeIcons.h"
#include "ThemeMetrics.h"
#include "analyzermodel.h"
#include "breadcrumbbar.h"
#include "directorymodel.h"
#include "hoverfade.h"
#include "imagelistmodel.h"
#include "naturalnameorder.h"
#include "selectionmodel.h"
#include "workspacemodel.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QStringListModel>
#include <QStyle>
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

    // ---- Compare default order: natural, case-insensitive file-name sort ----
    {
        CHECK(compareNaturalName(u"img2.jpg", u"img10.jpg") < 0, "natural: img2 before img10");
        CHECK(compareNaturalName(u"IMG_3.jpg", u"img_20.jpg") < 0,
              "natural: case-insensitive text chunks");
        CHECK(compareNaturalName(u"a.jpg", u"A.jpg") == 0,
              "natural: case-only names compare equal");
        const QStringList picked = {"/d/img10.jpg", "/d/IMG2.jpg", "/d/img1.jpg", "/e/b.jpg",
                                    "/d/a.jpg"};
        const QStringList sorted = sortPathsByNaturalFileName(picked);
        const QStringList expected = {"/d/a.jpg", "/e/b.jpg", "/d/img1.jpg", "/d/IMG2.jpg",
                                      "/d/img10.jpg"};
        CHECK(sorted == expected, "compare order: selection sorted by natural file name");
        const QStringList dupes = sortPathsByNaturalFileName({"/z/x.jpg", "/a/x.jpg"});
        CHECK(dupes == QStringList({"/a/x.jpg", "/z/x.jpg"}),
              "compare order: same file name tie-breaks on full path");
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
        const mviewer::ui::ThemeTokens darkTokens = mviewer::ui::makeThemeTokens(true);
        const mviewer::ui::ThemeTokens lightTokens = mviewer::ui::makeThemeTokens(false);
        QString darkSheet = qApp->styleSheet();
        CHECK(!darkSheet.isEmpty(), "dark application stylesheet is set");
        CHECK(darkSheet.contains(darkTokens.accent), "dark stylesheet contains the accent");
        CHECK(darkSheet.contains(QStringLiteral("#0a84ff")), "dark accent is Apple system blue");
        CHECK(darkSheet.contains(darkTokens.bg0), "dark stylesheet uses the dark background");
        CHECK(darkSheet.contains(darkTokens.separator), "dark stylesheet uses the separator");
        CHECK(!darkSheet.contains(QStringLiteral("{{")),
              "dark stylesheet has no unresolved placeholders");

        mviewer::ui::ThemeTokens systemDark =
            mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::System, true);
        mviewer::ui::ThemeTokens systemLight =
            mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::System, false);
        CHECK(mviewer::ui::Theme::tokensFor(mviewer::ui::ThemeMode::Dark, false).bg0 ==
                  darkTokens.bg0,
              "dark background token");
        CHECK(systemDark.bg0 == darkTokens.bg0, "System on a dark desktop uses dark tokens");
        CHECK(systemLight.bg0 != darkTokens.bg0, "System on a light desktop swaps tokens");
        CHECK(systemLight.accent == lightTokens.accent,
              "System on a light desktop uses light accent");
        CHECK(lightTokens.accent == QStringLiteral("#007aff"), "light accent is Apple system blue");
        CHECK(darkTokens.onAccent == QStringLiteral("#ffffff") &&
                  lightTokens.onAccent == darkTokens.onAccent,
              "on-accent stays literal white in both themes");
        QString lightSheet = mviewer::ui::Theme::buildStyleSheet(systemLight);
        CHECK(!lightSheet.isEmpty() && !lightSheet.contains(QStringLiteral("{{")),
              "light stylesheet has no unresolved placeholders");
        CHECK(lightSheet.contains(QStringLiteral("#007aff")),
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
        CHECK(systemSheet.contains(resolved.accent), "system stylesheet contains the accent");
        CHECK(systemSheet.contains(resolved.bg0), "system stylesheet follows the desktop scheme");
        CHECK(!mviewer::ui::Theme::themeModeName(mviewer::ui::ThemeMode::Dark).isEmpty(),
              "ThemeModeName not empty for Dark");
        CHECK(!mviewer::ui::Theme::themeModeName(mviewer::ui::ThemeMode::System).isEmpty(),
              "ThemeModeName not empty for System");

        CHECK(mviewer::ui::toolbarIconIds().size() == 13, "toolbar icon catalog has 13 ids");
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

    // ---- Light theme ----
    {
        using mviewer::ui::Theme;
        using mviewer::ui::ThemeMode;
        CHECK(Theme::themeModeFromKey(QStringLiteral("light")) == ThemeMode::Light,
              "light theme key parses");
        CHECK(Theme::themeModeFromKey(Theme::themeModeKey(ThemeMode::System)) == ThemeMode::System,
              "system theme key round-trips");
        CHECK(Theme::themeModeFromKey(QStringLiteral("bogus")) == ThemeMode::Dark,
              "unknown theme key falls back to dark");
        CHECK(!Theme::themeModeName(ThemeMode::Light).isEmpty(),
              "ThemeModeName not empty for Light");
        const mviewer::ui::ThemeTokens light = Theme::tokensFor(ThemeMode::Light, true);
        const mviewer::ui::ThemeTokens dark = Theme::tokensFor(ThemeMode::Dark, false);
        CHECK(light.bg0 == mviewer::ui::makeThemeTokens(false).bg0,
              "Light ignores a dark desktop and uses light tokens");

        Theme::applyTheme(ThemeMode::Dark);
        QLabel probe;
        probe.setStyleSheet(QStringLiteral("QLabel{background:%1;color:#ffffff;}")
                                .arg(Theme::themeColor(mviewer::ui::ThemeRole::Bg1)));
        probe.setTextFormat(Qt::RichText);
        probe.setText(QStringLiteral("<span style='color:%1'>x</span>")
                          .arg(Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
        Theme::applyTheme(ThemeMode::Light);
        CHECK(Theme::currentTheme() == ThemeMode::Light, "Theme switched to Light");
        CHECK(qApp->styleSheet().contains(light.bg0), "light stylesheet uses the light background");
        CHECK(probe.styleSheet().contains(light.bg1) && !probe.styleSheet().contains(dark.bg1),
              "live switch retints token colors in widget stylesheets");
        CHECK(probe.styleSheet().contains(QStringLiteral("#ffffff")),
              "live switch keeps literal white untouched");
        CHECK(probe.text().contains(light.textSecondary), "live switch retints rich-text labels");
        CHECK(!mviewer::ui::toolbarIconPixmap("open").isNull(),
              "toolbar icon renders in light theme");
        CHECK(QSettings().value(QStringLiteral("uiTheme")).toString() == QStringLiteral("light"),
              "light theme choice is persisted");
        Theme::applyTheme(ThemeMode::Dark);
        CHECK(probe.styleSheet().contains(dark.bg1), "switching back restores dark token colors");
    }

    // ---- Dialog polish + hover fade ----
    {
        QDialog dialog;
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        dialog.show();
        QApplication::processEvents();
        CHECK(box->button(QDialogButtonBox::Ok)->property("primary").toBool(),
              "dialog accept button is marked primary");
        CHECK(!box->button(QDialogButtonBox::Cancel)->property("primary").toBool(),
              "dialog reject button is not primary");
        dialog.hide();

        QListView view;
        QStringListModel fadeModel(QStringList{QStringLiteral("a"), QStringLiteral("b")});
        view.setModel(&fadeModel);
        CHECK(mviewer::ui::HoverFade::forView(&view) == nullptr, "no hover fade by default");
        auto *fade = new mviewer::ui::HoverFade(&view);
        CHECK(mviewer::ui::HoverFade::forView(&view) == fade, "hover fade found on its view");
        CHECK(fade->progress(fadeModel.index(0)) == 0.0, "unhovered item has no hover progress");
        CHECK(fade->progress(QModelIndex()) == 0.0, "invalid index has no hover progress");

        QWidget child(&dialog);
        CHECK(QApplication::style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) ==
                  mviewer::ui::metrics::kLayoutSpacing,
              "app style uses the spacing scale between layout items");
        CHECK(QApplication::style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, &dialog) ==
                  mviewer::ui::metrics::kWindowMargin,
              "dialogs get the window margin from the spacing scale");
        CHECK(QApplication::style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, &child) ==
                  mviewer::ui::metrics::kChildMargin,
              "child widgets get the child margin from the spacing scale");
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
