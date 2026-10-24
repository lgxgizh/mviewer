#include "benchmark/bench_progress.h"
#include "benchmark/scenarios.h"

#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"

#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace mviewer::bench
{
// ─── B10 / A-8.4: 100MP tile-cache viewport request ──────────────────────────
// Treat the source as a 10000×10000 (100 MP) image and measure how long
// TileCache::request() takes to fill a 1920×1080 viewport at fit scale.
// Tiles are synthesized on demand (no full 400 MB buffer) so the metric
// isolates tile enumeration + cache put/get + region fill cost without
// stressing CI hosts.
ScenarioResult scenarioHundredMpTiles()
{
    constexpr int kW = 10000;
    constexpr int kH = 10000;
    constexpr int kTile = 256;
    constexpr int kScreenW = 1920;
    constexpr int kScreenH = 1080;

    Viewport vp;
    vp.screenW = kScreenW;
    vp.screenH = kScreenH;
    vp.fit(kW, kH);

    TileGrid grid(kW, kH, kTile);
    TileCache cache;
    cache.maxTiles = 512;

    auto decode = [&](const std::string & /*id*/, int sx, int sy, int sw, int sh, int dw,
                      int dh) -> ImageData
    {
        if (sx < 0)
            sx = 0;
        if (sy < 0)
            sy = 0;
        if (sx + sw > kW)
            sw = kW - sx;
        if (sy + sh > kH)
            sh = kH - sy;
        if (sw <= 0 || sh <= 0 || scenarioPastDeadline())
            return ImageData{};
        // Materialize the canonical output resolution requested by TileCache.
        // This keeps the synthetic benchmark aligned with the byte-budgeted
        // render path instead of returning the full source region.
        if (dw <= 0 || dh <= 0)
            return ImageData{};
        ImageData tile = makeImageData(dw, dh, PixelFormat::RGBA32);
        if (tile.isNull() || !tile.buffer)
            return ImageData{};
        // Deterministic procedural fill — cheap stand-in for a region decode.
        auto &buf = *tile.buffer;
        for (int y = 0; y < dh; ++y)
        {
            if (scenarioPastDeadline())
                return ImageData{};
            for (int x = 0; x < dw; ++x)
            {
                const int sourceX = sx + (x * sw) / dw;
                const int sourceY = sy + (y * sh) / dh;
                const size_t i = (static_cast<size_t>(y) * dw + x) * 4;
                buf[i + 0] = static_cast<uint8_t>(sourceX & 0xFF);
                buf[i + 1] = static_cast<uint8_t>(sourceY & 0xFF);
                buf[i + 2] = static_cast<uint8_t>((sourceX ^ sourceY) & 0xFF);
                buf[i + 3] = 255;
            }
        }
        return tile;
    };

    // Warm-up (not timed): exercise the path once so first-call allocator noise
    // does not dominate the measured sample.
    {
        int warmCalls = 0;
        (void)cache.request("100mp", vp, grid, decode, &warmCalls);
        cache.clear();
    }

    if (scenarioPastDeadline())
        return timeoutResult("B10", "hundred_mp_viewport_ms", scenarioDeadlineBudgetMs(), false);

    // Timed cold request (empty cache).
    int decodeCalls = 0;
    const double t0 = nowMs();
    auto ready = cache.request("100mp", vp, grid, decode, &decodeCalls);
    const double coldMs = nowMs() - t0;
    if (scenarioPastDeadline())
        return timeoutResult("B10", "hundred_mp_viewport_ms", scenarioDeadlineBudgetMs(), false);

    // Timed warm request (all tiles cached) — should be near-instant.
    int warmCalls = 0;
    const double t1 = nowMs();
    auto readyWarm = cache.request("100mp", vp, grid, decode, &warmCalls);
    const double warmMs = nowMs() - t1;

    ScenarioResult r;
    r.name = "B10";
    r.metric = "hundred_mp_viewport_ms";
    r.value = coldMs;
    r.timing.samples = 1;
    r.timing.p50Ms = coldMs;
    r.timing.p95Ms = coldMs;
    r.timing.p99Ms = coldMs;
    // B10 is REPORT-ONLY by design (performance_budget.json excludes it from the
    // scenario_map hard gate; it is only +/-10% regression-checked under
    // --regression). The verdict below is therefore STRUCTURAL: the request must
    // fill the viewport and the warm request must hit the cache (zero decodes).
    // No absolute cold-latency cap lives here: an absolute cap that fits the
    // fastest dev box fails on legitimately slower CI/VM hosts (measured
    // 392.8 ms on the 2026-07-24 baseline box vs 1480 ms on a 2-core Xeon VM),
    // so absolute gating would need a data-driven entry in
    // performance_budget.json with documented hardware provenance.
    r.passed = !ready.empty() && (warmCalls == 0);
    r.detail = "image=10000x10000 tiles_ready=" + std::to_string(ready.size()) +
               " decode_calls=" + std::to_string(decodeCalls) +
               " cold_ms=" + std::to_string(coldMs) + " warm_ms=" + std::to_string(warmMs) +
               " warm_decode_calls=" + std::to_string(warmCalls) +
               " cache_size=" + std::to_string(cache.size()) +
               " warm_ready=" + std::to_string(readyWarm.size());
    return r;
}

// ─── Post-M22 named scenarios (review "performance regression" asks) ───────────────────
namespace
{
// Render a deterministic gradient + noise frame (stand-in for a real photo/RAW).
QImage makeSyntheticFrame(int w, int h, uint32_t seed)
{
    QImage img(w, h, QImage::Format_RGB888);
    std::mt19937 rng(seed ? seed : 0x9e3779b9u);
    std::uniform_int_distribution<int> noise(0, 40);
    for (int y = 0; y < h; ++y)
    {
        if (scenarioPastDeadline())
            return QImage();
        uchar *row = img.scanLine(y);
        const int gy = static_cast<int>(static_cast<double>(y) / h * 255);
        for (int x = 0; x < w; ++x)
        {
            const int gx = static_cast<int>(static_cast<double>(x) / w * 255);
            const int n = noise(rng);
            const int r = std::clamp(gx + n, 0, 255);
            const int g = std::clamp(gy + n, 0, 255);
            const int b = std::clamp((gx + gy) / 2 + n, 0, 255);
            const int o = x * 3;
            row[o] = static_cast<uchar>(r);
            row[o + 1] = static_cast<uchar>(g);
            row[o + 2] = static_cast<uchar>(b);
        }
    }
    return img;
}

// Persist a synthesized frame to a temp file that outlives the call (QTemporaryDir
// is held by the struct, so the file survives until the caller's scope ends).
struct TmpAsset
{
    QTemporaryDir dir;
    std::string path;
    bool ok = false;
};
TmpAsset saveSynthetic(const QImage &img, const char *ext)
{
    TmpAsset t;
    if (!t.dir.isValid())
        return t;
    t.path = std::string(t.dir.path().toUtf8()) + "/s." + ext;
    t.ok = img.save(QString::fromStdString(t.path), ext);
    return t;
}
} // namespace

// B11: 4K JPEG full-res decode latency.
ScenarioResult scenarioDecode4kJpeg()
{
    ScenarioResult r;
    r.name = "B11";
    r.metric = "decode_4k_jpeg_ms";
    const QImage frame = makeSyntheticFrame(3840, 2160, 1);
    if (scenarioPastDeadline())
        return timeoutResult(r.name, r.metric, scenarioDeadlineBudgetMs(), false);
    TmpAsset a = saveSynthetic(frame, "jpg");
    if (!a.ok)
    {
        r.detail = "synthetic 4K image save failed";
        return r;
    }
    std::vector<double> s;
    s.reserve(12);
    for (int i = 0; i < 12; ++i)
    {
        if (failIfScenarioTimedOut(r, false))
            return r;
        const double t0 = nowMs();
        const ImageData d = Decoder::decodeScaled(a.path, 3840);
        const double t1 = nowMs();
        (void)d;
        s.push_back(t1 - t0);
    }
    r.timing = summarize(s);
    r.value = r.timing.p50Ms;
    r.detail = "4K JPEG full-res decode via Decoder::decodeScaled (edge=3840)";
    return r;
}

// B12: 8K frame decode latency. CI ships no RAW sample, so this decodes a
// synthesized 8K RGB frame through the same ImageData pipeline a real RAW would
// feed; point it at a .raw/.dng asset to measure the true RAW decode path.
ScenarioResult scenarioDecode8k()
{
    ScenarioResult r;
    r.name = "B12";
    r.metric = "decode_8k_ms";
    const QImage frame = makeSyntheticFrame(8192, 4320, 2);
    if (scenarioPastDeadline())
        return timeoutResult(r.name, r.metric, scenarioDeadlineBudgetMs(), false);
    TmpAsset a = saveSynthetic(frame, "png");
    if (!a.ok)
    {
        r.detail = "synthetic 8K image save failed";
        return r;
    }
    std::vector<double> s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        if (failIfScenarioTimedOut(r, false))
            return r;
        const double t0 = nowMs();
        const ImageData d = Decoder::decodeScaled(a.path, 8192);
        const double t1 = nowMs();
        (void)d;
        s.push_back(t1 - t0);
    }
    r.timing = summarize(s);
    r.value = r.timing.p50Ms;
    r.detail = "8K frame full-res decode (edge=8192); CI stand-in for 8K RAW";
    return r;
}

