// Progressive publication: incremental conversions, middle inserts, type/date/size
// first-screen streaming, generation-before-exists. Timings are printed, not gated.
#include "check.h"
#include "core/image/ImageBuffer.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "gallery_filter_engine.h"
#include "imageviewer.h"
#include "thumbnailpanel.h"
#include "thumbnailpanel_testaccess.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QImage>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <thread>

QString currentPanelPath(const ThumbnailPanel &panel)
{
    const auto index = panel.currentIndex();
    return index.isValid() ? panel.pathList().value(index.row()) : QString();
}

void stubThumbnailDecode()
{
    ThumbnailPipeline::instance().setDecodeFn([](const std::string &, int) { return ImageData{}; });
}

bool writeEntries(const QString &dir, int count)
{
    bool ok = true;
    for (int i = 0; i < count; ++i)
    {
        const int n = count - i;
        const QString suffix = (n % 2 == 0) ? QStringLiteral("png") : QStringLiteral("jpg");
        QFile file(dir + "/image_" + QString::number(n, 10).rightJustified(5, QLatin1Char('0')) +
                   "." + suffix);
        ok = file.open(QIODevice::WriteOnly) && ok;
        const QByteArray payload(16 + (n % 17), 'x');
        ok = (file.write(payload) == payload.size()) && ok;
        const QDateTime stamp = QDateTime::currentDateTime().addSecs(-n);
        file.setFileTime(stamp, QFileDevice::FileModificationTime);
        file.close();
        file.setFileTime(stamp, QFileDevice::FileModificationTime);
    }
    return ok;
}

bool waitComplete(const ThumbnailPanel &panel, int timeoutMs = 60000)
{
    return mviewer::test::waitFor([&] { return ThumbnailPanelTestAccess::scanComplete(panel); },
                                  timeoutMs);
}

bool matchesExpected(const ThumbnailPanel &panel, const QString &typeFilter,
                     ThumbnailPanel::SortMode mode, bool ascending)
{
    mviewer::ui::GalleryFilterSnapshot snapshot;
    snapshot.query.type = typeFilter.toLower().toStdString();
    snapshot.query.sort = static_cast<mviewer::core::BrowseSortField>(mode);
    snapshot.query.ascending = ascending;
    const auto expected = mviewer::ui::evaluateGalleryFilter(panel.entries(), snapshot);
    const auto paths = panel.pathList();
    if (expected.size() != paths.size())
        return false;
    for (int i = 0; i < expected.size(); ++i)
    {
        if (expected.at(i).path != paths.at(i))
            return false;
    }
    return true;
}

void measureDirectory(const QString &dir, int count, ThumbnailPanel::SortMode mode, bool typed,
                      bool expectProgressive)
{
    ThumbnailPanel panel;
    panel.resize(800, 600);
    panel.setSort(mode, true);
    if (typed)
        panel.setTypeFilter(QStringLiteral("png"));
    ThumbnailPanel::resetStdPathConversionCount();
    qint64 firstMs = -1;
    bool firstDuringScan = false;
    QString current;
    int rowShifts = 0;
    QElapsedTimer timer;
    QObject::connect(&panel, &ThumbnailPanel::sequenceChanged, &panel,
                     [&](const QString &, const QStringList &paths)
                     {
                         if (paths.isEmpty())
                             return;
                         if (firstMs < 0)
                         {
                             firstMs = timer.elapsed();
                             firstDuringScan = !ThumbnailPanelTestAccess::scanComplete(panel);
                             current = paths.front();
                             panel.setCurrentIndex(panel.model()->index(0, 0));
                         }
                         else if (!current.isEmpty())
                         {
                             const QString now = currentPanelPath(panel);
                             MVIEWER_CHECK(now == current,
                                           "current path identity survives middle insertions");
                             const int row = panel.pathList().indexOf(current);
                             if (row > 0)
                                 ++rowShifts;
                         }
                     });
    timer.start();
    panel.setDirectory(dir);
    MVIEWER_CHECK(waitComplete(panel), "directory scan completes");
    const auto elapsed = timer.elapsed();
    const auto conversions = ThumbnailPanel::stdPathConversionCount();
    const auto paths = panel.pathList();
    const int expectedCount = typed ? count / 2 : count;
    std::printf("scan count=%d mode=%d typed=%d firstMs=%lld totalMs=%lld progressive=%d "
                "conversions=%llu shifts=%d paths=%d\n",
                count, static_cast<int>(mode), typed ? 1 : 0, static_cast<long long>(firstMs),
                static_cast<long long>(elapsed), firstDuringScan ? 1 : 0,
                static_cast<unsigned long long>(conversions), rowShifts, int(paths.size()));
    MVIEWER_CHECK(int(paths.size()) == expectedCount, "published membership matches filter");
    MVIEWER_CHECK(matchesExpected(panel, typed ? QStringLiteral("png") : QString(), mode, true),
                  "final displayed order matches the complete query");
    if (expectProgressive)
    {
        MVIEWER_CHECK(firstDuringScan,
                      "streamable sort/filter publishes the first screen mid-scan");
        MVIEWER_CHECK(conversions <= static_cast<uint64_t>(2 * count + 32),
                      "path conversions stay proportional to new paths, not full-table rescan");
    }
    else
    {
        MVIEWER_CHECK(!firstDuringScan,
                      "non-streamable sort keeps the full-directory barrier before first publish");
    }
}

