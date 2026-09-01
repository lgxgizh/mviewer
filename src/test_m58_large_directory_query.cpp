// M58 large-directory query acceptance.
//
// This is intentionally deterministic and does not require a native desktop:
// it exercises the same value-snapshot/search path used by SearchPanel with
// synthetic 10k/50k entries. Native GUI first-paint/typing feel remains a
// separately recorded MANUAL/BLOCKED qualification item in the Phase-0 doc.
#include "core/RatingStore.h"
#include "core/TagStore.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/search/BrowseQuery.h"
#include "core/search/SearchEngine.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::vector<mviewer::core::MetadataIndexEntry> makeEntries(size_t count)
{
    std::vector<mviewer::core::MetadataIndexEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const std::string path = "M58/IMG_" + std::to_string(i) + ".jpg";
        entries.push_back({path, "img_" + std::to_string(i) + " m58 camera", "M58 camera",
                           "M58 lens", static_cast<int>(i % 4), 1920, 1080});
    }
    return entries;
}

void runCase(size_t count)
{
    const auto entries = makeEntries(count);
    mviewer::core::SearchEngine engine;
    engine.indexEntries(entries);
    mviewer::domain::SearchQuery query;
    query.text = "camera";
    query.searchFilenames = false;
    query.searchPaths = false;
    query.searchMetadata = true;
    query.searchAnalysis = false;

    const auto started = std::chrono::steady_clock::now();
    const auto snapshot = engine.snapshot();
    const auto results = mviewer::core::SearchEngine::searchSnapshot(snapshot, query);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    assert(snapshot.size() == count);
    assert(results.size() == count);
    std::printf("M58 large_query entries=%zu results=%zu snapshot_search_ms=%lld\n", count,
                results.size(), static_cast<long long>(elapsed));
}

template <typename Predicate> void waitFor(Predicate &&predicate, int timeoutMs = 10000)
{
    const auto started = std::chrono::steady_clock::now();
    while (!predicate())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        if (elapsed >= timeoutMs)
            break;
    }
}

void runMetadataBatchCases()
{
    using Indexer = mviewer::core::MetadataIndexer;
    std::vector<std::string> paths;
    for (int i = 0; i < 513; ++i)
        paths.push_back("M58/batch_" + std::to_string(i) + ".jpg");

    size_t delivered = 0;
    size_t batchCount = 0;
    size_t largestBatch = 0;
    bool done = false;
    const uint64_t requestId = Indexer::instance().indexBatched(
        paths,
        [&](const std::vector<Indexer::Entry> &batch)
        {
            ++batchCount;
            delivered += batch.size();
            largestBatch = std::max(largestBatch, batch.size());
        },
        [&]() { done = true; });
    assert(requestId != 0);
    waitFor([&]() { return done; });
    assert(done);
    assert(delivered == paths.size());
    assert(batchCount == 3);
    assert(largestBatch <= 256);

    // Cancellation must invalidate already queued batches as well as work
    // still running on the worker. The first delivered batch cancels the
    // request; every later queued callback must observe that token.
    paths.clear();
    for (int i = 0; i < 4096; ++i)
        paths.push_back("M58/cancel_" + std::to_string(i) + ".jpg");
    size_t cancelledBatches = 0;
    bool cancelledDone = false;
    uint64_t cancelledId = 0;
    cancelledId = Indexer::instance().indexBatched(
        paths,
        [&](const std::vector<Indexer::Entry> &)
        {
            ++cancelledBatches;
            Indexer::instance().cancelRequest(cancelledId);
        },
        [&]() { cancelledDone = true; });
    assert(cancelledId != 0);
    waitFor([&]() { return cancelledBatches > 0 || cancelledDone; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    assert(cancelledBatches <= 1);
    assert(!cancelledDone);
    std::printf("M58 metadata batches delivered=%zu largest=%zu cancelled=%zu\n", batchCount,
                largestBatch, cancelledBatches);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    mviewer::core::BrowseQuery query;
    query.text = "m58";
    query.generation = 2;
    const auto copy = query;
    assert(copy.text == query.text && copy.generation == query.generation);

    // Snapshot APIs must be callable without exposing per-entry locking to a
    // worker. The singleton data is intentionally empty in this hermetic test.
    const auto ratings = mviewer::core::RatingStore::instance().snapshot();
    const auto tags = mviewer::core::TagStore::instance().snapshot();
    assert(ratings.rating("missing") == 0);
    assert(!tags.hasTag("missing", "m58"));

    runMetadataBatchCases();
    runCase(10000);
    runCase(50000);
    std::puts("M58 large directory query acceptance: PASS");
    return 0;
}
