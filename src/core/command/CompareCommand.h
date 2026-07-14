#pragma once
#include "ICommand.h"

#include <functional>

class CompareCommand : public ICommand
{
public:
    explicit CompareCommand(std::function<void()> onExecute);
    std::string id() const override { return "compare_images"; }
    std::string description() const override { return "比较选中的图片..."; }
    void execute() override;
    bool canExecute() const override;
    std::vector<CommandShortcut> shortcuts() const override;

private:
    std::function<void()> m_onExecute;
};
