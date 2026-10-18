#pragma once

#include <cstdint>
#include <string>

namespace mviewer::core
{

// One stat of a filesystem path. Missing files report exists=false and zeros.
struct FileStamp
{
    bool exists = false;
    int64_t sizeBytes = 0;
    int64_t mtimeMs = 0;
    int64_t mtimeSec = 0;
};

FileStamp fileStamp(const std::string &utf8Path);

// Persisted cache key. The path is round-tripped through QString so the bytes
// match MetadataReader::key (invalid UTF-8 becomes U+FFFD). Separators keep
// "a.jpg"|123 from colliding with "a.jpg12"|3.
std::string fileIdentityKey(const std::string &utf8Path, int64_t sizeBytes, int64_t mtimeMs);
std::string fileIdentityKey(const std::string &utf8Path);

// path|size|mtimeSec using the original path bytes (not a QString round-trip).
std::string fileIdentityCompositeSec(const std::string &path, int64_t sizeBytes, int64_t mtimeSec);

// In-memory revisions. No path component. The path overloads stat the file.
// fileRevisionMs is empty when the path does not exist; fileRevisionSec is not.
std::string fileRevisionSec(int64_t mtimeSec, int64_t sizeBytes);
std::string fileRevisionSec(const std::string &utf8Path);
std::string fileRevisionMs(int64_t mtimeMs, int64_t sizeBytes);
std::string fileRevisionMs(const std::string &utf8Path);

// Thumbnail filename: sha1hex(path)_mtimeMs_size_edge_v{schema}. Persisted.
std::string thumbnailDiskKey(const std::string &utf8Path, int edge, int64_t mtimeMs,
                             int64_t sizeBytes, int schemaVersion);

// ImageObject SHA-1 preimage: QString-round-tripped path, size and mtimeSec
// concatenated with no separators.
std::string legacyUnseparatedIdentity(const std::string &utf8Path, int64_t sizeBytes,
                                      int64_t mtimeSec);

} // namespace mviewer::core
