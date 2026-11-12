#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/rawplain/PlainRawFormat.h"
#include "domain/Image.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ADR 033 / ADR 034: file-level access to headerless dumps (.raw sensor or
// .gray/.grey grayscale). Qt-free; never throws (std exceptions from file I/O
// or allocation are caught inside).

namespace mviewer::core::rawplain
{

// Headerless dump: .raw sensor dump or .gray/.grey grayscale, ADR 033/034.
bool isPlainRawPath(const std::string &path);

// Case-insensitive ".gray" / ".grey" only.
bool isGrayRawPath(const std::string &path);

// Gray for .gray/.grey, otherwise Sensor (including paths that are not dumps).
DumpKind dumpKindForPath(const std::string &path);

// normalizeKey(path) (core/filesystem/Utf8Path.h); key for overrides.
std::string plainRawParamKey(const std::string &path);

// Global defaults plus per-file session overrides. defaults() / setDefaults(p)
// are the sensor pair (Preferences > RAW); gray defaults are separate (ADR 034).
// Thread-safe. Overrides stay one map keyed by plainRawParamKey.
class PlainRawParamStore
{
  public:
    static PlainRawParamStore &instance();

    Params defaults() const;           // Sensor
    void setDefaults(const Params &p); // Sensor; bumps revision()
    // Gray stores normalizedForKind(p, Gray). Sensor stores p unchanged.
    Params defaults(DumpKind kind) const;
    void setDefaults(DumpKind kind, const Params &p); // bumps revision()
    std::optional<Params> override(const std::string &key) const;
    void setOverride(const std::string &key, const Params &p); // bumps revision()
    void clearOverride(const std::string &key);                // bumps revision()
    // Moves a present override fromKey -> toKey (overwrites toKey). Missing
    // or equal keys: no-op, and revision() is not bumped.
    void moveOverride(const std::string &fromKey, const std::string &toKey);
    // Copies a present override onto toKey (overwrites toKey). Missing or
    // equal keys: no-op. Bumps revision() when a copy is written.
    void copyOverride(const std::string &fromKey, const std::string &toKey);
    // Sensor defaults back to Params{}, gray defaults to grayDefaultParams(),
    // no overrides. Bumps revision().
    void clearAll();
    uint64_t revision() const;

  private:
    mutable std::mutex m_mutex;
    Params m_defaults;
    Params m_grayDefaults = grayDefaultParams();
    std::unordered_map<std::string, Params> m_overrides;
    uint64_t m_revision = 0;
};

struct PlainRawFileInfo
{
    Resolved resolved; // params actually used
    bool fromOverride = false;
    int64_t fileSize = -1;       // -1 when the file cannot be sized
    std::vector<Params> guesses; // guessFromFileSize(resolved.params, fileSize, kind)
    std::string depthNote;       // non-empty when samples exceeded the configured bit depth
};

// kind = dumpKindForPath(path). An override is normalizedForKind(*saved, kind)
// then resolveOverride (size checks and why are filled; packing is not guessed).
// Otherwise resolveForFile(defaults(kind), file name, file size, kind).
// Unopenable file -> resolved.known false, why "cannot open file".
PlainRawFileInfo resolvePlainRawFile(const std::string &path);

// Reads frameBytes - headerOffset bytes at headerOffset and unpacks them.
// Lsb16 samples wider than bitDepth: without an override, bitDepth becomes
// the smallest of 12/14/16 that fits, blackLevel shifts up and whiteLevel
// becomes 0; with an override the samples are clipped. depthNote explains
// either case. False (why set) when !info.resolved.known, on short read
// or unpack failure.
bool readPlainRawSamples(const std::string &path, PlainRawFileInfo &info,
                         std::vector<std::uint16_t> &samples, std::string *why = nullptr);

// resolvePlainRawFile + readPlainRawSamples + renderPlainRaw(maxEdge).
// *infoOut, when set, is the info after the sample read (detected depth).
// Empty ImageData (why set) on failure.
ImageData decodePlainRawFile(const std::string &path, int maxEdge,
                             PlainRawFileInfo *infoOut = nullptr, std::string *why = nullptr);

// Fills width, height, fileSize, fileName, filePath, bitDepth, channels
// (1 for Mono else 3), frameCount 1, currentFrame 0, sequenceKind "static",
// animated false. Sensor format is "RAW (plain)" and colorSpace e.g.
// "RGGB 10-bit". Gray format is "Gray (headerless)" and colorSpace
// "Gray N-bit". When depthNote is set, colorSpace gains " (<note>)" and
// textKeys["MViewer.PlainRaw.Note"] stores the note.
void fillPlainRawMetadata(const PlainRawFileInfo &info, const std::string &path,
                          mviewer::domain::ImageMetadata &meta);

// "|praw=" + toString(normalized override or kind defaults) for plain dump
// paths, "" otherwise. Store lookup only, no file I/O.
std::string plainRawCacheKeySuffix(const std::string &path);
// Suffix empty -> key has no "|praw=" marker. Otherwise key ends with suffix.
bool plainRawCacheKeyAgrees(const std::string &key, const std::string &suffix);

// How plainRawSampleText obtains samples.
// Blocking reads the payload on a cache miss (existing callers and tests).
// CachedOrAsync returns cached text, or "" and starts one background fill.
// A fill starts only when none is running. See plainRawSampleFillIdle.
enum class SampleRead : std::uint8_t
{
    Blocking,
    CachedOrAsync
};

// Stored sample under (x, y) in source coordinates, e.g. "RAW G=512 (10-bit)"
// or "GRAY=128 (8-bit)", "" when not a known dump or out of range. Keeps up to
// four unpacked frames (path + params + file size + mtime), most recently used
// first, so hovering does not re-read the file. CachedOrAsync never reads the
// payload on the calling thread: a miss returns "" after starting at most one
// background fill (resolvePlainRawFile + readPlainRawSamples, then an LRU
// insert).
std::string plainRawSampleText(const std::string &path, int x, int y,
                               SampleRead mode = SampleRead::Blocking);

// True when no background sample fill is running.
bool plainRawSampleFillIdle();

} // namespace mviewer::core::rawplain
