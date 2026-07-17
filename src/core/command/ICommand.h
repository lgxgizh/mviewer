#pragma once

#include <functional>
#include <string>
#include <vector>

struct CommandShortcut
{
    int key;  // Qt::Key enum int value
    int mods; // Qt::KeyboardModifier flags (0 = no modifier)
};

// Base class for all user commands. Commands expose execute() AND undo() so a
// CommandStack can provide undo/redo history (Architect P1-4). Legacy commands
// that are not reversible simply leave undo() a no-op and canUndo()==false.
class ICommand
{
  public:
    virtual ~ICommand() = default;
    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual void execute() = 0;

    // Reverse execute(). Default: not reversible.
    virtual void undo()
    {
    }
    virtual bool canUndo() const
    {
        return false;
    }

    virtual bool canExecute() const
    {
        return true;
    }
    virtual std::vector<CommandShortcut> shortcuts() const
    {
        return {};
    }
};
