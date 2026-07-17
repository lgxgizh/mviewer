#pragma once

#include "ICommand.h"

#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"

#include <memory>
#include <string>

class ImageFrame;

// Crop a frame to a rectangular region (domain::Selection). Reversible: the
// pre-crop pixels are captured so undo() restores them exactly. Core-only
// (no Qt). The selection is clamped to the frame bounds by cropRegion().
class CropCommand : public ICommand
{
  public:
    CropCommand(std::shared_ptr<ImageFrame> frame, const mviewer::domain::Selection &region);

    std::string id() const override
    {
        return "crop.region";
    }
    std::string description() const override
    {
        return "Crop to selection";
    }
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return true;
    }
    bool canExecute() const override;

  private:
    std::shared_ptr<ImageFrame> m_frame;
    mviewer::domain::Selection m_region;
    struct Impl
    {
        bool hasSaved = false;
        ImageData saved; // pre-crop pixels for undo
    };
    std::unique_ptr<Impl> m_impl;
};
