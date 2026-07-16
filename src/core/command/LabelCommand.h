#pragma once

#include "ICommand.h"

#include <memory>
#include <string>

class ImageFrame;

// Add or remove a tag (label) on a frame. Reversible: undo() reverses the
// add/remove. Core-only (no Qt). Useful for the "Label" action in compare /
// album workflows (Architect P1-4).
class LabelCommand : public ICommand
{
public:
    enum class Mode
    {
        Add,
        Remove
    };
    LabelCommand(std::shared_ptr<ImageFrame> frame, std::string tag, Mode mode);

    std::string id() const override { return "label.set"; }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override { return true; }

private:
    std::shared_ptr<ImageFrame> m_frame;
    std::string m_tag;
    Mode m_mode;
};