void testExistsGeneration(const QString &dir)
{
    ThumbnailPanel panel;
    auto token = ThumbnailPanelTestAccess::scanGenToken(panel);
    std::atomic<int> preExists{0};
    ThumbnailPanel::resetScanExistsCount();
    ThumbnailPanel::setScanPreExistsProbe(
        [&]
        {
            ++preExists;
            token->fetch_add(1, std::memory_order_acq_rel);
        });
    panel.setDirectory(dir);
    MVIEWER_CHECK(mviewer::test::waitFor([&] { return preExists.load() >= 1; }, 15000),
                  "stale scan reaches the pre-exists generation gate");
    MVIEWER_CHECK(
        mviewer::test::waitFor(
            [&] { return preExists.load() >= 1 && ThumbnailPanel::scanExistsCount() == 0; }, 5000),
        "superseded scan does not call dir.exists()");
    ThumbnailPanel::setScanPreExistsProbe({});
    ThumbnailPanel::resetScanExistsCount();
    panel.setDirectory(dir);
    MVIEWER_CHECK(waitComplete(panel), "fresh scan after clearing the probe completes");
    MVIEWER_CHECK(ThumbnailPanel::scanExistsCount() >= 1,
                  "current scan still stats the directory once");
}

void testMiddleInsertPreload(const QStringList &paths)
{
    ImageViewer viewer;
    viewer.resize(1280, 800);
    viewer.show();
    mviewer::test::pump(50);
    const QStringList first16 = paths.mid(0, 16);
    viewer.setBrowseSequence({});
    viewer.setImage(paths[1]);
    viewer.setBrowseSequence(first16);
    MVIEWER_CHECK(
        mviewer::test::waitFor(
            [&] { return viewer.currentPath() == paths[1] && viewer.isLodDisplay(); }, 30000),
        "current image is ready before a middle insertion");
    viewer.resetDisplayRasterRetainStats();
    QStringList grown;
    grown.append(paths[20]); // sorts/places before current when prepended
    grown += first16;
    viewer.setBrowseSequence(grown);
    const auto stats = viewer.displayRasterRetainStats();
    std::printf("middle-insert retain starts=%llu cancels=%llu evictions=%llu inFlight=%llu "
                "index=%d n=%d\n",
                static_cast<unsigned long long>(stats.starts),
                static_cast<unsigned long long>(stats.cancels),
                static_cast<unsigned long long>(stats.evictions),
                static_cast<unsigned long long>(stats.inFlight), viewer.browseIndex(),
                int(viewer.browseSequence().size()));
    MVIEWER_CHECK(stats.evictions == 0, "middle insertion does not evict the warm display cache");
    MVIEWER_CHECK(viewer.currentPath() == paths[1] && viewer.browseIndex() == 2,
                  "current identity shifts row but keeps its path");
    MVIEWER_CHECK(stats.cancels == 0, "still-adjacent neighbor preload is not cancelled");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    stubThumbnailDecode();
    QTemporaryDir tmp;
    MVIEWER_CHECK(tmp.isValid(), "temporary directory");
    const QString dir1k = tmp.filePath(QStringLiteral("k1"));
    const QString dir10k = tmp.filePath(QStringLiteral("k10"));
    MVIEWER_CHECK(QDir().mkpath(dir1k) && QDir().mkpath(dir10k), "fixture directories");
    MVIEWER_CHECK(writeEntries(dir1k, 1000), "1k scan fixtures");
    MVIEWER_CHECK(writeEntries(dir10k, 10000), "10k scan fixtures");

    measureDirectory(dir1k, 1000, ThumbnailPanel::SortName, false, true);
    measureDirectory(dir1k, 1000, ThumbnailPanel::SortName, true, true);
    measureDirectory(dir1k, 1000, ThumbnailPanel::SortDate, false, true);
    measureDirectory(dir1k, 1000, ThumbnailPanel::SortSize, false, true);
    const QString dirRes = tmp.filePath(QStringLiteral("res"));
    MVIEWER_CHECK(QDir().mkpath(dirRes), "resolution fixture directory");
    bool resOk = true;
    constexpr int kResCount = 64;
    for (int i = 0; i < kResCount; ++i)
    {
        const QString path =
            dirRes + "/res_" + QString::number(i, 10).rightJustified(3, QLatin1Char('0')) + ".jpg";
        QImage img(8, 8, QImage::Format_RGB888);
        img.fill(QColor(i, 40, 80));
        resOk = img.save(path, "JPEG", 80) && resOk;
    }
    MVIEWER_CHECK(resOk, "resolution sort uses real tiny JPEGs, not dummy bytes");
    measureDirectory(dirRes, kResCount, ThumbnailPanel::SortResolution, false, false);
    measureDirectory(dir10k, 10000, ThumbnailPanel::SortName, false, true);
    measureDirectory(dir10k, 10000, ThumbnailPanel::SortName, true, true);

    testExistsGeneration(dir1k);

    QTemporaryDir images;
    MVIEWER_CHECK(images.isValid(), "preload fixtures");
    QStringList preloadPaths;
    for (int i = 0; i < 24; ++i)
    {
        const QString path =
            images.filePath(QStringLiteral("p%1.jpg").arg(i, 3, 10, QLatin1Char('0')));
        QImage img(i < 3 ? 2000 : 16, i < 3 ? 2001 : 16, QImage::Format_RGB888);
        img.fill(QColor(40 + i, 80, 120));
        MVIEWER_CHECK(img.save(path, "JPEG", 80), "preload jpeg");
        preloadPaths.append(path);
    }
    testMiddleInsertPreload(preloadPaths);

    std::fflush(stdout);
    return mviewer::test::failures() ? 1 : 0;
}
