#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerResult.h"
#include "core/analyzer/BlurAnalyzer.h"
#include "core/analyzer/BrightnessAnalyzer.h"
#include "core/analyzer/ColorCastAnalyzer.h"
#include "core/analyzer/ColorCheckerAnalyzer.h"
#include "core/analyzer/ContrastAnalyzer.h"
#include "core/analyzer/DeadPixelAnalyzer.h"
#include "core/analyzer/EntropyAnalyzer.h"
#include "core/analyzer/ExposureAnalyzer.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/analyzer/MTFAnalyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"
#include "core/analyzer/SharpnessAnalyzer.h"
#include "core/scheduler/TaskScheduler.h"

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

AnalyzerRegistry &registrySingleton()
{
    static AnalyzerRegistry inst;
    return inst;
}

} // namespace

AnalyzerRegistry &AnalyzerRegistry::instance()
{
    AnalyzerRegistry &inst = registrySingleton();
    Analyzer::registerBuiltins();
    return inst;
}

void Analyzer::registerBuiltins()
{
    // instance() calls this, and this calls back into the singleton. The
    // callback must not re-enter registerBuiltins (call_once is not recursive).
    static std::once_flag once;
    std::call_once(
        once,
        []
        {
            AnalyzerRegistry &reg = registrySingleton();
            reg.registerAnalyzer("histogram",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new HistogramAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("noise",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new NoiseAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("entropy",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new EntropyAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("psnr",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new PSNRAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("rgbmean",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new RGBMeanAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("sharpness",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new SharpnessAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("ssim",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new SSIMAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            // M13: analyzer extensions (MTF / Dead Pixel / ColorChecker).
            reg.registerAnalyzer("mtf",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new MTFAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("deadpixel",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new DeadPixelAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("colorchecker",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new ColorCheckerAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            // M15: analyzer extensions (Brightness/Contrast/Blur/ColorCast/Exposure).
            reg.registerAnalyzer("brightness",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new BrightnessAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("contrast",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new ContrastAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("blur",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new BlurAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("colorcast",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new ColorCastAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
            reg.registerAnalyzer("exposure",
                                 []() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
                                 {
                                     return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                         new ExposureAnalyzer(), [](Analyzer *p) { delete p; });
                                 });
        });
}

void AnalyzerRegistry::registerAnalyzer(const std::string &id, AnalyzerCreator creator)
{
    std::lock_guard<std::mutex> lock(m_factoriesMtx);
    m_factories[id] = std::move(creator);
}

void AnalyzerRegistry::unregister(const std::string &id)
{
    std::lock_guard<std::mutex> lock(m_factoriesMtx);
    m_factories.erase(id);
}

std::unique_ptr<Analyzer, AnalyzerDeleter> AnalyzerRegistry::create(const std::string &id) const
{
    AnalyzerCreator creator;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        auto it = m_factories.find(id);
        if (it == m_factories.end())
            return nullptr;
        creator = it->second;
    }
    return creator ? creator() : nullptr;
}

std::vector<std::string> AnalyzerRegistry::availableAnalyzers() const
{
    std::lock_guard<std::mutex> lock(m_factoriesMtx);
    std::vector<std::string> out;
    out.reserve(m_factories.size());
    for (const auto &kv : m_factories)
        out.push_back(kv.first);
    return out;
}

std::unordered_map<std::string, std::string>
AnalyzerRegistry::runAnalyzer(const ImageFrame &frame) const
{
    // M24 (C#7): run all registered analyzers in parallel on the SHARED,
    // bounded TaskScheduler AnalysisPool instead of one std::async thread per
    // analyzer. The old spawn-per-analyzer approach is unbounded (a registry
    // with many plugin analyzers creates that many threads) and proved fragile
    // in this build (fail-fast past ~13 concurrent tasks, 0xc0000409). The
    // frame is const and analyzers are read-only; each task creates its own
    // analyzer instance, so no locks are needed inside analyzers.
    //
    // Wait only for the tasks this call submitted. AnalysisPool is shared with
    // diff/ROI batches; draining the whole pool would block on unrelated work.
    // Timeout semantics match the old drain: 10 s, then cancel our tasks and
    // wait another 2 s. State is on the heap so a task that outlives the wait
    // cannot write into destroyed locals.
    struct BatchState
    {
        std::mutex mtx;
        std::condition_variable cv;
        size_t remaining = 0;
        std::unordered_map<std::string, std::string> results;
    };
    auto state = std::make_shared<BatchState>();
    auto frameSnapshot = std::make_shared<ImageFrame>(frame);

    std::vector<std::pair<std::string, AnalyzerCreator>> jobs;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        jobs.reserve(m_factories.size());
        for (const auto &kv : m_factories)
            jobs.emplace_back(kv.first, kv.second);
    }
    if (jobs.empty())
        return {};

    TaskScheduler &sched = TaskScheduler::instance();
    state->remaining = jobs.size();
    std::vector<TaskScheduler::TaskHandle> handles;
    handles.reserve(jobs.size());
    size_t rejected = 0;
    for (const auto &job : jobs)
    {
        const std::string id = job.first;
        const AnalyzerCreator factory = job.second;
        auto handle = sched.submit(
            TaskScheduler::AnalysisPool,
            [state, id, factory, frameSnapshot]()
            {
                std::string text;
                try
                {
                    auto analyzer = factory ? factory() : nullptr;
                    if (analyzer && analyzer->analyze(*frameSnapshot))
                        text = analyzer->resultText();
                }
                catch (const std::exception &error)
                {
                    // M24 (C#7): a throwing analyzer (e.g. a buggy plugin) must
                    // be isolated — the whole batch must not crash. Reported
                    // rather than swallowed: an invisible failure here looks
                    // like "no result".
                    qWarning("AnalyzerRegistry: analyzer '%s' threw: %s", id.c_str(), error.what());
                }
                catch (...)
                {
                    qWarning("AnalyzerRegistry: analyzer '%s' threw an unknown exception",
                             id.c_str());
                }
                std::lock_guard<std::mutex> lk(state->mtx);
                if (!text.empty())
                    state->results[id] = std::move(text);
                if (state->remaining > 0)
                    --state->remaining;
                if (state->remaining == 0)
                    state->cv.notify_all();
            });
        if (handle)
            handles.push_back(std::move(handle));
        else
            ++rejected;
    }
    if (rejected > 0)
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        if (state->remaining >= rejected)
            state->remaining -= rejected;
        else
            state->remaining = 0;
        if (state->remaining == 0)
            state->cv.notify_all();
    }

    {
        std::unique_lock<std::mutex> lk(state->mtx);
        if (!state->cv.wait_for(lk, std::chrono::seconds(10),
                                [&] { return state->remaining == 0; }))
        {
            lk.unlock();
            for (const auto &handle : handles)
            {
                if (handle)
                    TaskScheduler::cancelTree(handle->id);
            }
            lk.lock();
            state->cv.wait_for(lk, std::chrono::seconds(2), [&] { return state->remaining == 0; });
        }
        return state->results;
    }
}

std::vector<mviewer::analyzer::AnalyzerResult> AnalyzerRegistry::runBatch(
    const std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> &frames,
    const std::string &id) const
{
    AnalyzerCreator creator;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        auto it = m_factories.find(id);
        if (it == m_factories.end())
            return {};
        creator = it->second;
    }

    std::vector<mviewer::analyzer::AnalyzerResult> out;
    out.reserve(frames.size());
    for (const auto &[filename, frame] : frames)
    {
        if (!frame || frame->pixels().isNull())
            continue;
        auto analyzer = creator ? creator() : nullptr;
        if (!analyzer || !analyzer->analyze(*frame))
            continue;
        out.push_back(mviewer::analyzer::AnalyzerResult{filename, analyzer->resultMetrics(),
                                                        analyzer->resultText()});
    }
    return out;
}

AnalyzerCapability AnalyzerRegistry::capabilitiesOf(const std::string &id) const
{
    AnalyzerCreator creator;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        auto it = m_factories.find(id);
        if (it == m_factories.end())
            return AnalyzerCapability::None;
        creator = it->second;
    }
    auto instance = creator ? creator() : nullptr;
    if (!instance)
        return AnalyzerCapability::None;
    return instance->capabilities();
}

std::optional<AnalyzerInfo> AnalyzerRegistry::infoFor(const std::string &id) const
{
    AnalyzerCreator creator;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        auto it = m_factories.find(id);
        if (it == m_factories.end())
            return std::nullopt;
        creator = it->second;
    }
    auto instance = creator ? creator() : nullptr;
    if (!instance)
        return std::nullopt;
    return instance->info();
}

std::vector<std::string> AnalyzerRegistry::queryByCapability(AnalyzerCapability required) const
{
    std::vector<std::pair<std::string, AnalyzerCreator>> jobs;
    {
        std::lock_guard<std::mutex> lock(m_factoriesMtx);
        jobs.reserve(m_factories.size());
        for (const auto &kv : m_factories)
            jobs.emplace_back(kv.first, kv.second);
    }
    std::vector<std::string> result;
    for (const auto &[id, factory] : jobs)
    {
        auto instance = factory ? factory() : nullptr;
        if (!instance)
            continue;
        if (required == AnalyzerCapability::None ||
            hasCapability(instance->capabilities(), required))
        {
            result.push_back(id);
        }
    }
    return result;
}

namespace mviewer::analyzer
{

std::vector<AnalyzerResult>
runBatch(const std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> &frames,
         const std::string &analyzerId)
{
    return AnalyzerRegistry::instance().runBatch(frames, analyzerId);
}

} // namespace mviewer::analyzer
