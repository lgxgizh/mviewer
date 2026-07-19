#pragma once

#include "core/command/ICommand.h"

#include <functional>
#include <string>
#include <vector>

// Generic callback-backed command. Used for UI-bound shortcuts (navigation,
// preview, fullscreen) that delegate to an existing MainWindow method via a
// captured lambda -- avoids one header/cpp per trivial shortcut. Mirrors the
// callback style of OpenDirectoryCommand but carries an explicit id,
// description, and shortcut list so it integrates with CommandRegistry.
class CallbackCommand : public ICommand
{
  public:
    CallbackCommand(std::string id, std::string description, std::function<void()> onExecute,
                    std::vector<CommandShortcut> shortcuts = {})
        : m_id(std::move(id)), m_description(std::move(description)),
          m_onExecute(std::move(onExecute)), m_shortcuts(std::move(shortcuts))
    {
    }

    std::string id() const override
    {
        return m_id;
    }
    std::string description() const override
    {
        return m_description;
    }
    void execute() override
    {
        if (canExecute() && m_onExecute)
            m_onExecute();
    }
    bool canExecute() const override
    {
        return m_onExecute != nullptr;
    }
    std::vector<CommandShortcut> shortcuts() const override
    {
        return m_shortcuts;
    }

  private:
    std::string m_id;
    std::string m_description;
    std::function<void()> m_onExecute;
    std::vector<CommandShortcut> m_shortcuts;
};
