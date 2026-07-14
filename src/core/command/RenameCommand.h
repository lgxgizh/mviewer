#pragma once
#include "ICommand.h"
#include <functional>

class RenameCommand : public ICommand {
public:
  explicit RenameCommand(std::function<void()> onExecute);
  std::string id() const override { return "rename_image"; }
  std::string description() const override { return "重命名..."; }
  void execute() override;
  bool canExecute() const override;
  std::vector<CommandShortcut> shortcuts() const override;

private:
  std::function<void()> m_onExecute;
};
