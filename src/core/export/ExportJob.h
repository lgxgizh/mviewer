// M21: unified Export Job — single config + runner for image/report export.
// Qt-free header. UI (ExportDialog) and BatchProcessor both feed this runner
// so Convert / Contact / CSV / JSON / HTML share one execution path.
#pragma once

#include "core/image/ImageBuffer.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mviewer::exportjob
{

enum class Mode
{
    Convert = 0,
    ContactSheet,
    Pdf,
    Csv,
    Json,
    HtmlReport,
    Clipboard
};

enum class ResizeMode
{
    None = 0,
    Fit,
    Scale
};

struct ExportJobConfig
{
    Mode mode = Mode::Convert;
    std::vector<std::string> sources;
    // Optional legacy batch source directory. Enumeration belongs to the
    // worker-side ExportJob, never to the Qt dialog thread.
    std::string sourceDirectory;
    std::string outDir;
    // Optional explicit destination for a single-image Convert request. This
    // keeps Save As on the same worker-side runner without making the UI
    // synthesize a rename pattern or encode pixels itself.
    std::string destinationPath;
    std::string format = "jpeg"; // jpeg / png / webp / tiff / bmp
    int quality = 90;
    ResizeMode resizeMode = ResizeMode::None;
    int resizeValue = 1920; // fit: max edge; scale: percent
    std::string watermarkText;
    int watermarkPos = 3; // 0=tl,1=tr,2=bl,3=br,4=center,5=tile
    int watermarkOpacity = 40;
    std::string renamePattern = "{name}_{seq:3}";
    int contactCols = 4;
    int contactThumb = 200;

    // P0 #⑦: crop applied before encode (disabled when cropEnabled is false).
    bool cropEnabled = false;
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    // P0 #⑦: strip metadata. Re-encoding from raw pixels already drops EXIF/ICC,
    // so this is effectively always-on; the flag records explicit user intent.
    bool stripMetadata = true;

    // Viewer Copy/Save uses the same display/ICC materialization as the
    // rendered frame. Ordinary batch conversion retains its historical raw
    // decode contract unless this flag is explicitly enabled.
    bool preserveDisplayAppearance = false;

    // Contact/PDF staging contract. The writer API is currently batch-shaped,
    // so bound scaled staging rather than allowing source-count-linear memory
    // growth without a hard limit.
    std::size_t stagingMemoryBudgetBytes = 512ULL * 1024ULL * 1024ULL;

    // M24 (D#3): optional cancellation token. run() checks it between items and
    // stops early, reporting what already completed. Never dereferenced if null.
    std::shared_ptr<std::atomic<bool>> cancel;
};

struct ExportJobResult
{
    int done = 0;
    int total = 0;
    int failed = 0;
    std::string message;
    std::string primaryOutput; // e.g. contact sheet path / report path
    // Clipboard ownership is transferred as a value-owned core image. The
    // final QClipboard::setImage() remains a GUI-thread operation.
    ImageData clipboardImage;
};

// Progress: (done, total, currentSourcePath)
using ProgressFn = std::function<void(int, int, const std::string &)>;

// Run a job. Every mode is executed by this worker-side runner; UI callers only
// submit the config and present the value-owned result.
ExportJobResult run(const ExportJobConfig &cfg, ProgressFn progress = {});

// Shared atomic text writer for report-like UI entry points that already have
// a value-owned snapshot. It uses the same temporary-file and final-commit
// contract as run().
bool writeTextAtomically(const std::string &destination, const std::string &contents);

// Helpers used by ExportDialog / tests.
std::string modeName(Mode m);
Mode modeFromName(const std::string &name);

} // namespace mviewer::exportjob
