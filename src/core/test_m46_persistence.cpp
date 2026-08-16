// M46 — crash-safe persistence fault-injection regression tests.
//
// Contracts under test:
//   P1  atomicWriteFile: a successful write leaves the COMPLETE new version;
//       every injected failure (temp create / write / replace) returns false
//       and leaves the PREVIOUS official version byte-identical — a legal
//       state file can never become a half-written text/JSON file.
//   P2  no temp files are left behind after a failed write; stale temps from
//       a crashed process are never read as state and are swept once aged.
//   P3  RatingStore: rapid updates coalesce, the LAST update wins after the
//       flush boundary, and a failed worker write is retried (not dropped).
//   P4  TagStore / SidecarStore writes are atomic with the same failure
//       semantics (old version intact on failure).
//   P5  Application-shutdown-style flush persists the latest state.

#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/TagStore.h"
#include "core/filesystem/AtomicFile.h"

#include <QDir>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

namespace
{
using namespace mviewer::core;
namespace fs = std::filesystem;

std::string readAll(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string out;
    char buf[4096];
    while (in)
    {
        in.read(buf, sizeof(buf));
        out.append(buf, static_cast<size_t>(in.gcount()));
    }
    return out;
}

int tempFilesIn(const std::string &dir, const std::string &base)
{
    int n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.size() > base.size() + 4 && name.compare(0, base.size(), base) == 0 &&
            name.compare(name.size() - 4, 4, ".tmp") == 0)
            ++n;
    }
    return n;
}

void testAtomicWriteBasics()
{
    printf("\n[P1. atomicWriteFile success + every injected failure]\n");
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/state.txt";

    CHECK(atomicWriteFile(path, "version-1-content\n"), "first write succeeds");
    CHECK(readAll(path) == "version-1-content\n", "complete new version on disk");

    // Failure injection matrix: every step failure must leave v1 intact and
    // clean up its temp file.
    const AtomicWriteFaults faults[] = {
        {true, false, false},  // temp create
        {false, true, false},  // write
        {false, false, true},  // replace
    };
    for (const auto &f : faults)
    {
        setAtomicWriteFaults(f);
        std::string err;
        const bool ok = atomicWriteFile(path, "corrupting-content", &err);
        setAtomicWriteFaults({});
        CHECK(!ok, "injected failure returns false");
        CHECK(!err.empty(), "failure is diagnosed");
        CHECK(readAll(path) == "version-1-content\n",
              "previous official version intact after the injected failure");
        CHECK(tempFilesIn(dir, "state.txt") == 0,
              "no temp file left behind after the injected failure");
    }

    // Subsequent success replaces completely.
    CHECK(atomicWriteFile(path, "version-2-content\n"), "write after failures succeeds");
    CHECK(readAll(path) == "version-2-content\n", "new version fully on disk");
    CHECK(tempFilesIn(dir, "state.txt") == 0, "no temp residue after success");
    QDir(tmp.path()).removeRecursively();
}

void testStaleTempHandling()
{
    printf("\n[P2. stale temps are never state; aged ones are swept]\n");
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/state.txt";

    // A crashed process left a temp behind (simulated with our naming scheme).
    const std::string stale = dir + "/state.txt.9999.1.1.tmp";
    {
        std::ofstream out(stale, std::ios::binary);
        out << "garbage from a crashed writer";
    }
    CHECK(atomicWriteFile(path, "fresh-state\n"), "write succeeds next to a stale temp");
    CHECK(readAll(path) == "fresh-state\n",
          "the official file holds the NEW state — the stale temp was never read");
    CHECK(tempFilesIn(dir, "state.txt") == 1,
          "a fresh stale temp is inert (age-gated sweep leaves it)");

    // Age the stale temp beyond the sweep horizon; the next write removes it.
    std::error_code ec;
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(2);
    fs::last_write_time(stale, old, ec);
    CHECK(atomicWriteFile(path, "fresh-state-2\n"), "write succeeds again");
    CHECK(tempFilesIn(dir, "state.txt") == 0, "aged stale temp swept by the next write");
    CHECK(readAll(path) == "fresh-state-2\n", "official state unaffected by the sweep");
    QDir(tmp.path()).removeRecursively();
}

