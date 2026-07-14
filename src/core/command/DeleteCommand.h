#pragma once
#include "ICommand.h"

#include <functional>

class DeleteCommand : public ICommand
{
public:
    explicit DeleteCommand(std::function<void()> onExecute);
    std::string id() const override { return "delete_image"; }
    std::string description() const override { return "删除到回收站"; }
    void execute() override;
    bool canExecute() const override;
    std::vector<CommandShortcut> shortcuts() const override;

private:
    std::function<void()> m_onExecute;
};
