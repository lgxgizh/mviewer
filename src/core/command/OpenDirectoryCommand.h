#pragma once
#include "ICommand.h"

#include <functional>

class OpenDirectoryCommand : public ICommand
{
  public:
    explicit OpenDirectoryCommand(std::function<void()> onExecute);
    std::string id() const override
    {
        return "open_directory";
    }
    std::string description() const override
    {
        return "Open folder...";
    }
    void execute() override;
    bool canExecute() const override;
    std::vector<CommandShortcut> shortcuts() const override;

  private:
    std::function<void()> m_onExecute;
};
