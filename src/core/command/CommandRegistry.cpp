#include "core/command/CommandRegistry.h"

#include <algorithm>

CommandRegistry &CommandRegistry::instance()
{
    static CommandRegistry inst;
    return inst;
}

void CommandRegistry::registerCommand(std::unique_ptr<ICommand> cmd)
{
    if (!cmd)
        return;
    const std::string id = cmd->id();
    if (m_commands.find(id) == m_commands.end())
        m_order.push_back(id);
    m_commands[id] = std::move(cmd);
}

void CommandRegistry::unregisterCommand(const std::string &id)
{
    m_commands.erase(id);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
}

bool CommandRegistry::execute(const std::string &id)
{
    auto it = m_commands.find(id);
    if (it == m_commands.end())
        return false;
    it->second->execute();
    return true;
}

ICommand *CommandRegistry::findByShortcut(int key, int mods) const
{
    for (const auto &id : m_order) {
        const auto *cmd = m_commands.at(id).get();
        for (const auto &sc : cmd->shortcuts()) {
            if (sc.key == key && sc.mods == mods)
                return const_cast<ICommand *>(cmd);
        }
    }
    return nullptr;
}

std::vector<ICommand *> CommandRegistry::allCommands() const
{
    std::vector<ICommand *> out;
    out.reserve(m_order.size());
    for (const auto &id : m_order)
        out.push_back(m_commands.at(id).get());
    return out;
}
