#include "core/search/SearchEngine.h"
#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"
#include "core/metadata/MetadataIndexer.h"
#include "domain/Image.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace mviewer::core
{
namespace
{

std::string toLower(std::string_view s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s)
        r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

std::string snippetFromPos(std::string_view text, size_t pos, size_t matchLen, size_t radius = 40)
{
    if (text.empty() || pos == std::string_view::npos)
        return {};
    const size_t start = (pos > radius) ? (pos - radius) : 0;
    const size_t end = std::min(pos + matchLen + radius, text.size());
    std::string snip(text.substr(start, end - start));
    if (start > 0)
        snip = "..." + snip;
    if (end < text.size())
        snip += "...";
    return snip;
}

int calcScore(size_t matchCount, size_t totalMatches, domain::SearchMatch::Type type)
{
    int base = 0;
    switch (type)
    {
    case domain::SearchMatch::Type::Filename:
        base = 40;
        break;
    case domain::SearchMatch::Type::Metadata:
        base = 20;
        break;
    case domain::SearchMatch::Type::Analysis:
        base = 10;
        break;
    case domain::SearchMatch::Type::Path:
        base = 5;
        break;
    }
    // Prefer fewer files with more matches; demote if spread across many files.
    if (totalMatches == 0)
        return base + static_cast<int>(matchCount * 5);
    return base + static_cast<int>(matchCount * 5) - static_cast<int>(totalMatches / 2);
}

} // anonymous namespace

// ── SearchIndex ──────────────────────────────────────────────────────────────

std::string SearchIndex::buildBlob(const domain::ImageMetadata &meta, const RawMetadata &raw,
                                   const std::string &analysisText)
{
    std::ostringstream oss;
    oss << meta.fileName << " " << meta.filePath << " " << meta.format << " ";
    for (const auto &[k, v] : meta.textKeys)
        oss << k << " " << v << " ";
    oss << raw.make << " " << raw.model << " " << raw.lens << " ";
    if (raw.iso > 0)
        oss << "ISO" << raw.iso << " ";
    if (raw.focalLength > 0)
        oss << raw.focalLength << "mm ";
    if (raw.exposureSec > 0.0)
        oss << raw.exposureSec << "s ";
    if (raw.fNumber > 0.0)
        oss << "f/" << raw.fNumber << " ";
    if (raw.width > 0)
        oss << raw.width << "x" << raw.height << " ";
    if (!analysisText.empty())
        oss << analysisText;
    return toLower(oss.str());
}

void SearchIndex::reserve(size_t capacity)
{
    m_blobs.reserve(capacity);
    m_pathIndex.reserve(capacity);
}

void SearchIndex::indexFile(const std::string &path, const domain::ImageMetadata &meta,
                            const RawMetadata &raw, const std::string &analysisText)
{
    auto it = m_pathIndex.find(path);
    if (it != m_pathIndex.end())
    {
        m_blobs[it->second].blob = buildBlob(meta, raw, analysisText);
        return;
    }
    const size_t idx = m_blobs.size();
    m_pathIndex[path] = idx;
    m_blobs.push_back({path, buildBlob(meta, raw, analysisText)});
}

void SearchIndex::indexBlob(const std::string &path, const std::string &blob)
{
    auto it = m_pathIndex.find(path);
    if (it != m_pathIndex.end())
    {
        m_blobs[it->second].blob = blob;
        return;
    }
    const size_t idx = m_blobs.size();
    m_pathIndex[path] = idx;
    m_blobs.push_back({path, blob});
}

void SearchIndex::removeFile(const std::string &path)
{
    auto it = m_pathIndex.find(path);
    if (it == m_pathIndex.end())
        return;

    const size_t idx = it->second;
    m_blobs.erase(m_blobs.begin() + idx);
    m_pathIndex.erase(it);
    for (size_t i = idx; i < m_blobs.size(); ++i)
    {
        m_pathIndex[m_blobs[i].path] = i;
    }
}

void SearchIndex::clear()
{
    m_blobs.clear();
    m_pathIndex.clear();
}

std::vector<domain::SearchResult>
SearchIndex::search(const domain::SearchQuery &query,
                    const AnalysisTextProvider &analysisProvider) const
{
    (void)analysisProvider;
    if (query.text.empty())
        return {};

    std::vector<domain::SearchResult> results;
    const std::string &term = query.text;
    const std::string termLower = toLower(term);

    for (const auto &entry : m_blobs)
    {
        std::vector<domain::SearchMatch> matches;

        // Filename match (extract just the filename part).
        const auto sep = entry.path.find_last_of("/\\");
        const std::string_view fname = (sep != std::string::npos)
                                           ? std::string_view(entry.path).substr(sep + 1)
                                           : std::string_view(entry.path);

        if (query.searchFilenames)
        {
            size_t pos = std::string::npos;
            if (query.caseSensitive)
            {
                pos = fname.find(term);
            }
            else
            {
                auto it = std::search(
                    fname.begin(), fname.end(), termLower.begin(), termLower.end(),
                    [](char a, char b)
                    {
                        return static_cast<char>(std::tolower(static_cast<unsigned char>(a))) == b;
                    });
                if (it != fname.end())
                    pos = static_cast<size_t>(std::distance(fname.begin(), it));
            }
            if (pos != std::string::npos)
            {
                matches.push_back({domain::SearchMatch::Type::Filename, "",
                                   snippetFromPos(fname, pos, term.size())});
            }
        }

        // Path match.
        if (query.searchPaths)
        {
            size_t pos = std::string::npos;
            if (query.caseSensitive)
            {
                pos = entry.path.find(term);
            }
            else
            {
                auto it = std::search(
                    entry.path.begin(), entry.path.end(), termLower.begin(), termLower.end(),
                    [](char a, char b)
                    {
                        return static_cast<char>(std::tolower(static_cast<unsigned char>(a))) == b;
                    });
                if (it != entry.path.end())
                    pos = static_cast<size_t>(std::distance(entry.path.begin(), it));
            }
            if (pos != std::string::npos)
            {
                matches.push_back({domain::SearchMatch::Type::Path, "",
                                   snippetFromPos(entry.path, pos, term.size())});
            }
        }

        // Blob (metadata + analysis) match.
        if (query.searchMetadata || query.searchAnalysis)
        {
            size_t pos = std::string::npos;
            if (query.caseSensitive)
            {
                pos = entry.blob.find(term);
            }
            else
            {
                // entry.blob is already lowercased when built
                pos = entry.blob.find(termLower);
            }

            if (pos != std::string::npos)
            {
                const std::string snip = snippetFromPos(entry.blob, pos, term.size());
                if (query.searchMetadata)
                    matches.push_back({domain::SearchMatch::Type::Metadata, "", snip});
                if (query.searchAnalysis)
                    matches.push_back({domain::SearchMatch::Type::Analysis, "", snip});
            }
        }

        if (!matches.empty())
        {
            int score = 0;
            for (const auto &m : matches)
                score += calcScore(1, matches.size(), m.type);
            results.push_back({entry.path, std::move(matches), score});
        }
    }

    std::sort(results.begin(), results.end());
    return results;
}

// ── SearchEngine ─────────────────────────────────────────────────────────────

void SearchEngine::indexDirectory(const std::vector<std::string> &paths,
                                  const std::vector<domain::ImageMetadata> &metas,
                                  const std::vector<RawMetadata> &raws,
                                  const AnalysisTextProvider &analysisProvider)
{
    m_analysisProvider = analysisProvider;
    m_index.clear();

    const size_t n = std::min({paths.size(), metas.size(), raws.size()});
    m_index.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        std::string analysisText;
        if (m_analysisProvider)
            analysisText = m_analysisProvider(paths[i]);
        m_index.indexFile(paths[i], metas[i], raws[i], analysisText);
    }
}

void SearchEngine::reset()
{
    m_index.clear();
    m_analysisProvider = {};
}

void SearchEngine::indexEntry(const MetadataIndexEntry &entry)
{
    m_index.indexBlob(entry.path, entry.searchBlob);
}

void SearchEngine::indexEntries(const std::vector<MetadataIndexEntry> &entries)
{
    m_index.clear();
    m_index.reserve(entries.size());
    for (const auto &e : entries)
        m_index.indexBlob(e.path, e.searchBlob);
}

std::vector<domain::SearchResult> SearchEngine::search(const domain::SearchQuery &query) const
{
    return m_index.search(query, m_analysisProvider);
}

} // namespace mviewer::core
