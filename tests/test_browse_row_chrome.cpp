#include "test_browse_row_chrome.h"

#include "Theme.h"
#include "thumbnailpanel.h"
#include "thumbnailpanel_testaccess.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QPalette>
#include <QRect>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <string>

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

// The delegate fills the whole option.rect, but text, the 48 px thumbnail and
// column content sit inside rect.adjusted(4, 2, -4, -2). Sample the top band of
// the row at several x positions and keep the most common color, so platform
// font metrics or column widths cannot move the probe onto text (a single pixel
// at the right edge passed on Windows but hit content on Linux nightly).
QColor sampleRowBackground(ThumbnailPanel &panel, int row)
{
    const QModelIndex index = panel.model()->index(row, 0);
    const QRect vr = panel.visualRect(index);
    const QImage img = panel.viewport()->grab().toImage();
    const qreal dpr = img.devicePixelRatio();
    const int y = qBound(0, qRound((vr.top() + 1) * dpr), img.height() - 1);
    QHash<QRgb, int> counts;
    constexpr int kSamples = 16;
    for (int i = 1; i < kSamples; ++i)
    {
        const qreal lx = vr.left() + vr.width() * i / qreal(kSamples);
        const int x = qBound(0, qRound(lx * dpr), img.width() - 1);
        ++counts[img.pixel(x, y)];
    }
    QRgb best = 0;
    int bestCount = -1;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    {
        if (it.value() > bestCount)
        {
            best = it.key();
            bestCount = it.value();
        }
    }
    return QColor::fromRgba(best);
}

std::string colorText(const QColor &c)
{
    return c.name(QColor::HexArgb).toStdString();
}

bool colorClose(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) <= 1 && qAbs(a.green() - b.green()) <= 1 &&
           qAbs(a.blue() - b.blue()) <= 1 && qAbs(a.alpha() - b.alpha()) <= 1;
}

// One Details zebra pass in the current theme. Expected colors come from the
// view palette in the Normal (Active) group, which is exactly what
// QListView::paintEvent hands the delegate as option.palette for enabled items.
void runDetailsZebraCase(const QString &dirPath, const std::string &theme)
{
    ThumbnailPanel panel;
    panel.setObjectName(QStringLiteral("thumbnailPanel"));
    panel.resize(900, 500);
    // Keep the window away from the (offscreen) cursor so no row is hovered.
    panel.move(400, 300);
    panel.setViewMode(ThumbnailPanel::Details);
    panel.show();
    pump(50);
    panel.setDirectory(dirPath);
    CHECK(waitFor([&] { return panel.pathList().size() == 3; }, 10000),
          theme << ": Details zebra fixture scanned three files");
    if (panel.pathList().size() != 3)
        return;
    if (panel.selectionModel())
        panel.selectionModel()->clearSelection();
    panel.repaint();
    pump(80);

    const QPalette pal = panel.palette();
    const QColor base = pal.color(QPalette::Active, QPalette::Base);
    const QColor alt = pal.color(QPalette::Active, QPalette::AlternateBase);
    const QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
    CHECK(base != alt, theme << ": Details unselected Base and AlternateBase differ");
    CHECK(base != highlight && alt != highlight,
          theme << ": Details zebra colors are not Highlight");

    const QColor row0 = sampleRowBackground(panel, 0);
    const QColor row1 = sampleRowBackground(panel, 1);
    const QColor row2 = sampleRowBackground(panel, 2);
    CHECK(colorClose(row0, base), theme
                                      << ": unselected even Details row uses Base (v1.0.168 zebra)"
                                      << " got " << colorText(row0) << " want " << colorText(base));
    CHECK(colorClose(row1, alt), theme << ": unselected odd Details row uses AlternateBase"
                                       << " got " << colorText(row1) << " want " << colorText(alt));
    CHECK(colorClose(row2, base), theme << ": third Details row returns to Base"
                                        << " got " << colorText(row2));
    CHECK(row0 != row1, theme << ": unselected Details rows are not a flat fill");

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
    CHECK(colorClose(selected, highlight), theme << ": selected Details row uses Highlight got "
                                                 << colorText(selected) << " want "
                                                 << colorText(highlight));
    CHECK(colorClose(stillOdd, alt),
          theme << ": unselected odd Details row stays AlternateBase got " << colorText(stillOdd));
}
} // namespace

int runBrowseRowChromeTests()
{
    std::cout << "── Browse: Details zebra and context menu ──\n";

    ThumbnailPanel titlesPanel;
    const QStringList titles = ThumbnailPanelTestAccess::browseContextMenuTitles(titlesPanel, {});
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
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for Details zebra exists");
    if (!tmp.isValid())
        return g_failures;
    const QDir dir(tmp.path());
    writePng(dir, QStringLiteral("a.png"), QColor(200, 20, 20));
    writePng(dir, QStringLiteral("b.png"), QColor(20, 200, 20));
    writePng(dir, QStringLiteral("c.png"), QColor(20, 20, 200));

    const mviewer::ui::ThemeMode modes[] = {mviewer::ui::ThemeMode::Light,
                                            mviewer::ui::ThemeMode::Dark};
    for (const auto mode : modes)
    {
        mviewer::ui::Theme::applyTheme(mode);
        const std::string theme = mode == mviewer::ui::ThemeMode::Dark ? "dark" : "light";
        runDetailsZebraCase(tmp.path(), theme);
    }

    mviewer::ui::Theme::applyTheme(previousTheme);
    return g_failures;
}
