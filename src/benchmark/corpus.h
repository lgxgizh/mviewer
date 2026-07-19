#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Runtime corpus generator for the MViewer benchmark suite.
// Synthesizes images into a temp dir (no committed binaries) using QImage,
// matching the known-good paths the decoder golden images use.
//
//  - JPEG / PNG : 24 MP (6000 x 4000) RGB, per docs/performance.md budget.
//  - TIFF       : 4 MP (2000 x 2000) RGB (TIFF codec is gated, keep light).
//
// The generator fills a deterministic gradient + noise so decoders do real work
// (no flat-color shortcut that would make codec timing meaningless).

namespace mviewer::bench
{

struct Corpus
{
    std::string dir;
    std::vector<std::string> jpegPaths;
    std::vector<std::string> pngPaths;
    std::vector<std::string> tiffPaths;

    std::vector<std::string> allPaths() const;
    void clear() const; // deletes the temp dir
};

// totalImages: how many of EACH format to generate (default 1000 per format).
// jpegW/H: dimensions of the generated JPEG/PNG images.
// outDir: if non-empty, emit the corpus into this directory (no auto-removal,
//   no scenarios) — used by the P3 `benchmark/data/` tier generator. Defaults
//   to "" which uses MVIEWER_BENCH_TMP / system TEMP as before.
// format: "all" (default) emits jpeg+png+tif; "jpeg" emits JPEG only. The JPEG
//   tier is what the review's P3 "large: 10000 jpeg" calls for and is ~60KB/img,
//   so a 10000-image large tier fits disks where the full triplet (1.6MB/img)
//   would not.
// NOTE: keep these modest by default — generating multi-thousand-pixel images
// needs a live GUI/Qt context and is slow; the bench harness overrides sizes
// per-scenario. Default 512x512 is enough to exercise real decode/encode paths.
Corpus makeCorpus(size_t totalImages = 1000, int jpegW = 512, int jpegH = 512,
                  const std::string &outDir = "", const std::string &format = "all");

} // namespace mviewer::bench
