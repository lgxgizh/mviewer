#include "core/command/ToggleHistogramCommand.h"

#include <QKeySequence>

ToggleHistogramCommand::ToggleHistogramCommand(std::function<void()> onExecute)
    : m_onExecute(std::move(onExecute)) {}

void ToggleHistogramCommand::execute() {
  if (canExecute() && m_onExecute)
    m_onExecute();
}

bool ToggleHistogramCommand::canExecute() const {
  return m_onExecute != nullptr;
}

std::vector<CommandShortcut> ToggleHistogramCommand::shortcuts() const {
  return {{Qt::Key_H, Qt::ControlModifier}};
}
