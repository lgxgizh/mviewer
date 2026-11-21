// M46 — crash-safe persistence fault-injection regression tests.
//
// Contracts under test:
//   P1  atomicWriteFile: a successful write leaves the COMPLETE new version;
//       every injected failure (temp create / write / durable flush / replace)
//       returns false and leaves the PREVIOUS official version byte-identical
//       — a legal state file can never become a half-written text/JSON file.
//   P2  no temp files are left behind after a failed write; stale temps from
//       a crashed process are never read as state and are swept once aged.

#include "core/filesystem/AtomicFile.h"

#include <QDir>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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
        {true, false, false},        // temp create
        {false, true, false},        // write
        {false, false, true},        // replace
        {false, false, false, true}, // durable flush
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

    // Age the stale temp beyond the sweep horizon. The sweep is amortized to the
    // FIRST write in each directory (a per-write full-directory walk was the
    // defect this replaced), so this case uses its own fresh directory.
    std::error_code ec;
    const std::string sweepDir = dir + "/aged";
    fs::create_directories(sweepDir, ec);
    const std::string sweepPath = sweepDir + "/state.txt";
    const std::string aged = sweepDir + "/state.txt.9999.1.1.tmp";
    {
        std::ofstream out(aged, std::ios::binary);
        out << "garbage from a crashed writer";
    }
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(2);
    fs::last_write_time(aged, old, ec);
    CHECK(atomicWriteFile(sweepPath, "fresh-state-2\n"), "write succeeds in a new directory");
    CHECK(tempFilesIn(sweepDir, "state.txt") == 0,
          "aged stale temp swept by the first write in a directory");
    CHECK(readAll(sweepPath) == "fresh-state-2\n", "official state unaffected by the sweep");
    QDir(tmp.path()).removeRecursively();
}

} // namespace

int main()
{
    printf("=== M46 persistence crash-safety tests ===\n");
    fflush(stdout);

    testAtomicWriteBasics();
    testStaleTempHandling();

    setAtomicWriteFaults({}); // never leak fault injection

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
