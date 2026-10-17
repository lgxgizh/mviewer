// M59 Browse regression: exercise the production ThumbnailPanel evaluator and
// its generation-guarded latest-wins UI projection over a large source.

#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

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

template <typename Predicate> bool waitFor(Predicate &&predicate, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    return predicate();
}

QList<ThumbnailPanel::Entry> makeEntries(int count)
{
    QList<ThumbnailPanel::Entry> entries;
    entries.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const QString name = QStringLiteral("M59_camera_%1.jpg").arg(i, 5, 10, QLatin1Char('0'));
        entries.append({QStringLiteral("M59/%1").arg(name), name, i + 1, 1920, 1080,
                        QDateTime::fromSecsSinceEpoch(1000 + i)});
    }
    return entries;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 10k rows go through the exact value-snapshot evaluator used by the
    // worker, including metadata, type, tag, rating, and stable sorting.
    const auto source = makeEntries(10000);
    mviewer::core::BrowseQuery cameraQuery;
    cameraQuery.text = "camera";
    cameraQuery.metadata = true;
    cameraQuery.type = "jpg";
    cameraQuery.sort = mviewer::core::BrowseSortField::Size;
    cameraQuery.ascending = false;
    mviewer::core::RatingStore::Snapshot ratings;
    mviewer::core::TagStore::Snapshot tags;
    mviewer::core::TagStore::Snapshot emptyTags;
    QHash<QString, QString> metaIndex;
    QHash<QString, int> metaIso;
    QHash<QString, QString> metaCamera;
    QHash<QString, QString> metaLens;
    for (const auto &entry : source)
    {
        metaIndex.insert(entry.path, QStringLiteral("m59 camera lens"));
        metaIso.insert(entry.path, 100 + (entry.size % 3));
    }
    const auto evaluated = ThumbnailPanel::evaluateQuerySnapshotForTest(
        source, QStringLiteral("camera"), false, QRegularExpression(), cameraQuery, ratings, tags,
        metaIndex, metaIso, metaCamera, metaLens);
    CHECK(evaluated.size() == source.size(), "production evaluator keeps all matching 10k rows");
    CHECK(!evaluated.isEmpty() && evaluated.first().size > evaluated.last().size,
          "production evaluator applies descending size sort");

    // Keep the larger acceptance tier on the production evaluator too.  This
    // catches accidental O(N²) query work without involving filesystem I/O.
    const auto largeSource = makeEntries(50000);
    mviewer::core::BrowseQuery largeQuery;
    largeQuery.text = "camera";
    largeQuery.type = "jpg";
    largeQuery.sort = mviewer::core::BrowseSortField::Name;
    const auto largeEvaluated = ThumbnailPanel::evaluateQuerySnapshotForTest(
        largeSource, QStringLiteral("camera"), false, QRegularExpression(), largeQuery, ratings,
        emptyTags, {}, {}, {}, {});
    CHECK(largeEvaluated.size() == largeSource.size(),
          "production evaluator keeps all matching 50k rows");

    // A concrete metadata/tag/rating intersection must be evaluated by the
    // same function rather than by a test-only duplicate implementation.
    const auto focus = source.at(1234).path;
    ratings.ratings[focus.toStdString()] = 5;
    tags.tags[focus.toStdString()].insert("focus");
    metaCamera.insert(focus, "m59 camera");
    metaLens.insert(focus, "m59 lens");
    metaIso.insert(focus, 200);
    mviewer::core::BrowseQuery exactQuery;
    exactQuery.metadata = true;
    exactQuery.ratingMinimum = 5;
    exactQuery.tag = "focus";
    exactQuery.camera = "camera";
    exactQuery.lens = "lens";
    exactQuery.iso = 200;
    const auto exact = ThumbnailPanel::evaluateQuerySnapshotForTest(
        source, QString(), false, QRegularExpression(), exactQuery, ratings, tags, metaIndex,
        metaIso, metaCamera, metaLens);
    CHECK(exact.size() == 1 && exact.first().path == focus,
          "production evaluator combines rating/tag/camera/lens/ISO filters");

    ThumbnailPanel::Entry mixed;
    mixed.path = QStringLiteral("C:\\photos\\mixed.jpg");
    mixed.name = QStringLiteral("mixed.jpg");
    mixed.size = 10;
    QHash<QString, QString> mixedCamera;
    mixedCamera.insert(ThumbnailPanel::galleryPathKey(QStringLiteral("C:/photos/mixed.jpg")),
                       QStringLiteral("Canon EOS"));
    QHash<QString, QString> mixedIndex;
    mixedIndex.insert(ThumbnailPanel::galleryPathKey(QStringLiteral("C:/photos/mixed.jpg")),
                      QStringLiteral("canon portrait"));
    mviewer::core::BrowseQuery mixedQuery;
    mixedQuery.camera = "canon";
    mixedQuery.metadata = true;
    const auto mixedKept = ThumbnailPanel::evaluateQuerySnapshotForTest(
        {mixed}, QStringLiteral("portrait"), false, QRegularExpression(), mixedQuery, ratings,
        emptyTags, mixedIndex, {}, mixedCamera, {});
    CHECK(mixedKept.size() == 1 && mixedKept.first().path == mixed.path,
          "camera and metadata filters match the other slash style");

    // Drive a real panel over >256 files so A -> B -> C uses the cancellable,
    // debounced worker path.  Only C may become visible after quiescence.
    QTemporaryDir directory;
    CHECK(directory.isValid(), "temporary Browse directory is available");
    constexpr int kPerQuery = 220;
    for (const QString &prefix : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
    {
        for (int i = 0; i < kPerQuery; ++i)
        {
            QFile file(directory.filePath(QStringLiteral("%1_%2.jpg").arg(prefix).arg(i)));
            CHECK(file.open(QIODevice::WriteOnly), "Browse fixture file opens");
            file.write("M59");
        }
    }

    ThumbnailPanel panel;
    panel.setDirectory(directory.path());
    CHECK(waitFor([&]() { return panel.entries().size() == kPerQuery * 3; }, 10000),
          "real ThumbnailPanel commits the complete source snapshot");
    panel.setFilter(QStringLiteral("A"));
    panel.setFilter(QStringLiteral("B"));
    panel.setFilter(QStringLiteral("C"));
    CHECK(waitFor(
              [&]()
              {
                  if (panel.entries().size() != kPerQuery)
                      return false;
                  for (const auto &entry : panel.entries())
                      if (!entry.name.startsWith(QLatin1Char('C')))
                          return false;
                  return true;
              },
              10000),
          "real ThumbnailPanel latest-wins projection publishes only C");

    QList<ThumbnailPanel::Entry> ties = {
        {QStringLiteral("sort/b.png"), QStringLiteral("b.png"), 10, 8, 8,
         QDateTime::fromSecsSinceEpoch(50)},
        {QStringLiteral("sort/a.png"), QStringLiteral("a.png"), 10, 8, 8,
         QDateTime::fromSecsSinceEpoch(50)},
        {QStringLiteral("sort/img10.png"), QStringLiteral("img10.png"), 3, 1, 1,
         QDateTime::fromSecsSinceEpoch(10)},
        {QStringLiteral("sort/img2.png"), QStringLiteral("img2.png"), 3, 1, 1,
         QDateTime::fromSecsSinceEpoch(10)},
        {QStringLiteral("sort/img02.png"), QStringLiteral("img02.png"), 3, 2, 2,
         QDateTime::fromSecsSinceEpoch(20)},
    };
    const auto namesOf = [](const QList<ThumbnailPanel::Entry> &rows)
    {
        QStringList names;
        for (const auto &row : rows)
            names.append(row.name);
        return names.join(QLatin1Char(','));
    };
    mviewer::core::BrowseQuery sizeDesc;
    sizeDesc.sort = mviewer::core::BrowseSortField::Size;
    sizeDesc.ascending = false;
    const auto bySize = ThumbnailPanel::evaluateQuerySnapshotForTest(
        ties, QString(), false, QRegularExpression(), sizeDesc, ratings, emptyTags, {}, {}, {}, {});
    CHECK(namesOf(bySize) == QStringLiteral("a.png,b.png,img2.png,img02.png,img10.png"),
          "descending size keeps natural name order for equal sizes");
    mviewer::core::BrowseQuery byName;
    byName.sort = mviewer::core::BrowseSortField::Name;
    const auto named = ThumbnailPanel::evaluateQuerySnapshotForTest(
        ties, QString(), false, QRegularExpression(), byName, ratings, emptyTags, {}, {}, {}, {});
    CHECK(namesOf(named) == QStringLiteral("a.png,b.png,img2.png,img02.png,img10.png"),
          "natural names put img2 before img10 and a shorter zero-run first");
    mviewer::core::BrowseQuery dateDesc;
    dateDesc.sort = mviewer::core::BrowseSortField::Date;
    dateDesc.ascending = false;
    const auto byDate = ThumbnailPanel::evaluateQuerySnapshotForTest(
        ties, QString(), false, QRegularExpression(), dateDesc, ratings, emptyTags, {}, {}, {}, {});
    CHECK(namesOf(byDate) == QStringLiteral("a.png,b.png,img02.png,img2.png,img10.png"),
          "descending date keeps name order when timestamps match");

    // Details view must start ensureMetaIndex so camera/lens/ISO columns leave
    // the "-" placeholder. Thumbnail mode alone must not build that index.
    {
        QTemporaryDir detailsDir;
        CHECK(detailsDir.isValid(), "Details meta: temporary directory is available");
        const QString imagePath = detailsDir.filePath(QStringLiteral("details_meta.png"));
        {
            QImage img(32, 24, QImage::Format_RGB32);
            img.fill(QColor(20, 40, 60));
            CHECK(img.save(imagePath, "PNG"), "Details meta: PNG fixture written");
        }
        ThumbnailPanel detailsPanel;
        detailsPanel.setViewMode(ThumbnailPanel::Thumbnail);
        detailsPanel.setDirectory(detailsDir.path());
        CHECK(waitFor([&]() { return detailsPanel.entries().size() == 1; }, 10000),
              "Details meta: Thumbnail scan lands the fixture");
        CHECK(detailsPanel.metaIsoForPath(imagePath) < 0,
              "Details meta: Thumbnail mode leaves the EXIF index empty");
        detailsPanel.setViewMode(ThumbnailPanel::Details);
        CHECK(waitFor([&]() { return detailsPanel.metaIsoForPath(imagePath) >= 0; }, 20000),
              "Details meta: switching to Details indexes the gallery path");
    }

    // Details & List mode: newly entering a directory must load visible thumbnails
    // without leaving them as null/white placeholders, even after visiting another
    // directory with a scrolled visible window.
    {
        QTemporaryDir dir1;
        QTemporaryDir dir2;
        CHECK(dir1.isValid() && dir2.isValid(), "Thumb test: temporary directories available");
        for (int i = 0; i < 5; ++i)
        {
            QImage img(48, 48, QImage::Format_RGB32);
            img.fill(QColor(10 + i * 20, 30 + i * 15, 50 + i * 10));
            img.save(dir1.filePath(QStringLiteral("dir1_%1.png").arg(i)), "PNG");
            img.save(dir2.filePath(QStringLiteral("dir2_%1.png").arg(i)), "PNG");
        }

        ThumbnailPanel panel;
        panel.resize(800, 600);
        panel.show();
        panel.setViewMode(ThumbnailPanel::Details);

        // Enter dir1
        panel.setDirectory(dir1.path());
        CHECK(waitFor([&]() { return panel.entries().size() == 5; }, 10000),
              "Thumb test: dir1 scan lands 5 entries");
        // Simulate previous scroll in ThumbnailPipeline
        ThumbnailPipeline::instance().setVisibleRange(10, 20);

        // Now switch into dir2
        panel.setDirectory(dir2.path());
        CHECK(waitFor([&]() { return panel.entries().size() == 5; }, 10000),
              "Thumb test: dir2 scan lands 5 entries");
        const QString firstImage = dir2.filePath(QStringLiteral("dir2_0.png"));
        CHECK(waitFor([&]() { return !panel.thumbReady(firstImage).isNull(); }, 15000),
              "Thumb test: entering dir2 loads first visible thumbnail");

        // Re-enter dir1 (should immediately hit pipeline cache without stall)
        panel.setDirectory(dir1.path());
        CHECK(waitFor([&]() { return panel.entries().size() == 5; }, 10000),
              "Thumb test: re-entering dir1 lands entries");
        const QString dir1Image = dir1.filePath(QStringLiteral("dir1_0.png"));
        CHECK(waitFor([&]() { return !panel.thumbReady(dir1Image).isNull(); }, 15000),
              "Thumb test: re-entering dir1 loads thumbnail");
    }

    // Progressive scroll anchor test: inserting batches must preserve the
    // visual anchor position when the user has scrolled down.
    {
        QTemporaryDir testDir;
        CHECK(testDir.isValid(), "Progressive scroll test: temp dir available");
        for (int i = 0; i < 40; ++i)
        {
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(Qt::blue);
            img.save(
                testDir.filePath(QStringLiteral("scroll_%1.png").arg(i, 4, 10, QLatin1Char('0'))),
                "PNG");
        }

        ThumbnailPanel panel;
        panel.resize(600, 400);
        panel.show();
        panel.setDirectory(testDir.path());
        CHECK(waitFor([&]() { return panel.entries().size() == 40; }, 10000),
              "Progressive scroll test: directory scan finishes");

        QScrollBar *bar = panel.verticalScrollBar();
        CHECK(bar && bar->maximum() > 0, "Progressive scroll test: vertical scrollbar available");
        if (bar && bar->maximum() > 0)
        {
            bar->setValue(bar->maximum() / 2);
            const int savedValue = bar->value();
            CHECK(savedValue > 0, "Progressive scroll test: scrolled down");

            QString anchorPath;
            int anchorOffset = 0;
            panel.captureScrollAnchorForTest(anchorPath, anchorOffset);
            CHECK(!anchorPath.isEmpty(), "Progressive scroll test: anchor captured");
            const int oldRow = panel.pathList().indexOf(anchorPath);
            CHECK(oldRow >= 0, "Progressive scroll test: old row valid");
            const int oldVisualY = panel.visualRect(panel.model()->index(oldRow, 0)).top();

            // Simulate incoming progressive batch that sorts before the anchor
            QList<ThumbnailPanel::Entry> batch;
            batch.append({testDir.filePath(QStringLiteral("scroll_0000_early.png")),
                          QStringLiteral("scroll_0000_early.png"), 100, 16, 16,
                          QDateTime::currentDateTime()});
            panel.insertProgressiveRowsForTest(batch);

            // Anchor item must remain at the exact same screen position
            const int newRow = panel.pathList().indexOf(anchorPath);
            CHECK(newRow > oldRow, "Progressive scroll test: anchor shifted row downwards");
            const int newVisualY = panel.visualRect(panel.model()->index(newRow, 0)).top();
            CHECK(newVisualY == oldVisualY,
                  "Progressive scroll test: visual anchor item remains stationary on screen");
        }
    }

    std::printf("M59 Browse regression failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
