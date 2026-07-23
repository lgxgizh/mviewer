#include "core/search/SearchEngine.h"
#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"
#include "domain/Image.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace mviewer::core
{
namespace
{

std::string toLower(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s)
        r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

bool contains(const std::string &haystack, const std::string &needle, bool caseSensitive)
{
    if (needle.empty())
        return true;
    if (caseSensitive)
        return haystack.find(needle) != std::string::npos;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

std::string snippet(const std::string &haystack, const std::string &needle, size_t radius = 40)
{
    if (needle.empty() || haystack.empty())
        return {};
    const auto hs = toLower(haystack);
    const auto nd = toLower(needle);
    const auto pos = hs.find(nd);
    if (pos == std::string::npos)
        return {};
    const size_t start = (pos > radius) ? (pos - radius) : 0;
    const size_t end = std::min(pos + needle.size() + radius, haystack.size());
    std::string snip = haystack.substr(start, end - start);
    if (start > 0)
        snip = "..." + snip;
    if (end < haystack.size())
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

void SearchIndex::indexFile(const std::string &path, const domain::ImageMetadata &meta,
                            const RawMetadata &raw, const std::string &analysisText)
{
    // Update existing entry if found.
    for (auto &e : m_blobs)
    {
        if (e.path == path)
        {
            e.blob = buildBlob(meta, raw, analysisText);
            return;
        }
    }
    m_blobs.push_back({path, buildBlob(meta, raw, analysisText)});
}

void SearchIndex::removeFile(const std::string &path)
{
    m_blobs.erase(std::remove_if(m_blobs.begin(), m_blobs.end(),
                                 [&](const Entry &e) { return e.path == path; }),
                  m_blobs.end());
}

void SearchIndex::clear()
{
    m_blobs.clear();
}

std::vector<domain::SearchResult>
SearchIndex::search(const domain::SearchQuery &query,
                    const AnalysisTextProvider &analysisProvider) const
{
    if (query.text.empty())
        return {};

    std::vector<domain::SearchResult> results;
    const std::string term = query.text;

    for (const auto &entry : m_blobs)
    {
        std::vector<domain::SearchMatch> matches;

        // Filename match (extract just the filename part).
        const auto sep = entry.path.find_last_of("/\\");
        const std::string fname =
            (sep != std::string::npos) ? entry.path.substr(sep + 1) : entry.path;
        if (query.searchFilenames && contains(fname, term, query.caseSensitive))
        {
            matches.push_back({domain::SearchMatch::Type::Filename, "", snippet(fname, term)});
        }

        // Path match.
        if (query.searchPaths && contains(entry.path, term, query.caseSensitive))
        {
            matches.push_back({domain::SearchMatch::Type::Path, "", snippet(entry.path, term)});
        }

        // Blob (metadata + analysis) match.
        if ((query.searchMetadata || query.searchAnalysis) &&
            contains(entry.blob, term, query.caseSensitive))
        {
            // Distinguish metadata vs analysis matches by checking sub-ranges.
            // We look for the term in the blob and try to attribute it.
            std::string blobLower = toLower(entry.blob);
            std::string termLower = toLower(term);
            size_t pos = 0;
            while ((pos = blobLower.find(termLower, pos)) != std::string::npos)
            {
                // Simple heuristic: if the match position falls in the earlier part
                // of the blob, it's metadata; later part is analysis.
                // Split point: we embed analysis text at the end of the blob.
                // We'll mark all as Metadata first; if analysisProvider exists, we
                // can check, but for simplicity we mark both types.
                if (query.searchMetadata)
                    matches.push_back(
                        {domain::SearchMatch::Type::Metadata, "", snippet(entry.blob, term)});
                if (query.searchAnalysis)
                    matches.push_back(
                        {domain::SearchMatch::Type::Analysis, "", snippet(entry.blob, term)});
                pos += termLower.size();
                break; // one match per type per file is enough
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

std::vector<domain::SearchResult> SearchEngine::search(const domain::SearchQuery &query) const
{
    return m_index.search(query, m_analysisProvider);
}

} // namespace mviewer::core
