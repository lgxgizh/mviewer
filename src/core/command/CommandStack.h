#pragma once

#include "ICommand.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// ─── CommandStack ────────────────────────────────────────────────────────────
// Undo/redo history for the unified Command pattern (Architect P1-4). Every
// user action that should be reversible — Rotate, Crop, Compare, Label, Rename,
// Delete — is an ICommand; the stack runs execute() and records it so undo()
// can reverse it. Domain-free (core/command, no Qt).
//
// Bounded: `maxDepth` limits retained history (oldest undos are dropped).
// A change callback lets the UI refresh its Undo/Redo menu state. The callback
// is invoked synchronously on the thread that completed the mutating call, but
// never while the internal state mutex is held. UI-owned stacks must therefore
// be mutated on the UI thread; worker code must marshal its final state
// transition to the UI thread before calling execute/undo/redo/clear.

class CommandStack
{
  public:
    explicit CommandStack(size_t maxDepth = 100) : m_maxDepth(maxDepth)
    {
    }

    // Execute a command and push it onto the undo history. Clears the redo
    // stack (a new action invalidates the redo branch). Takes ownership.
    // Returns false if the command reported an error; the command is then NOT
    // added to the undo history so it cannot be half-undone.
    bool execute(std::unique_ptr<ICommand> cmd)
    {
        std::unique_lock<std::mutex> operationLock(m_operationMtx);
        if (!cmd)
        {
            const auto callback = setErrorAndGetCallback("Command cannot be executed.");
            operationLock.unlock();
            invokeCallback(callback);
            return false;
        }

        bool executable = false;
        try
        {
            executable = cmd->canExecute();
        }
        catch (...)
        {
            executable = false;
        }
        if (!executable)
        {
            const auto callback = setErrorAndGetCallback("Command cannot be executed.");
            operationLock.unlock();
            invokeCallback(callback);
            return false;
        }

        std::string error;
        try
        {
            // ICommand::execute() may perform real filesystem work. It is
            // serialized against other stack mutations, but deliberately does
            // not hold m_mtx, so readers and callbacks cannot self-deadlock.
            cmd->execute();
            error = cmd->lastError();
        }
        catch (...)
        {
            error = "Command execution threw an exception.";
        }

        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            m_lastError = error;
            if (error.empty())
            {
                m_undo.push_back(std::move(cmd));
                trimUndoLocked();
                m_redo.clear();
            }
            else if (cmd && cmd->hasUnresolvedState())
            {
                // Preserve a command that still owns recoverable disk state.
                m_undo.push_back(std::move(cmd));
                trimUndoLocked();
                m_redo.clear();
            }
            callback = m_onChange;
        }
        operationLock.unlock();
        invokeCallback(callback);
        return error.empty();
    }

    // Record a command whose execute() already completed on a worker. The
    // caller must marshal this call to the owner/UI thread; this method only
    // performs the short history transition and never runs ICommand code.
    // Failed commands are retained only when they report unresolved state.
    bool recordExecuted(std::unique_ptr<ICommand> cmd)
    {
        std::unique_lock<std::mutex> operationLock(m_operationMtx);
        if (!cmd)
        {
            const auto callback = setErrorAndGetCallback("Command cannot be recorded.");
            operationLock.unlock();
            invokeCallback(callback);
            return false;
        }

        std::string error;
        try
        {
            error = cmd->lastError();
        }
        catch (...)
        {
            error = "Command state inspection threw an exception.";
        }

        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            m_lastError = error;
            if (error.empty() || cmd->hasUnresolvedState())
            {
                m_undo.push_back(std::move(cmd));
                trimUndoLocked();
                m_redo.clear();
            }
            callback = m_onChange;
        }
        operationLock.unlock();
        invokeCallback(callback);
        return error.empty();
    }

    // Reverse the most recent command. Moves it to the redo stack on success.
    bool undo()
    {
        std::unique_lock<std::mutex> operationLock(m_operationMtx);
        std::unique_ptr<ICommand> cmd;
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            if (m_undo.empty() || !m_undo.back()->canUndo())
            {
                m_lastError = "Nothing to undo.";
                callback = m_onChange;
            }
            else
            {
                // Remove the command while it runs so getters never inspect a
                // command whose internal state is being changed by undo().
                cmd = std::move(m_undo.back());
                m_undo.pop_back();
            }
        }

        if (!cmd)
        {
            operationLock.unlock();
            invokeCallback(callback);
            return false;
        }

        std::string error;
        try
        {
            cmd->undo();
            error = cmd->lastError();
        }
        catch (...)
        {
            error = "Command undo threw an exception.";
        }

        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            m_lastError = error;
            if (error.empty())
                m_redo.push_back(std::move(cmd));
            else
                m_undo.push_back(std::move(cmd));
            callback = m_onChange;
        }
        operationLock.unlock();
        invokeCallback(callback);
        return error.empty();
    }

    // Re-apply the most recently undone command.
    bool redo()
    {
        std::unique_lock<std::mutex> operationLock(m_operationMtx);
        std::unique_ptr<ICommand> cmd;
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            if (m_redo.empty())
            {
                m_lastError = "Nothing to redo.";
                callback = m_onChange;
            }
            else
            {
                cmd = std::move(m_redo.back());
                m_redo.pop_back();
            }
        }

        if (!cmd)
        {
            operationLock.unlock();
            invokeCallback(callback);
            return false;
        }

        std::string error;
        try
        {
            cmd->execute();
            error = cmd->lastError();
        }
        catch (...)
        {
            error = "Command execution threw an exception.";
        }

        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            m_lastError = error;
            if (error.empty())
            {
                m_undo.push_back(std::move(cmd));
                trimUndoLocked();
            }
            else
                m_redo.push_back(std::move(cmd));
            callback = m_onChange;
        }
        operationLock.unlock();
        invokeCallback(callback);
        return error.empty();
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
        std::unique_lock<std::mutex> operationLock(m_operationMtx);
        std::vector<std::unique_ptr<ICommand>> oldUndo;
        std::vector<std::unique_ptr<ICommand>> oldRedo;
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> stateLock(m_mtx);
            oldUndo.swap(m_undo);
            oldRedo.swap(m_redo);
            m_lastError.clear();
            callback = m_onChange;
        }
        operationLock.unlock();
        invokeCallback(callback);
    }

    void setChangeCallback(std::function<void()> cb)
    {
        std::lock_guard<std::mutex> stateLock(m_mtx);
        m_onChange = std::move(cb);
    }

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

    // Description of the last failed execute/undo/redo, empty on success.
    std::string lastError() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_lastError;
    }

  private:
    std::function<void()> setErrorAndGetCallback(const std::string &error)
    {
        std::lock_guard<std::mutex> stateLock(m_mtx);
        m_lastError = error;
        return m_onChange;
    }

    void trimUndoLocked()
    {
        while (m_undo.size() > m_maxDepth)
            m_undo.erase(m_undo.begin());
    }

    static void invokeCallback(const std::function<void()> &callback)
    {
        if (callback)
            callback();
    }

    // m_mtx protects the observable history/error/callback state. This second
    // mutex serializes ICommand transitions without making readers wait on
    // filesystem I/O and without allowing two transitions to move the same
    // history entry concurrently.
    mutable std::mutex m_operationMtx;
    mutable std::mutex m_mtx;
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
    size_t m_maxDepth;
    std::function<void()> m_onChange;
    std::string m_lastError;
};
