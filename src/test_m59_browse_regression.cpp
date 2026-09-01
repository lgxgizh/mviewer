// M59 Browse regression: exercise the production ThumbnailPanel evaluator and
// its generation-guarded latest-wins UI projection over a large source.

#include "thumbnailpanel.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
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

    std::printf("M59 Browse regression failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
