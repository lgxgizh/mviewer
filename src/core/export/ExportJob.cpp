#include "core/export/ExportJob.h"

#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageTransform.h"

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace mviewer::exportjob
{

std::string modeName(Mode m)
{
    switch (m)
    {
    case Mode::Convert:
        return "convert";
    case Mode::ContactSheet:
        return "contact";
    case Mode::Pdf:
        return "pdf";
    case Mode::Csv:
        return "csv";
    case Mode::Json:
        return "json";
    case Mode::HtmlReport:
        return "html";
    case Mode::Clipboard:
        return "clipboard";
    }
    return "convert";
}

Mode modeFromName(const std::string &name)
{
    if (name == "contact")
        return Mode::ContactSheet;
    if (name == "pdf")
        return Mode::Pdf;
    if (name == "csv")
        return Mode::Csv;
    if (name == "json")
        return Mode::Json;
    if (name == "html")
        return Mode::HtmlReport;
    if (name == "clipboard")
        return Mode::Clipboard;
    return Mode::Convert;
}

static std::string applyRename(const std::string &pattern, const std::string &base,
                               const std::string &ext, int index, int total)
{
    // Minimal {name} / {seq} / {seq:N} substitution (parity with ImageTransform helper).
    return mviewer::core::applyRenamePattern(pattern, base, ext, index, total);
}

static ImageData maybeResize(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.resizeMode == ResizeMode::Fit)
        return mviewer::core::resizeToFit(d, cfg.resizeValue, cfg.resizeValue);
    if (cfg.resizeMode == ResizeMode::Scale)
        return mviewer::core::resizeByFactor(d, cfg.resizeValue / 100.0);
    return d;
}

static ImageData maybeWatermark(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.watermarkText.empty())
        return d;
    const auto pos = static_cast<mviewer::core::WatermarkPosition>(cfg.watermarkPos);
    return mviewer::core::addTextWatermark(d, cfg.watermarkText, pos,
                                           cfg.watermarkOpacity / 100.0, 32);
}

static std::string extensionFor(const std::string &format)
{
    if (format == "jpeg")
        return ".jpg";
    return "." + format;
}

ExportJobResult run(const ExportJobConfig &cfg, ProgressFn progress)
{
    ExportJobResult r;
    r.total = static_cast<int>(cfg.sources.size());

    if (cfg.sources.empty())
    {
        r.message = "no sources";
        return r;
    }
    if (cfg.outDir.empty())
    {
        r.message = "no output directory";
        return r;
    }

    // Convert is the unified path. Other modes are still handled by ExportDialog
    // specialized methods; report them as delegated so callers can fall back.
    if (cfg.mode != Mode::Convert)
    {
        r.message = "delegated:" + modeName(cfg.mode);
        return r;
    }

    std::error_code ec;
    fs::create_directories(cfg.outDir, ec);

    const std::string ext = extensionFor(cfg.format);
    Encoder::Params params;
    params.quality = cfg.quality;

    for (int i = 0; i < r.total; ++i)
    {
        const std::string &src = cfg.sources[static_cast<size_t>(i)];
        if (progress)
            progress(i, r.total, src);

        ImageData data = Decoder::decodeFull(src);
        if (data.isNull())
        {
            ++r.failed;
            continue;
        }
        data = maybeWatermark(maybeResize(data, cfg), cfg);

        fs::path p(src);
        const std::string baseName = p.stem().string();
        const std::string srcExt = p.extension().string();
        std::string outBase = applyRename(cfg.renamePattern, baseName, srcExt, i, r.total);
        if (outBase.empty())
            outBase = baseName;
        // Strip accidental extension from rename result.
        if (outBase.size() > 4 && outBase.find('.') != std::string::npos)
        {
            const auto dot = outBase.find_last_of('.');
            if (dot != std::string::npos)
                outBase = outBase.substr(0, dot);
        }
        const fs::path dst = fs::path(cfg.outDir) / (outBase + ext);
        if (Encoder::encode(data, dst.string(), params))
        {
            ++r.done;
            if (r.primaryOutput.empty())
                r.primaryOutput = dst.string();
        }
        else
        {
            ++r.failed;
        }
    }

    if (progress)
        progress(r.total, r.total, {});

    std::ostringstream oss;
    oss << "done " << r.done << " / " << r.total;
    if (r.failed)
        oss << " (failed " << r.failed << ")";
    r.message = oss.str();
    return r;
}

} // namespace mviewer::exportjob
