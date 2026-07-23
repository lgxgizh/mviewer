// M17: Sidecar persistence — per-image .xmp JSON sidecar files that mirror
// RatingStore data (rating, color label, pick, reject) alongside image files.
// Provides portability: moving a folder preserves its metadata.
#pragma once

#include <string>

namespace mviewer::core {

class RatingStore;

class SidecarStore
{
public:
    static SidecarStore &instance();

    // Write sidecar for a single image path (reads data from RatingStore).
    bool writeSidecar(const std::string &imagePath);
    // Read sidecar and populate RatingStore if data differs.
    bool readSidecar(const std::string &imagePath);
    // Delete sidecar file.
    bool removeSidecar(const std::string &imagePath);

    // Bulk import: scan directory for .xmp files and merge into RatingStore.
    // Returns count of merged entries.
    int importDirectory(const std::string &dirPath);
    // Bulk export: write sidecars for all known ratings in a directory.
    // Returns count of written sidecars.
    int exportDirectory(const std::string &dirPath);

    // Get the sidecar file path for an image.
    static std::string sidecarPath(const std::string &imagePath);

private:
    SidecarStore() = default;
    std::string toJson(const std::string &imagePath);
    bool fromJson(const std::string &json, const std::string &imagePath);
};

}  // namespace mviewer::core
