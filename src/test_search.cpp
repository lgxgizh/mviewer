// SearchEngine unit tests — verify index building, search, ranking, and
// integration with MetadataReader + RawMetadata.
#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"
#include "core/search/SearchEngine.h"
#include "domain/Image.h"
#include "domain/SearchQuery.h"
#include "domain/SearchResult.h"

#include <QCoreApplication>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;

static void CHECK(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ── setup: create metadata for a few synthetic images ───────────────

    std::vector<std::string> paths = {
        "/test/photo_a.jpg",
        "/test/photo_b.png",
        "/test/landscape_c.jpg",
        "/test/portrait_d.tiff",
    };

    std::vector<mviewer::domain::ImageMetadata> metas;
    std::vector<mviewer::core::RawMetadata> raws;

    {
        mviewer::domain::ImageMetadata m;
        m.fileName = "photo_a.jpg";
        m.filePath = "/test/photo_a.jpg";
        m.format = "JPEG";
        m.textKeys["Make"] = "Canon";
        m.textKeys["Model"] = "EOS R5";
        m.textKeys["DateTimeOriginal"] = "2025:01:15 08:30:00";
        metas.push_back(m);

        mviewer::core::RawMetadata r;
        r.make = "Canon";
        r.model = "EOS R5";
        r.lens = "RF 24-70mm f/2.8L";
        r.iso = 400;
        r.focalLength = 35;
        r.fNumber = 2.8;
        r.exposureSec = 0.002;
        raws.push_back(r);
    }
    {
        mviewer::domain::ImageMetadata m;
        m.fileName = "photo_b.png";
        m.filePath = "/test/photo_b.png";
        m.format = "PNG";
        m.textKeys["Software"] = "GIMP 2.10";
        metas.push_back(m);

        mviewer::core::RawMetadata r;
        r.make = "Sony";
        r.model = "A7IV";
        r.lens = "FE 85mm f/1.4 GM";
        r.iso = 100;
        r.focalLength = 85;
        r.fNumber = 1.4;
        r.exposureSec = 0.0005;
        raws.push_back(r);
    }
    {
        mviewer::domain::ImageMetadata m;
        m.fileName = "landscape_c.jpg";
        m.filePath = "/test/landscape_c.jpg";
        m.format = "JPEG";
        m.textKeys["Make"] = "Nikon";
        m.textKeys["Model"] = "Z8";
        m.textKeys["Description"] = "Sunrise over mountains with fog";
        metas.push_back(m);

        mviewer::core::RawMetadata r;
        r.make = "Nikon";
        r.model = "Z8";
        r.lens = "Nikkor Z 14-24mm f/2.8 S";
        r.iso = 64;
        r.focalLength = 14;
        r.fNumber = 8.0;
        r.exposureSec = 0.0167;
        raws.push_back(r);
    }
    {
        mviewer::domain::ImageMetadata m;
        m.fileName = "portrait_d.tiff";
        m.filePath = "/test/portrait_d.tiff";
        m.format = "TIFF";
        m.textKeys["Artist"] = "Jane Doe";
        m.textKeys["Copyright"] = "2025 Studio";
        metas.push_back(m);

        mviewer::core::RawMetadata r;
        r.make = "Canon";
        r.model = "5D Mark IV";
        r.lens = "EF 70-200mm f/2.8L IS III";
        r.iso = 800;
        r.focalLength = 135;
        r.fNumber = 2.8;
        r.exposureSec = 0.004;
        raws.push_back(r);
    }

    // ── SearchIndex unit tests ─────────────────────────────────────────

    mviewer::core::SearchIndex idx;

    // Index all four files (no analysis text yet).
    for (size_t i = 0; i < paths.size(); ++i)
        idx.indexFile(paths[i], metas[i], raws[i], "");

    CHECK(idx.size() == 4, "Index should contain 4 entries after indexing");

    // Update an existing entry.
    {
        mviewer::core::RawMetadata r2 = raws[0];
        r2.iso = 800;
        idx.indexFile(paths[0], metas[0], r2, "ISO800_updated");
        CHECK(idx.size() == 4, "Index size unchanged after update");
    }

    // Remove one entry.
    idx.removeFile(paths[3]);
    CHECK(idx.size() == 3, "Index size should decrease after removal");

    // Re-add.
    idx.indexFile(paths[3], metas[3], raws[3], "");
    CHECK(idx.size() == 4, "Index size back to 4 after re-add");

    // Clear.
    idx.clear();
    CHECK(idx.size() == 0, "Index should be empty after clear");

    // Rebuild for query tests.
    for (size_t i = 0; i < paths.size(); ++i)
        idx.indexFile(paths[i], metas[i], raws[i], "");

    // ── Query: filename search ─────────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "photo";
        q.searchFilenames = true;
        q.searchMetadata = false;
        q.searchAnalysis = false;

        auto results = idx.search(q);
        CHECK(results.size() == 2, "Filename search for 'photo' should find 2 files");
        // Verify score ordering.
        CHECK(results.front().score >= results.back().score,
              "Results should be ordered by descending score");
    }

    // ── Query: metadata search (camera model) ──────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "Canon";
        q.searchFilenames = false;
        q.searchMetadata = true;
        q.searchAnalysis = false;

        auto results = idx.search(q);
        CHECK(results.size() == 2, "Metadata search for 'Canon' should find 2 files");
    }

    // ── Query: lens search ─────────────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "85mm";
        q.searchMetadata = true;
        q.searchFilenames = false;

        auto results = idx.search(q);
        CHECK(results.size() == 1, "Lens search for '85mm' should find 1 file");
        CHECK(results.front().filePath == "/test/photo_b.png",
              "85mm lens should match Sony A7IV photo");
    }

    // ── Query: ISO search ──────────────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "ISO400";
        q.searchMetadata = true;
        q.searchFilenames = false;

        auto results = idx.search(q);
        CHECK(results.size() >= 1, "ISO search should find at least 1 match");
    }

    // ── Query: multi-scope search ──────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "f/2.8";
        q.searchMetadata = true;
        q.searchFilenames = false;

        auto results = idx.search(q);
        CHECK(results.size() >= 2, "Aperture 'f/2.8' should match multiple files");
    }

    // ── Query: no results ──────────────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "nonexistent_term_xyz123";
        q.searchFilenames = true;
        q.searchMetadata = true;

        auto results = idx.search(q);
        CHECK(results.empty(), "Nonsense query should return no results");
    }

    // ── Query: empty text ──────────────────────────────────────────────

    {
        mviewer::domain::SearchQuery q;
        q.text = "";

        auto results = idx.search(q);
        CHECK(results.empty(), "Empty query should return no results");
    }

    // ── SearchEngine integration test ─────────────────────────────────

    {
        mviewer::core::SearchEngine engine;
        engine.indexDirectory(paths, metas, raws, {});

        mviewer::domain::SearchQuery q;
        q.text = "nikon";
        q.searchFilenames = true;
        q.searchMetadata = true;

        auto results = engine.search(q);
        CHECK(results.size() == 1, "Engine should find Nikon via metadata");
        CHECK(results.front().filePath == "/test/landscape_c.jpg",
              "Nikon camera should match Z8 image");

        engine.reset();
        auto resultsAfterReset = engine.search(q);
        CHECK(resultsAfterReset.empty(), "Engine should return no results after reset");
    }

    // ── Analysis text test ──────────────────────────────────────────────

    {
        // Re-index with analysis text.
        idx.clear();
        for (size_t i = 0; i < paths.size(); ++i)
        {
            std::string analysis;
            if (i == 0)
                analysis = "Histogram: mean=128 stddev=45 min=0 max=255";
            else if (i == 2)
                analysis = "NoiseEstimate: SNR=32dB ISO64";
            idx.indexFile(paths[i], metas[i], raws[i], analysis);
        }

        {
            mviewer::domain::SearchQuery q;
            q.text = "Histogram";
            q.searchAnalysis = true;
            q.searchFilenames = false;
            q.searchMetadata = false;

            auto results = idx.search(q);
            CHECK(results.size() == 1, "Analysis search for 'Histogram' should find 1 file");
            CHECK(results.front().filePath == "/test/photo_a.jpg",
                  "Histogram should match photo_a");
        }

        {
            mviewer::domain::SearchQuery q;
            q.text = "SNR";
            q.searchAnalysis = true;
            q.searchFilenames = false;

            auto results = idx.search(q);
            CHECK(results.size() >= 1, "Analysis search for 'SNR' should find matches");
        }
    }

    // ── Result struct tests ────────────────────────────────────────────

    {
        mviewer::domain::SearchResult a, b;
        a.score = 100;
        b.score = 50;
        CHECK(a < b, "Higher score should sort before lower score");
    }

    std::fprintf(stderr, "%s: %d failures\n", g_fail == 0 ? "All tests passed" : "Tests failed",
                 g_fail);
    return g_fail == 0 ? 0 : 1;
}