// B15: per-frame zoom render cost. Proxies the hot path behind wheel/gesture
// zoom: rescale an 8K frame down to a 1080p viewport.
ScenarioResult scenarioZoomLatency()
{
    ScenarioResult r;
    r.name = "B15";
    r.metric = "zoom_frame_ms";
    QImage big = makeSyntheticFrame(8192, 4320, 3);
    if (scenarioPastDeadline())
        return timeoutResult(r.name, r.metric, scenarioDeadlineBudgetMs(), false);
    QImage view(1920, 1080, QImage::Format_RGB888);
    std::vector<double> s;
    s.reserve(30);
    for (int i = 0; i < 30; ++i)
    {
        if (failIfScenarioTimedOut(r, false))
            return r;
        const double t0 = nowMs();
        QImage z = big.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        view = z.copy();
        const double t1 = nowMs();
        s.push_back(t1 - t0);
    }
    r.timing = summarize(s);
    r.value = r.timing.p50Ms;
    r.detail = "8K->1080p SmoothTransformation rescale per frame (zoom hot-path proxy)";
    return r;
}

// B16: render throughput (fps). Synthesizes a viewport rescale loop (the hot
// path behind continuous wheel/gesture zoom) and reports achieved FPS.
// Report-only (machine dependent) — never blocks CI under --enforce.
ScenarioResult scenarioRenderFps()
{
    ScenarioResult r;
    r.name = "B16";
    r.metric = "render_fps";
    QImage big = makeSyntheticFrame(8000, 6000, 4);
    if (scenarioPastDeadline())
        return timeoutResult(r.name, r.metric, scenarioDeadlineBudgetMs(), true);
    QImage view(1920, 1080, QImage::Format_RGB888);
    const int frames = 120;
    const double t0 = nowMs();
    for (int i = 0; i < frames; ++i)
    {
        if (failIfScenarioTimedOut(r, true))
            return r;
        // Vary the target size slightly to emulate continuous zoom; copy so the
        // resize does real work every frame (worst-case continuous-zoom path).
        const int w = 1280 + (i % 5) * 160;
        QImage z = big.scaled(w, 720, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        view = z.copy();
    }
    const double t1 = nowMs();
    const double elapsed = std::max(t1 - t0, 1e-6);
    const double fps = frames / (elapsed / 1000.0);
    r.value = fps;
    r.passed = true;
    r.detail = "frames=" + std::to_string(frames) + " elapsed_ms=" + std::to_string(elapsed);
    return r;
}

} // namespace mviewer::bench
