#pragma once

#include "ICommand.h"

#include "core/image/ImageBuffer.h"

#include <memory>
#include <string>

class ImageFrame;

// Rotate a frame 90 degrees clockwise. Reversible: the pre-rotation pixels are
// captured so undo() restores them exactly. Core-only (no Qt).
class RotateCommand : public ICommand
{
public:
    explicit RotateCommand(std::shared_ptr<ImageFrame> frame);

    std::string id() const override { return "rotate.cw90"; }
    std::string description() const override { return "Rotate 90° CW"; }
    void execute() override;
    void undo() override;
    bool canUndo() const override { return true; }
    bool canExecute() const override;

private:
    std::shared_ptr<ImageFrame> m_frame;
    struct Impl
    {
        bool hasSaved = false;
        ImageData saved; // pre-rotation pixels for undo
    };
    std::unique_ptr<Impl> m_impl;
};
