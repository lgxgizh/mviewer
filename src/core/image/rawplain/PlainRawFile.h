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

// ADR 033: file-level access to plain .raw sensor dumps. Qt-free; never
// throws (std exceptions from file I/O or allocation are caught inside).

namespace mviewer::core::rawplain
{

// Case-insensitive ".raw" suffix.
bool isPlainRawPath(const std::string &path);

// normalizeKey(path) (core/filesystem/Utf8Path.h); key for overrides.
std::string plainRawParamKey(const std::string &path);

// Global defaults (Preferences > RAW) plus per-file session overrides set by
// the RAW Parameters dialog. Thread-safe.
class PlainRawParamStore
{
  public:
    static PlainRawParamStore &instance();

    Params defaults() const;
    void setDefaults(const Params &p); // bumps revision()
    std::optional<Params> override(const std::string &key) const;
    void setOverride(const std::string &key, const Params &p); // bumps revision()
    void clearOverride(const std::string &key);                // bumps revision()
    void clearAll(); // defaults back to Params{}, no overrides; bumps revision()
    uint64_t revision() const;

  private:
    mutable std::mutex m_mutex;
    Params m_defaults;
    std::unordered_map<std::string, Params> m_overrides;
    uint64_t m_revision = 0;
};

struct PlainRawFileInfo
{
    Resolved resolved; // params actually used
    bool fromOverride = false;
    int64_t fileSize = -1;       // -1 when the file cannot be sized
    std::vector<Params> guesses; // guessFromFileSize(resolved.params, fileSize)
};

// Override for the file when set (resolveForFile is still run with the
// override as 'defaults' so size checks and why are filled), else
// resolveForFile(store defaults, file name, file size). Unopenable file ->
// resolved.known false, why "cannot open file".
PlainRawFileInfo resolvePlainRawFile(const std::string &path);

// Reads frameBytes - headerOffset bytes at headerOffset and unpacks them.
// False (why set) when !info.resolved.known, on short read or unpack failure.
bool readPlainRawSamples(const std::string &path, const PlainRawFileInfo &info,
                         std::vector<std::uint16_t> &samples, std::string *why = nullptr);

// resolvePlainRawFile + readPlainRawSamples + renderPlainRaw(maxEdge).
// Empty ImageData (why set) on failure.
ImageData decodePlainRawFile(const std::string &path, int maxEdge,
                             PlainRawFileInfo *infoOut = nullptr, std::string *why = nullptr);

// Fills width, height, fileSize, fileName, filePath, bitDepth, channels
// (1 for Mono else 3), format "RAW (plain)", colorSpace e.g. "RGGB 10-bit",
// frameCount 1, currentFrame 0, sequenceKind "static", animated false.
void fillPlainRawMetadata(const PlainRawFileInfo &info, const std::string &path,
                          mviewer::domain::ImageMetadata &meta);

// "|praw=" + toString(override or defaults) for .raw paths, "" otherwise.
// Store lookup only, no file I/O.
std::string plainRawCacheKeySuffix(const std::string &path);
// Suffix empty -> key has no "|praw=" marker. Otherwise key ends with suffix.
bool plainRawCacheKeyAgrees(const std::string &key, const std::string &suffix);

// Stored sample under (x, y) in source coordinates, e.g. "RAW G=512 (10-bit)",
// "" when not a known .raw or out of range. Keeps the last unpacked frame
// (path + params + file size) so hovering does not re-read the file.
std::string plainRawSampleText(const std::string &path, int x, int y);

} // namespace mviewer::core::rawplain
