#pragma once

#include "ICommand.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry {
public:
  static CommandRegistry &instance();

  void registerCommand(std::unique_ptr<ICommand> cmd);
  void unregisterCommand(const std::string &id);

  // Execute by id
  bool execute(const std::string &id);

  // Find command by key shortcut
  ICommand *findByShortcut(int key, int mods) const;

  // List all registered commands
  std::vector<ICommand *> allCommands() const;

  // Iterator access
  auto begin() const { return m_order.begin(); }
  auto end() const { return m_order.end(); }

private:
  std::unordered_map<std::string, std::unique_ptr<ICommand>> m_commands;
  std::vector<std::string> m_order; // insertion order
};
