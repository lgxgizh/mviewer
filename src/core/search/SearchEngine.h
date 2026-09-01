#pragma once
#include "domain/SearchQuery.h"
#include "domain/SearchResult.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mviewer::domain
{
struct ImageMetadata;
}

namespace mviewer::core
{
struct RawMetadata;
struct MetadataIndexEntry;

// Forward declare: callback that provides analyzer result text for a given path.
using AnalysisTextProvider = std::function<std::string(const std::string &path)>;

// SearchIndex maps a file path to its concatenated searchable text blob.
// The blob is built from metadata (EXIF + camera) and (optionally) analyzer output.
// It lives in core, so the header stays Qt-free while the .cpp may use Qt.
class SearchIndex
{
  public:
    SearchIndex() = default;

    // Add or update the index entry for a single file.
    void indexFile(const std::string &path, const domain::ImageMetadata &meta,
                   const RawMetadata &raw, const std::string &analysisText);

    // Remove an entry (e.g. when an image is closed).
    void removeFile(const std::string &path);

    // Clear all entries.
    void clear();

    // Number of indexed files.
    size_t size() const
    {
        return m_blobs.size();
    }

    // Execute a domain-level SearchQuery and return ranked results.
    // analysisProvider is called for paths that haven't been indexed yet
    // (lazy-fill), or pass a no-op provider if pre-indexed.
    std::vector<domain::SearchResult>
    search(const domain::SearchQuery &query,
           const AnalysisTextProvider &analysisProvider = {}) const;

    // Add or update the raw searchable blob for a path (used by indexEntry).
    void indexBlob(const std::string &path, const std::string &blob);

  private:
    struct Entry
    {
        std::string path;
        std::string blob; // concatenated, lowercased searchable text
    };
    std::vector<Entry> m_blobs;

    static std::string buildBlob(const domain::ImageMetadata &meta, const RawMetadata &raw,
                                 const std::string &analysisText);
};

// SearchEngine orchestrates searching across a set of file paths.
// It builds a SearchIndex from available data sources and executes queries.
class SearchEngine
{
  public:
    using Snapshot = SearchIndex;
    SearchEngine() = default;

    // Register a data source: all images in a directory, with their metadata.
    // Existing entries for the same path are updated.
    void indexDirectory(const std::vector<std::string> &paths,
                        const std::vector<domain::ImageMetadata> &metas,
                        const std::vector<RawMetadata> &raws,
                        const AnalysisTextProvider &analysisProvider = {});

    // Remove all entries and providers.
    void reset();

    // Rebuild the index from the shared MetadataIndexer's already-built
    // entries (M25: the search panel and the gallery filters reuse one async
    // indexing pass instead of parsing every file twice on the UI thread).
    void indexEntry(const MetadataIndexEntry &entry);
    void indexEntries(const std::vector<MetadataIndexEntry> &entries);

    // Return ranked results for the given query.
    std::vector<domain::SearchResult> search(const domain::SearchQuery &query) const;

    // M58: copy the immutable searchable index once at the UI boundary. The
    // returned value is safe to evaluate on a worker while new indexing starts.
    Snapshot snapshot() const
    {
        return m_index;
    }

    static std::vector<domain::SearchResult> searchSnapshot(const Snapshot &snapshot,
                                                             const domain::SearchQuery &query)
    {
        return snapshot.search(query);
    }

    // Access the underlying index (useful for tests).
    const SearchIndex &index() const
    {
        return m_index;
    }

  private:
    SearchIndex m_index;
    AnalysisTextProvider m_analysisProvider;
};

} // namespace mviewer::core
