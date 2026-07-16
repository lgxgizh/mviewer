#pragma once

#include "ICommand.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ─── CommandStack ────────────────────────────────────────────────────────────
// Undo/redo history for the unified Command pattern (Architect P1-4). Every
// user action that should be reversible — Rotate, Crop, Compare, Label, Rename,
// Delete — is an ICommand; the stack runs execute() and records it so undo()
// can reverse it. Domain-free (core/command, no Qt).
//
// Bounded: `maxDepth` limits retained history (oldest undos are dropped).
// A change callback lets the UI refresh its Undo/Redo menu state.

class CommandStack
{
public:
    explicit CommandStack(size_t maxDepth = 100)
        : m_maxDepth(maxDepth)
    {
    }

    // Execute a command and push it onto the undo history. Clears the redo
    // stack (a new action invalidates the redo branch). Takes ownership.
    void execute(std::unique_ptr<ICommand> cmd)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!cmd || !cmd->canExecute())
            return;
        cmd->execute();
        m_undo.push_back(std::move(cmd));
        while (m_undo.size() > m_maxDepth)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        notify();
    }

    // Reverse the most recent command. Moves it to the redo stack.
    void undo()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_undo.empty() || !m_undo.back()->canUndo())
            return;
        auto& cmd = m_undo.back();
        cmd->undo();
        m_redo.push_back(std::move(cmd));
        m_undo.pop_back();
        notify();
    }

    // Re-apply the most recently undone command.
    void redo()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_redo.empty())
            return;
        auto& cmd = m_redo.back();
        cmd->execute();
        m_undo.push_back(std::move(cmd));
        m_redo.pop_back();
        notify();
    }

    bool canUndo() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return !m_undo.empty() && m_undo.back()->canUndo();
    }
    bool canRedo() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return !m_redo.empty();
    }

    std::string undoLabel() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_undo.empty() ? std::string{} : m_undo.back()->description();
    }
    std::string redoLabel() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_redo.empty() ? std::string{} : m_redo.back()->description();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_undo.clear();
        m_redo.clear();
        notify();
    }

    void setChangeCallback(std::function<void()> cb) { m_onChange = std::move(cb); }

    size_t undoDepth() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_undo.size();
    }
    size_t redoDepth() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_redo.size();
    }

private:
    void notify()
    {
        if (m_onChange)
            m_onChange();
    }

    mutable std::mutex m_mtx;
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
    size_t m_maxDepth;
    std::function<void()> m_onChange;
};
