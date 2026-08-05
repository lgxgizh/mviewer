#include "core/export/ExportJob.h"

#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageTransform.h"
#include "domain/Selection.h"

#include <cctype>
#include <filesystem>
#include <sstream>
#include <unordered_set>

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
    return mviewer::core::addTextWatermark(d, cfg.watermarkText, pos, cfg.watermarkOpacity / 100.0,
                                           32);
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
    // M24 (D#2): validate the requested format up front — an unknown format id
    // must fail loudly instead of silently encoding a different format.
    static const std::unordered_set<std::string> kKnownFormats = {
        "jpeg", "jpg", "png", "webp", "tiff", "tif", "bmp"};
    if (!kKnownFormats.count(cfg.format))
    {
        r.failed = r.total;
        r.message = "unsupported format: " + cfg.format;
        return r;
    }
    Encoder::Params params;
    params.quality = cfg.quality;

    for (int i = 0; i < r.total; ++i)
    {
        const std::string &src = cfg.sources[static_cast<size_t>(i)];
        // M24 (D#3): cooperative cancellation between items.
        if (cfg.cancel && cfg.cancel->load(std::memory_order_relaxed))
        {
            r.message = "cancelled after " + std::to_string(r.done) + " / " +
                        std::to_string(r.total);
            return r;
        }
        if (progress)
            progress(i, r.total, src);

        ImageData data = Decoder::decodeFull(src);
        if (data.isNull())
        {
            ++r.failed;
            continue;
        }
        data = maybeResize(data, cfg);
        if (cfg.cropEnabled && cfg.cropW > 0 && cfg.cropH > 0)
            data = cropRegion(
                data, mviewer::domain::Selection{cfg.cropX, cfg.cropY, cfg.cropW, cfg.cropH});
        data = maybeWatermark(data, cfg);

        fs::path p(src);
        const std::string baseName = p.stem().string();
        // applyRenamePattern expects extension without the leading dot.
        std::string srcExt = p.extension().string();
        if (!srcExt.empty() && srcExt.front() == '.')
            srcExt.erase(srcExt.begin());
        std::string outBase = applyRename(cfg.renamePattern, baseName, srcExt, i, r.total);
        if (outBase.empty())
            outBase = baseName;
        // Only strip a trailing image extension if the rename pattern accidentally
        // re-introduced one (e.g. "{name}.{ext}"). Do NOT strip dots inside the
        // base name (photo.v2 → photo would be wrong).
        {
            static const char *kImgExts[] = {".jpg", ".jpeg", ".png", ".webp", ".tif",  ".tiff",
                                             ".bmp", ".gif",  ".jp2", ".jxl",  ".heic", ".avif"};
            for (const char *e : kImgExts)
            {
                const size_t elen = std::char_traits<char>::length(e);
                if (outBase.size() > elen)
                {
                    const std::string tail = outBase.substr(outBase.size() - elen);
                    // case-insensitive compare
                    bool match = true;
                    for (size_t k = 0; k < elen; ++k)
                    {
                        const char a =
                            static_cast<char>(std::tolower(static_cast<unsigned char>(tail[k])));
                        const char b =
                            static_cast<char>(std::tolower(static_cast<unsigned char>(e[k])));
                        if (a != b)
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                    {
                        outBase.resize(outBase.size() - elen);
                        break;
                    }
                }
            }
        }
        const fs::path dst = fs::path(cfg.outDir) / (outBase + ext);
        // M24 (D#8): encode to a temp file and atomically rename into place, so
        // an interrupted/failed encode never leaves a partial file at the final
        // name that looks like a successful export. The temp name keeps the
        // REAL extension (".mviewer-tmp-<dst>") because Encoder::encode derives
        // the format from the file suffix.
        const fs::path tmp = dst.parent_path() / (".mviewer-tmp-" + dst.filename().string());
        if (Encoder::encode(data, tmp.string(), params))
        {
            std::error_code tec;
            fs::rename(tmp, dst, tec);
            if (tec)
            {
                // Windows rename does not overwrite an existing target.
                fs::remove(dst, tec);
                fs::rename(tmp, dst, tec);
            }
            if (tec)
            {
                fs::remove(tmp, tec);
                ++r.failed;
            }
            else
            {
                ++r.done;
                if (r.primaryOutput.empty())
                    r.primaryOutput = dst.string();
            }
        }
        else
        {
            fs::remove(tmp, ec); // best-effort cleanup of the partial temp file
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
