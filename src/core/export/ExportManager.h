// M17: Unified Export Manager — single entry point for all export operations.
// Coordinates ExportDialog, BatchDialog, and ExportReport through a common
// API with preset support, filtered-set export, and progress tracking.
#pragma once

#include "core/image/ImageBuffer.h"

#include <functional>
#include <string>
#include <vector>

// Qt-free header usable from core layer.
// The UI layer (MainWindow) owns the ExportDialog/BatchDialog lifecycle;
// ExportManager provides the orchestration logic.

class ExportManager
{
  public:
    // Preset — serializable export configuration.
    struct Preset
    {
        std::string name;
        std::string outDir;
        std::string format = "jpeg";
        int quality = 90;
        std::string resizeMode; // "none", "fit", "scale"
        int resizeValue = 1920;
        std::string watermarkText;
        int watermarkPos = 0; // 0=tl,1=tr,2=bl,3=br,4=center,5=tile
        int watermarkOpacity = 40;
        std::string renamePattern = "{name}_{seq:3}";
        int cols = 4;
        int thumbSize = 200;

        std::string toJson() const;
        static Preset fromJson(const std::string &json);
    };

    static ExportManager &instance();

    // Presets
    bool savePreset(const Preset &p);
    bool deletePreset(const std::string &name);
    std::vector<Preset> listPresets() const;
    Preset loadPreset(const std::string &name) const;

    // Resolve all presets from the default location.
    void loadAllPresets();
    void saveAllPresets();

    // Progress callback: void(int done, int total, const std::string &currentFile)
    using ProgressFn = std::function<void(int, int, const std::string &)>;

    // Path helpers
    static std::string defaultPresetDir();
    static std::string presetPath(const std::string &name);

  private:
    ExportManager() = default;
    std::vector<Preset> m_presets;
};
