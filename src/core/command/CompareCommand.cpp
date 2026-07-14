#include "core/command/CompareCommand.h"

#include <QKeySequence>

CompareCommand::CompareCommand(std::function<void()> onExecute)
    : m_onExecute(std::move(onExecute)) {}

void CompareCommand::execute() {
  if (canExecute() && m_onExecute)
    m_onExecute();
}

bool CompareCommand::canExecute() const { return m_onExecute != nullptr; }

std::vector<CommandShortcut> CompareCommand::shortcuts() const {
  return {{Qt::Key_M, Qt::ControlModifier}};
}
