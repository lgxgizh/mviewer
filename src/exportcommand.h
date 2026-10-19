#pragma once

#include "core/command/ICommand.h"

class QWidget;

class ExportCommand : public ICommand
{
  public:
    explicit ExportCommand(QWidget *parent);

    std::string id() const override
    {
        return "export";
    }
    std::string description() const override
    {
        return "Export Images";
    }
    void execute() override;
    bool canExecute() const override;
    std::vector<CommandShortcut> shortcuts() const override;

  private:
    QWidget *m_parent = nullptr;
};