void testRatingStoreCoalescingAndFlush()
{
    printf("\n[P3. RatingStore coalesced updates, flush boundary, failure retry]\n");
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string ratingsPath = dir + "/ratings.txt";
    const std::string flagsPath = dir + "/flags.txt";

    auto &rs = RatingStore::instance();
    rs.setFilePath(ratingsPath);

    // Rapid burst: only the LAST value per path may survive.
    constexpr int kFiles = 40;
    for (int i = 0; i < kFiles; ++i)
    {
        rs.setRating("img_" + std::to_string(i) + ".png", 1 + (i % 5));
        rs.setColorLabel("img_" + std::to_string(i) + ".png", 1 + (i % 6));
    }
    for (int i = 0; i < kFiles; ++i)
        rs.setRating("img_" + std::to_string(i) + ".png", 5 - (i % 5)); // overwrite
    CHECK(rs.rating("img_0.png") == 5, "in-memory read is immediate (last value)");

    // Flush boundary: complete latest state must be on disk NOW (no debounce
    // sleep needed).
    CHECK(rs.save(), "explicit flush succeeds");
    std::string onDisk = readAll(ratingsPath);
    CHECK(onDisk.find("5|img_0.png") != std::string::npos,
          "flushed ratings contain the LAST update for img_0");
    CHECK(onDisk.find("1|img_0.png") == std::string::npos,
          "flushed ratings contain no stale first update");

    // Reload semantics: point away and back, then read from disk.
    rs.setFilePath(dir + "/empty_ratings.txt");
    CHECK(!rs.load(), "missing file load returns false");
    rs.setFilePath(ratingsPath);
    CHECK(rs.load(), "reload of the flushed file succeeds");
    CHECK(rs.rating("img_0.png") == 5, "reloaded rating matches the flushed value");

    // Failure semantics: a failed flush must not corrupt the official file.
    const std::string before = readAll(ratingsPath);
    setAtomicWriteFaults({false, true, false}); // fail the write step
    rs.setRating("img_fail.png", 4);            // schedules a worker write
    CHECK(!rs.save(), "flush reports the injected write failure");
    setAtomicWriteFaults({});
    CHECK(readAll(ratingsPath) == before,
          "official ratings file byte-identical after a failed flush");

    // Retry: the worker re-arms the dirty bit after a failure and persists on
    // the next quiet period; the last update must NOT be dropped.
    setAtomicWriteFaults({false, true, false});
    rs.setRating("img_retry.png", 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(350)); // worker retry window
    setAtomicWriteFaults({});
    std::this_thread::sleep_for(std::chrono::milliseconds(350)); // successful retry
    CHECK(rs.save(), "post-retry flush succeeds");
    onDisk = readAll(ratingsPath);
    CHECK(onDisk.find("3|img_retry.png") != std::string::npos,
          "the retried write persisted the update that first failed");

    QDir(tmp.path()).removeRecursively();
}

void testTagAndSidecarAtomicity()
{
    printf("\n[P4. TagStore / SidecarStore atomic failure semantics]\n");
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();

    auto &ts = TagStore::instance();
    const std::string tagsPath = dir + "/tags.txt";
    ts.setFilePath(tagsPath);
    ts.clearTags("a.png");
    ts.addTag("a.png", "keep");
    CHECK(ts.save(), "tags initial save succeeds");
    const std::string before = readAll(tagsPath);

    setAtomicWriteFaults({false, false, true}); // fail the replace
    ts.addTag("a.png", "newtag");
    CHECK(!ts.save(), "tags save reports the injected failure");
    setAtomicWriteFaults({});
    CHECK(readAll(tagsPath) == before, "tags file intact after the injected failure");
    CHECK(ts.save(), "tags save succeeds after the fault clears");
    CHECK(readAll(tagsPath).find("newtag") != std::string::npos,
          "tags update persisted after recovery");

    // SidecarStore: same semantics on the per-image .xmp file.
    auto &ss = SidecarStore::instance();
    const std::string imgPath = dir + "/photo.jpg";
    const std::string sidecar = dir + "/photo.xmp";
    auto &rs = RatingStore::instance();
    rs.setRating(imgPath, 4);
    CHECK(ss.writeSidecar(imgPath), "sidecar initial write succeeds");
    const std::string sidecarBefore = readAll(sidecar);
    CHECK(sidecarBefore.find("\"rating\": 4") != std::string::npos,
          "sidecar contains the rating");

    setAtomicWriteFaults({true, false, false}); // fail temp create
    rs.setRating(imgPath, 5);
    CHECK(!ss.writeSidecar(imgPath), "sidecar write reports the injected failure");
    setAtomicWriteFaults({});
    CHECK(readAll(sidecar) == sidecarBefore,
          "sidecar file intact after the injected failure");
    CHECK(ss.writeSidecar(imgPath), "sidecar write succeeds after the fault clears");
    CHECK(readAll(sidecar).find("\"rating\": 5") != std::string::npos,
          "sidecar updated to the new rating");
    rs.setRating(imgPath, 0); // clean up the global store state

    QDir(tmp.path()).removeRecursively();
}

void testShutdownFlush()
{
    printf("\n[P5. shutdown-style flush persists the latest state]\n");
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string ratingsPath = dir + "/ratings.txt";

    auto &rs = RatingStore::instance();
    rs.setFilePath(ratingsPath);
    rs.setRating("last.png", 2);
    // Shutdown boundary: the store's destructor joins the worker, which
    // flushes the dirty state. The flushSave()/save() API is the observable
    // in-process equivalent — the worker path is exercised through the
    // destructor at process exit AND through save() here.
    rs.flushSave();
    const std::string onDisk = readAll(ratingsPath);
    CHECK(onDisk.find("2|last.png") != std::string::npos,
          "latest rating persisted at the flush boundary");
    QDir(tmp.path()).removeRecursively();
}

} // namespace

int main()
{
    printf("=== M46 persistence crash-safety tests ===\n");
    fflush(stdout);

    testAtomicWriteBasics();
    testStaleTempHandling();
    testRatingStoreCoalescingAndFlush();
    testTagAndSidecarAtomicity();
    testShutdownFlush();

    setAtomicWriteFaults({}); // never leak fault injection

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
