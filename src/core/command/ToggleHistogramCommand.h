#pragma once
#include "ICommand.h"
#include <functional>

class ToggleHistogramCommand : public ICommand {
public:
  explicit ToggleHistogramCommand(std::function<void()> onExecute);
  std::string id() const override { return "toggle_histogram"; }
  std::string description() const override { return "切换直方图显示"; }
  void execute() override;
  bool canExecute() const override;
  std::vector<CommandShortcut> shortcuts() const override;

private:
  std::function<void()> m_onExecute;
};
