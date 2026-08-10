// M25 RC convergence — Phase 1 regression baseline (core level).
//
// Guards the Professional Browse data-pipeline contracts before the fixes land:
//   1. MetadataFilter field semantics — Camera matches the camera fields only,
//      Lens matches the lens field only (no whole-blob cross-matching).
//   2. Sort keys — resolution/camera/lens keys are computed ONCE per file, never
//      inside a comparator (no file I/O / metadata parse during std::sort).
//   3. MetadataIndexer — asynchronous, cancellable, generation-scoped metadata
//      indexing with progressive delivery and cache reuse.
#include "core/image/ImageSortKeys.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/search/MetadataFilter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            std::cout << "  PASS: " << msg << "\n";                                                \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "  FAIL: " << msg << "\n";                                                \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using mviewer::core::RawMetadata;

// ─── 1. MetadataFilter field semantics ──────────────────────────────────────
static void testMetadataFilterFields()
{
    std::cout << "\n[MetadataFilter field semantics]\n";
    RawMetadata raw;
    raw.make = "SONY";
    raw.model = "ILCE-7RM4";
    raw.lens = "Canon EF 24-70mm F2.8"; // deliberately a DIFFERENT brand in lens
    raw.iso = 6400;

    // Camera filter must match the camera fields...
    CHECK(mviewer::core::metadata::matchesCamera(raw, "sony"), "camera filter 'sony' matches make");
    CHECK(mviewer::core::metadata::matchesCamera(raw, "ilce"), "camera filter matches model");
    // ...but must NOT match the lens field.
    CHECK(!mviewer::core::metadata::matchesCamera(raw, "canon"),
          "camera filter 'canon' does NOT match the lens field");
    CHECK(!mviewer::core::metadata::matchesCamera(raw, "24-70"),
          "camera filter '24-70' does NOT match the lens field");

    // Lens filter must match the lens field...
    CHECK(mviewer::core::metadata::matchesLens(raw, "canon"), "lens filter 'canon' matches lens");
    CHECK(mviewer::core::metadata::matchesLens(raw, "24-70"), "lens filter '24-70' matches lens");
    // ...but must NOT match the camera fields.
    CHECK(!mviewer::core::metadata::matchesLens(raw, "sony"),
          "lens filter 'sony' does NOT match the camera fields");
    CHECK(!mviewer::core::metadata::matchesLens(raw, "ilce"),
          "lens filter 'ilce' does NOT match the model");

    // ISO is exact numeric.
    CHECK(mviewer::core::metadata::matchesIso(raw, 6400), "ISO filter matches exact value");
    CHECK(!mviewer::core::metadata::matchesIso(raw, 3200), "ISO filter rejects other value");
    CHECK(mviewer::core::metadata::isoOf(raw) == 6400, "isoOf reads the sensor ISO");
}

// ─── 2. Sort keys computed once per file ────────────────────────────────────
static void testSortKeysOncePerFile()
{
    std::cout << "\n[Sort keys: one metadata/dimension pass per file]\n";
    QTemporaryDir tmp;
    std::vector<std::string> paths;
    for (int i = 0; i < 50; ++i)
    {
        const std::string p = tmp.path().toStdString() + "/img" + std::to_string(i) + ".dng";
        QFile f(QString::fromStdString(p));
        f.open(QIODevice::WriteOnly);
        f.close();
        paths.push_back(p);
    }

    // Counting readers prove the "once per file, never inside the comparator"
    // contract: exactly N expensive reads for N files, whatever sort we run.
    int dimReads = 0;
    int metaReads = 0;
    auto dimReader = [&](const std::string &) -> int64_t
    {
        ++dimReads;
        return 1234567;
    };
    auto metaReader = [&](const std::string &) -> RawMetadata
    {
        ++metaReads;
        RawMetadata rm;
        rm.make = "SONY";
        rm.model = "A7R";
        rm.lens = "LENS" + std::to_string(metaReads);
        return rm;
    };

    for (mviewer::core::SortField field :
         {mviewer::core::SortField::Resolution, mviewer::core::SortField::Camera,
          mviewer::core::SortField::Lens, mviewer::core::SortField::Name,
          mviewer::core::SortField::Size, mviewer::core::SortField::Date})
    {
        dimReads = 0;
        metaReads = 0;
        const auto keys =
            mviewer::core::computeSortKeys(paths, field, dimReader, metaReader);
        CHECK(keys.size() == paths.size(), "one key per file");
        const int expectedDim = (field == mviewer::core::SortField::Resolution) ? 50 : 0;
        const int expectedMeta = (field == mviewer::core::SortField::Camera ||
                                  field == mviewer::core::SortField::Lens)
                                     ? 50
                                     : 0;
        CHECK(dimReads == expectedDim, "dimension reads happen once per file, only when needed");
        CHECK(metaReads == expectedMeta, "metadata parses happen once per file, only when needed");
    }

    // Key values must agree with the underlying sources (spot check via the
    // counting readers' contract: resolution key reflects dimReader output).
    const auto resKeys = mviewer::core::computeSortKeys(
        {paths[0]}, mviewer::core::SortField::Resolution, dimReader, metaReader);
    CHECK(!resKeys.empty() && resKeys[0].resolution == 1234567,
          "resolution key matches the reader value");
}

// ─── 3. MetadataIndexer async / cancellable / generation-scoped ─────────────
static void testMetadataIndexer()
{
    std::cout << "\n[MetadataIndexer: async, cancellable, progressive]\n";
    QTemporaryDir tmp;
    std::vector<std::string> paths;
    for (int i = 0; i < 8; ++i)
    {
        const std::string p = tmp.path().toStdString() + "/img" + std::to_string(i) + ".dng";
        QFile f(QString::fromStdString(p));
        f.open(QIODevice::WriteOnly);
        f.close();
        paths.push_back(p);
    }

    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel(); // reset any prior generation

    int delivered = 0;
    bool done = false;
    const uint64_t token = indexer.index(
        paths,
        [&](const mviewer::core::MetadataIndexer::Entry &e)
        {
            CHECK(!e.path.empty(), "delivered entry carries a path");
            ++delivered;
        },
        [&]() { done = true; });

    // Pump the event loop until completion (bounded).
    QElapsedTimer t;
    t.start();
    while (!done && t.elapsed() < 10000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(done, "indexer reports completion");
    CHECK(delivered == 8, "every path delivered exactly once");
    CHECK(token != 0, "indexer returns a generation token");

    // Cache reuse: a second index of the same files must not re-read metadata
    // (verify via cached() being populated without a new pass).
    for (const auto &p : paths)
        CHECK(indexer.cached(p).has_value(), "cached entry available after indexing");

    // Cancellation: start a big job, cancel it, and verify the cancelled
    // generation never delivers a late completion.
    std::vector<std::string> big;
    for (int i = 0; i < 200; ++i)
        big.push_back(tmp.path().toStdString() + "/big" + std::to_string(i) + ".dng");
    int cancelledDelivered = 0;
    bool cancelledDone = false;
    indexer.index(
        big,
        [&](const mviewer::core::MetadataIndexer::Entry &) { ++cancelledDelivered; },
        [&]() { cancelledDone = true; });
    indexer.cancel();
    t.restart();
    while (t.elapsed() < 2000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!cancelledDone, "cancelled generation never reports completion");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== Browse pipeline convergence tests (M25 phase 1, core) ===\n";
    fflush(stdout);

    testMetadataFilterFields();
    testSortKeysOncePerFile();
    testMetadataIndexer();

    std::cout << "\n=== Results: " << g_pass << " passed, " << g_fail << " failed ===\n";
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
