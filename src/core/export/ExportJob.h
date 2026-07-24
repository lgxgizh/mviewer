// M21: unified Export Job — single config + runner for image/report export.
// Qt-free header. UI (ExportDialog) and BatchProcessor both feed this runner
// so Convert / Contact / CSV / JSON / HTML share one execution path.
#pragma once

#include <functional>
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
    std::string outDir;
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
};

struct ExportJobResult
{
    int done = 0;
    int total = 0;
    int failed = 0;
    std::string message;
    std::string primaryOutput; // e.g. contact sheet path / report path
};

// Progress: (done, total, currentSourcePath)
using ProgressFn = std::function<void(int, int, const std::string &)>;

// Run a job. Convert mode is fully implemented here; other modes may return a
// "delegated" result so the UI can keep its specialized path until fully moved.
ExportJobResult run(const ExportJobConfig &cfg, ProgressFn progress = {});

// Helpers used by ExportDialog / tests.
std::string modeName(Mode m);
Mode modeFromName(const std::string &name);

} // namespace mviewer::exportjob
