#pragma once
#include "ICommand.h"

#include <string>
#include <vector>

// A clipboard paste into the open folder (item 5: undo = delete the copies).
//
// execute() is a no-op because the paste has already written the files by the
// time the command is recorded (see ThumbnailPanel::pasteClipboardIntoCurrent-
// Directory). undo() removes exactly those files, and only while they are still
// inside the folder they were created in — a stale history entry must never
// delete something in a folder the user has since navigated to.
class ClipboardPasteCommand : public ICommand
{
  public:
    ClipboardPasteCommand(std::vector<std::string> created, std::string directory);

    std::string id() const override
    {
        return "clipboard_paste";
    }
    std::string description() const override
    {
        return "Paste into folder";
    }
    void execute() override;
    void undo() override;
    bool canUndo() const override;
    // The paste sources are not retained, so a redo could not recreate the
    // copies; the stack must not offer one.
    bool canRedo() const override
    {
        return false;
    }

  private:
    std::vector<std::string> m_created;
    std::string m_directory;
};
