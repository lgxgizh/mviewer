#pragma once

#include <string>

// M46 — crash-safe atomic file replace (Qt-free core helper).
//
// Contract:
//   1. a successful write leaves the COMPLETE new version at `path`;
//   2. any failed write leaves the PREVIOUS official version intact — a legal
//      state file can never become a half-written JSON/text file;
//   3. the official file is replaced only after the temp file is fully
//      written, flushed and closed (temp -> flush/close -> replace);
//   4. temp files are uniquely named and are NEVER read as official state, so
//      a stale temp left by a crashed process cannot corrupt the next start;
//      stale temps from crashed processes are swept (age-gated) before a write;
//   5. file formats are untouched — this helper only changes HOW a byte
//      sequence reaches the official path.
//
// Failure semantics are deterministic and testable via setAtomicWriteFaults().
namespace mviewer::core
{

// Atomically replace `path` with `content`. Returns true on success. On
// failure returns false (errorOut receives a diagnostic when provided) and
// `path` still holds its previous content (or does not exist).
bool atomicWriteFile(const std::string &path, const std::string &content,
                     std::string *errorOut = nullptr);

// M46 deterministic-test instrumentation. When a flag is set, the matching
// step fails exactly as if the OS reported that failure: the function aborts
// at that point, cleans up its temp file, and leaves the official file
// untouched. Production code never sets these.
struct AtomicWriteFaults
{
    bool failTempCreate = false;  // temp file creation fails
    bool failWrite = false;       // the write/flush reports failure
    bool failReplace = false;     // the final replace fails
};

void setAtomicWriteFaults(const AtomicWriteFaults &faults);
AtomicWriteFaults atomicWriteFaults();

} // namespace mviewer::core
