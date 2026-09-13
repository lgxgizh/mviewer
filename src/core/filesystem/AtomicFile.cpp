// M46 — crash-safe atomic file replace implementation (see AtomicFile.h).
#include "core/filesystem/AtomicFile.h"
#include "core/filesystem/Utf8Path.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mviewer::core
{

namespace
{
std::atomic<int> &tempCounter()
{
    static std::atomic<int> counter{0};
    return counter;
}

// True when `name` is exactly the temp shape this file writes for `base`:
// base + "." + pid + "." + ticks + "." + serial + ".tmp" (three numeric groups).
// Matching on the base-name prefix alone would delete unrelated files such as
// "settings.json.bak.tmp" or another tool's temp with the same starting name.
bool isStaleTempName(const std::string &name, const std::string &base)
{
    const std::string prefix = base + ".";
    if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0)
        return false;
    if (name.compare(name.size() - 4, 4, ".tmp") != 0)
        return false;

    const std::string middle = name.substr(prefix.size(), name.size() - prefix.size() - 4);
    int separators = 0;
    bool digitsSinceSeparator = false;
    for (char c : middle)
    {
        if (c == '.')
        {
            if (!digitsSinceSeparator)
                return false;
            ++separators;
            digitsSinceSeparator = false;
            continue;
        }
        if (c < '0' || c > '9')
            return false;
        digitsSinceSeparator = true;
    }
    return separators == 2 && digitsSinceSeparator;
}

std::mutex &faultMutex()
{
    static std::mutex mtx;
    return mtx;
}

AtomicWriteFaults g_faults;
} // namespace

void setAtomicWriteFaults(const AtomicWriteFaults &faults)
{
    std::lock_guard<std::mutex> lk(faultMutex());
    g_faults = faults;
}

AtomicWriteFaults atomicWriteFaults()
{
    std::lock_guard<std::mutex> lk(faultMutex());
    return g_faults;
}

bool atomicWriteFile(const std::string &path, const std::string &content,
                     std::string *errorOut)
{
    try
    {
    namespace fs = std::filesystem;
    const AtomicWriteFaults faults = atomicWriteFaults();

    std::error_code ec;
    const fs::path target = pathFromUtf8(path);
    const fs::path dir = target.parent_path();
    if (!dir.empty())
        fs::create_directories(dir, ec);

    // Unique temp name in the SAME directory (same volume => replace is
    // atomic). The pid + counter + timestamp component guarantees a crashed
    // process's temp can never be mistaken for (or collide with) a live one.
    const int serial = tempCounter().fetch_add(1, std::memory_order_relaxed);
    const unsigned long pid =
#ifdef _WIN32
        static_cast<unsigned long>(GetCurrentProcessId());
#else
        static_cast<unsigned long>(::getpid());
#endif
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string base = pathToUtf8(target.filename());
    const std::string tempName = base + "." + std::to_string(pid) + "." +
                                 std::to_string(now) + "." + std::to_string(serial) + ".tmp";
    const fs::path temp = dir.empty() ? pathFromUtf8(tempName)
                                      : dir / pathFromUtf8(tempName);

    auto cleanup = [&]()
    {
        std::error_code rmEc;
        fs::remove(temp, rmEc);
    };

    // Sweep stale temps from crashed writers (age-gated: anything older than
    // one hour for this target's base name). They are garbage, never state —
    // load() only ever reads the official path.
    //
    // Two constraints keep this cheap and safe: the name must match the exact
    // shape written above (`base.<pid>.<ticks>.<serial>.tmp`), so an unrelated
    // `base-something.tmp` is never deleted, and each directory is swept at most
    // once per process — this used to walk the whole directory on EVERY write
    // (sidecar/rating writes happen per image while browsing).
    {
        static std::mutex sweptMutex;
        static std::set<std::string> sweptDirs;
        const std::string dirKey = pathToUtf8(dir);
        bool sweep = false;
        {
            std::lock_guard<std::mutex> lk(sweptMutex);
            sweep = sweptDirs.insert(dirKey).second;
        }
        if (sweep)
        {
            const auto oneHour = fs::file_time_type::clock::now() - std::chrono::hours(1);
            std::error_code itEc;
            for (fs::directory_iterator it(dir, itEc), end; !itEc && it != end; it.increment(itEc))
            {
                const std::string name = pathToUtf8(it->path().filename());
                if (!isStaleTempName(name, base))
                    continue;
                std::error_code tEc;
                const auto mtime = fs::last_write_time(it->path(), tEc);
                if (!tEc && mtime < oneHour)
                    fs::remove(it->path(), tEc);
            }
        }
    }

    if (faults.failTempCreate)
    {
        if (errorOut)
            *errorOut = "fault injection: temp create failure";
        return false;
    }

    // 1) Write the temp file fully.
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (errorOut)
            *errorOut = "cannot create temp file: " + pathToUtf8(temp);
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (faults.failWrite)
        out.setstate(std::ios::failbit); // deterministic write-failure injection
    out.flush();
    if (out.fail())
    {
        out.close();
        cleanup();
        if (errorOut)
            *errorOut = "write/flush failed for: " + pathToUtf8(temp);
        return false;
    }
    out.close();
    if (out.fail())
    {
        cleanup();
        if (errorOut)
            *errorOut = "close failed for: " + pathToUtf8(temp);
        return false;
    }

    if (faults.failReplace)
    {
        cleanup();
        if (errorOut)
            *errorOut = "fault injection: replace failure";
        return false;
    }

    // 2) Atomic replace: the official file is either the complete old version
    // or the complete new version — never a mix.
#ifdef _WIN32
    const std::wstring tempW = temp.wstring();
    const std::wstring targetW = target.wstring();
    if (!MoveFileExW(tempW.c_str(), targetW.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        cleanup();
        if (errorOut)
            *errorOut = "replace failed for: " + path;
        return false;
    }
#else
    fs::rename(temp, target, ec);
    if (ec)
    {
        cleanup();
        if (errorOut)
            *errorOut = "replace failed for: " + path + ": " + ec.message();
        return false;
    }
#endif
    return true;
    }
    catch (const std::exception &ex)
    {
        if (errorOut)
            *errorOut = std::string("atomic write failed: ") + ex.what();
        return false;
    }
    catch (...)
    {
        if (errorOut)
            *errorOut = "atomic write failed: unknown error";
        return false;
    }
}

} // namespace mviewer::core
