#include "test_browse_row_chrome.h"

#include "Theme.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QRect>
#include <QTemporaryDir>
#include <functional>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs)
        pump(10);
    return pred();
}

QString writePng(const QDir &dir, const QString &name, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(48, 32, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

QColor sampleRowBackground(ThumbnailPanel &panel, int row)
{
    const QModelIndex index = panel.model()->index(row, 0);
    const QRect vr = panel.visualRect(index);
    const QImage img = panel.viewport()->grab().toImage();
    const qreal dpr = img.devicePixelRatio();
    const int x = qBound(0, qRound((vr.right() - 8) * dpr), img.width() - 1);
    const int y = qBound(0, qRound(vr.center().y() * dpr), img.height() - 1);
    return img.pixelColor(x, y);
}

bool colorClose(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) <= 1 && qAbs(a.green() - b.green()) <= 1 &&
           qAbs(a.blue() - b.blue()) <= 1 && qAbs(a.alpha() - b.alpha()) <= 1;
}
} // namespace

int runBrowseRowChromeTests()
{
    std::cout << "── Browse: Details zebra and context menu ──\n";

    ThumbnailPanel titlesPanel;
    const QStringList titles = titlesPanel.browseContextMenuTitlesForTest({});
    const auto hasTitle = [&](const char *source)
    { return titles.contains(ThumbnailPanel::tr(source)); };
    CHECK(hasTitle("Open") && hasTitle("Rename") && hasTitle("Copy"),
          "browse context menu keeps Open, Rename, and Copy");
    CHECK(hasTitle("Move to Recycle Bin") && hasTitle("Compare") &&
              hasTitle("Batch Analysis Export"),
          "browse context menu keeps file, compare, and analyze actions");
    CHECK(!hasTitle("Set rating") && !hasTitle("Clear Rating"),
          "browse context menu has no Set rating entries");
    CHECK(!hasTitle("Set color label") && !hasTitle("No label"),
          "browse context menu has no Set color label entries");
    CHECK(!hasTitle("Set Mark") && !hasTitle("Mark as Pick (Pick)") && !hasTitle("Clear Markers"),
          "browse context menu has no Set Mark entries");
    CHECK(!hasTitle("Add Tag…") && !hasTitle("Remove Tag from Selection"),
          "browse context menu has no Add/Remove Tag entries");

    const auto previousTheme = mviewer::ui::Theme::currentTheme();
    mviewer::ui::Theme::applyTheme(mviewer::ui::ThemeMode::Light);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for Details zebra exists");
    if (!tmp.isValid())
        return g_failures;
    const QDir dir(tmp.path());
    writePng(dir, QStringLiteral("a.png"), QColor(200, 20, 20));
    writePng(dir, QStringLiteral("b.png"), QColor(20, 200, 20));
    writePng(dir, QStringLiteral("c.png"), QColor(20, 20, 200));

    ThumbnailPanel panel;
    panel.setObjectName(QStringLiteral("thumbnailPanel"));
    panel.resize(900, 500);
    panel.setViewMode(ThumbnailPanel::Details);
    panel.show();
    pump(50);
    panel.setDirectory(tmp.path());
    CHECK(waitFor([&] { return panel.pathList().size() == 3; }, 10000),
          "Details zebra fixture scanned three files");
    if (panel.pathList().size() != 3)
        return g_failures;
    if (panel.selectionModel())
        panel.selectionModel()->clearSelection();
    panel.repaint();
    pump(80);

    const QColor base = panel.palette().color(QPalette::Active, QPalette::Base);
    const QColor alt = panel.palette().color(QPalette::Active, QPalette::AlternateBase);
    const QColor highlight = panel.palette().color(QPalette::Active, QPalette::Highlight);
    CHECK(base != alt, "Details unselected Base and AlternateBase differ");
    CHECK(base != highlight && alt != highlight, "Details zebra colors are not Highlight");

    const QColor row0 = sampleRowBackground(panel, 0);
    const QColor row1 = sampleRowBackground(panel, 1);
    CHECK(colorClose(row0, base), "unselected even Details row uses Base (v1.0.168 zebra)");
    CHECK(colorClose(row1, alt), "unselected odd Details row uses AlternateBase (v1.0.168 zebra)");
    CHECK(row0 != row1, "unselected Details rows are not a flat fill");

    if (panel.selectionModel())
    {
        const QModelIndex first = panel.model()->index(0, 0);
        panel.selectionModel()->select(first, QItemSelectionModel::ClearAndSelect);
        panel.selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    }
    panel.repaint();
    pump(40);
    const QColor selected = sampleRowBackground(panel, 0);
    const QColor stillOdd = sampleRowBackground(panel, 1);
    CHECK(colorClose(selected, highlight), "selected Details row uses Highlight");
    CHECK(colorClose(stillOdd, alt), "unselected odd Details row stays AlternateBase");

    mviewer::ui::Theme::applyTheme(previousTheme);
    return g_failures;
}
