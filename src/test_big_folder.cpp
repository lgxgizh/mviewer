// Big folders: path -> row lookups are hash based (no cleanPath per row per
// lookup), and a progressive scan publishes rows already in the final
// natural-name order so the gallery does not jump when the scan completes.
// NOLINTBEGIN
#include "imagelistmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (c)                                                                                     \
            ++g_pass;                                                                              \
        else                                                                                       \
        {                                                                                          \
            ++g_fail;                                                                              \
            printf("FAIL: %s\n", m);                                                               \
        }                                                                                          \
    } while (0)

static bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return done();
}

static void testFiftyThousandNavigation()
{
    printf("\n[50k-path navigation lookups]\n");
    constexpr int kCount = 50000;
    QStringList paths;
    paths.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        paths.append(QStringLiteral("/photos/2026/trip/IMG_%1.jpg").arg(i));
    ImageListModel list;
    list.setPaths(paths, QStringLiteral("/photos/2026/trip"));

    QElapsedTimer t;
    t.start();
    bool allFound = true;
    for (int i = 0; i < 1000; ++i)
    {
        const int want = (i * 49) % kCount;
        allFound = allFound && list.indexOf(paths.at(want)) == want;
    }
    const qint64 ms = t.elapsed();
    printf("  1000 lookups over %d paths: %lld ms\n", kCount, ms);
    CHECK(allFound, "every lookup returns its row");
    CHECK(ms < 2000, "1000 lookups stay far below the old linear-scan cost");
    CHECK(list.indexOf(QStringLiteral("/photos/2026//trip/./IMG_7.jpg")) == 7,
          "non-normalized spelling still matches");
    CHECK(list.indexOf(QStringLiteral("/photos/none.jpg")) == -1, "missing path is -1");

    // Simulated progressive scan: setPaths per 128-entry batch, one lookup each.
    t.restart();
    QStringList growing;
    int lastRow = -1;
    for (int i = 0; i < kCount; ++i)
    {
        growing.append(paths.at(i));
        if (growing.size() % 128 == 0 || i == kCount - 1)
        {
            list.setPaths(growing, QStringLiteral("/photos/2026/trip"));
            lastRow = list.indexOf(paths.at(growing.size() / 2)) == growing.size() / 2 ? 0 : -2;
        }
    }
    printf("  progressive 128-batches + lookup: %lld ms\n", t.elapsed());
    CHECK(lastRow == 0, "lookup during progressive growth");

    QStringList dup = {QStringLiteral("/a/x.jpg"), QStringLiteral("/a/y.jpg"),
                       QStringLiteral("/a/./x.jpg")};
    list.setPaths(dup, QStringLiteral("/a"));
    CHECK(list.indexOf(QStringLiteral("/a/x.jpg")) == 0, "equivalent duplicates -> first row");
    list.removePaths({QStringLiteral("/a//x.jpg")});
    CHECK(list.count() == 1 && list.pathAt(0) == QStringLiteral("/a/y.jpg"),
          "removePaths drops every equivalent spelling");
}

static void testProgressiveScanOrder()
{
    printf("\n[progressive scan publishes final order]\n");
    QTemporaryDir dir;
    constexpr int kFiles = 2000;
    for (int i = 1; i <= kFiles; ++i)
    {
        // Unpadded numbers: directory/lexicographic order differs from the
        // natural order the finished scan sorts by.
        QFile f(QDir(dir.path()).filePath(QStringLiteral("img%1.jpg").arg(i)));
        f.open(QIODevice::WriteOnly);
    }
    ThumbnailPanel panel;
    panel.resize(600, 400);
    panel.show();
    int emissions = 0;
    int outOfOrder = 0;
    QStringList last;
    QObject::connect(&panel, &ThumbnailPanel::sequenceChanged, &panel,
                     [&](const QString &, const QStringList &paths)
                     {
                         ++emissions;
                         // Every published list must be ordered like the final one.
                         for (int i = 1; i < paths.size(); ++i)
                         {
                             const int a = QFileInfo(paths.at(i - 1)).baseName().mid(3).toInt();
                             const int b = QFileInfo(paths.at(i)).baseName().mid(3).toInt();
                             if (a >= b)
                                 ++outOfOrder;
                         }
                         last = paths;
                     });
    panel.setDirectory(dir.path());
    CHECK(waitUntil([&] { return last.size() == kFiles; }, 60000), "scan completed");
    QApplication::processEvents();
    printf("  %d emissions, %d out-of-order neighbours\n", emissions, outOfOrder);
    CHECK(outOfOrder == 0, "progressive rows already in natural order (no jump at the end)");
    CHECK(last.first().endsWith(QStringLiteral("/img1.jpg")) &&
              last.last().endsWith(QStringLiteral("/img2000.jpg")),
          "final order is natural");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testFiftyThousandNavigation();
    testProgressiveScanOrder();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
// NOLINTEND
