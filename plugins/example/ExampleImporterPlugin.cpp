// Example Importer plugin (A-9.3).
//
// Demonstrates the Importer plugin contract: implement IImporter, export the
// frozen ABI triple plus createImporter/destroyImporter/pluginName, and
// PluginManager registers the instance into ImporterRegistry.
//
// This example imports a plain directory as a single-folder Workspace by
// scanning common image extensions (no recursive walk — keeps the sample small).

#include "core/import/IImporter.h"
#include "core/plugin/PluginABI.h"
#include "domain/Image.h"
#include "domain/Workspace.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#ifndef MVIEWER_PLUGIN_EXPORT
#ifdef _WIN32
#define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MVIEWER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#endif

namespace fs = std::filesystem;

namespace
{
const char *kImageExts[] = {".jpg",  ".jpeg", ".png", ".bmp", ".tif", ".tiff",
                            ".webp", ".gif",  ".ppm", ".tga", ".ico", nullptr};

bool isImageExt(const fs::path &p)
{
    const auto ext = p.extension().string();
    std::string lower;
    lower.reserve(ext.size());
    for (char c : ext)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (const char **e = kImageExts; *e; ++e)
        if (lower == *e)
            return true;
    return false;
}
} // namespace

class FolderImporter : public IImporter
{
  public:
    std::string name() const override
    {
        return "folder-importer";
    }

    std::string description() const override
    {
        return "Example folder importer — scans a directory for images";
    }

    std::vector<std::string> extensions() const override
    {
        // Empty = any path / directory.
        return {};
    }

    bool canImport(const std::string &path) const override
    {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }

    mviewer::domain::Workspace importWorkspace(const std::string &path) const override
    {
        mviewer::domain::Workspace ws;
        std::error_code ec;
        if (!fs::is_directory(path, ec))
            return ws;

        ws.rootPath = path;
        mviewer::domain::Folder folder;
        folder.path = path;
        folder.name = fs::path(path).filename().string();
        if (folder.name.empty())
            folder.name = path;

        for (const auto &entry : fs::directory_iterator(path, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec))
                continue;
            if (!isImageExt(entry.path()))
                continue;
            mviewer::domain::ImageMetadata meta;
            meta.fileName = entry.path().filename().string();
            meta.filePath = entry.path().string();
            meta.fileSize = static_cast<int64_t>(entry.file_size(ec));
            folder.imageSet.images.push_back(std::move(meta));
        }
        folder.imageSet.folderPath = path;
        ws.folders.push_back(std::move(folder));
        return ws;
    }
};

extern "C" {

MVIEWER_PLUGIN_EXPORT IImporter *createImporter()
{
    return new FolderImporter();
}

MVIEWER_PLUGIN_EXPORT void destroyImporter(IImporter *p)
{
    delete p;
}

MVIEWER_PLUGIN_EXPORT const char *pluginName()
{
    return "folder-importer";
}

MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
{
    static const PluginABI abi;
    return &abi;
}

} // extern "C"
