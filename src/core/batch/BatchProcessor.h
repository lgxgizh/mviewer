#pragma once

#include "domain/BatchJob.h"

#include <atomic>
#include <functional>
#include <string>

namespace mviewer::core
{

// BatchProcessor executes a BatchJobConfig over a list of image files.
// For each file it: decodes → applies ordered operations (resize, watermark,
// analyze, rename, export) → collects results. Progress is reported via
// callback so the UI layer can update a progress bar.
//
// Header is Qt-free; the .cpp may use Qt (Decoder, ImageTransform, etc.).
class BatchProcessor
{
  public:
    // Progress callback: (currentFileIndex, totalFiles, currentFilePath).
    using ProgressCallback = std::function<void(int, int, const std::string &)>;

    BatchProcessor() = default;

    // Set a progress callback (called before each file is processed).
    void setProgressCallback(ProgressCallback cb)
    {
        m_progressCb = std::move(cb);
    }

    // Execute the batch job synchronously. Returns aggregated results.
    // If cancelled (via requestCancel()), stops after the current file.
    domain::BatchJobResult execute(const domain::BatchJobConfig &config);

    // Request cancellation (thread-safe). The current file finishes, then
    // the run stops.
    void requestCancel()
    {
        m_cancelled.store(true);
    }

    // Check whether cancellation was requested.
    bool isCancelled() const
    {
        return m_cancelled.load();
    }

  private:
    ProgressCallback m_progressCb;
    std::atomic<bool> m_cancelled{false};

    // Process a single file through the configured operation pipeline.
    domain::BatchFileResult processFile(const domain::BatchJobConfig &config,
                                        const std::string &inputPath, int fileIndex,
                                        int totalFiles);
};

} // namespace mviewer::core
