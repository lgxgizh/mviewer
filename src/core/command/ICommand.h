#pragma once

#include <functional>
#include <string>
#include <vector>

struct CommandShortcut {
    int key; // Qt::Key enum int value
    int mods; // Qt::KeyboardModifier flags (0 = no modifier)
};

// Base class for all user commands
class ICommand
{
public:
    virtual ~ICommand() = default;
    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual void execute() = 0;
    virtual bool canExecute() const { return true; }
    virtual std::vector<CommandShortcut> shortcuts() const { return {}; }
};
