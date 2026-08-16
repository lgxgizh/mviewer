#pragma once

#include "core/export/ExportJob.h"

#include <filesystem>
#include <optional>
#include <system_error>

namespace mviewer::exportjob
{

struct ReportRow
{
    std::string name;
    int width = 0;
    int height = 0;
    double lumMean = 0.0;
    double rMean = 0.0;
    double gMean = 0.0;
    double bMean = 0.0;
};

ImageData maybeResize(const ImageData &data, const ExportJobConfig &cfg);
ImageData maybeWatermark(const ImageData &data, const ExportJobConfig &cfg);
std::string extensionFor(const std::string &format);
ImageData decodeSource(const std::string &path, const ExportJobConfig &cfg);
std::optional<ReportRow> analyzeSource(const std::filesystem::path &path);
std::string csvEscape(const std::string &value);
std::string jsonEscape(const std::string &value);
std::string htmlEscape(const std::string &value);
std::string fixedNumber(double value);
std::string jsonNumber(double value);
std::filesystem::path pathFromUtf8(const std::string &value);
std::string pathToUtf8(const std::filesystem::path &path);
std::string outputBaseName(const ExportJobConfig &cfg, const std::filesystem::path &source,
                           int index, int total);
bool writeTextAtomically(const std::filesystem::path &destination, const std::string &contents);
bool writeTextAtomically(const std::filesystem::path &destination, const std::string &contents,
                         const std::function<bool()> &cancelled);
std::filesystem::path uniqueTempPath(const std::filesystem::path &destination);
bool commitTempFile(const std::filesystem::path &temporary,
                    const std::filesystem::path &destination, std::error_code &error);

} // namespace mviewer::exportjob
