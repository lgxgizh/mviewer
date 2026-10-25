#pragma once

#include "ICommand.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Reversible on-disk rotate/flip. ExifOrientation undoes by applying the
// inverse orientation change. Pixel rewrites undo by restoring a byte backup
// taken before the first execute. Redo repeats the forward operation and does
// not take another backup.
//
// Backup budget is process-wide: one file may use at most 200 MB and the
// live backups together may use at most 1 GB. A file over the per-file cap
// is still transformed and then cannot be undone. When a new backup does not
// fit, the oldest backups are discarded and those commands can no longer
// undo the affected files.
class RotateFilesCommand : public ICommand
{
  public:
    enum class Op
    {
        Rotate,
        FlipH,
        FlipV
    };

    RotateFilesCommand(std::vector<std::string> paths, Op op, int degreesCw);
    ~RotateFilesCommand() override;

    RotateFilesCommand(const RotateFilesCommand &) = delete;
    RotateFilesCommand &operator=(const RotateFilesCommand &) = delete;
    RotateFilesCommand(RotateFilesCommand &&) = delete;
    RotateFilesCommand &operator=(RotateFilesCommand &&) = delete;

    std::string id() const override;
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override;
    std::string lastError() const override
    {
        return m_lastError;
    }

    // Return false to stop before the next file. `finished` is how many files
    // have already been attempted. Invoked on the thread that calls execute().
    void setProgressCallback(std::function<bool(int finished, int total)> callback)
    {
        m_progress = std::move(callback);
    }

    // Successful paths after execute() and after undo(). UI thread.
    void setPathsChangedCallback(std::function<void(const std::vector<std::string> &)> callback)
    {
        m_onPaths = std::move(callback);
    }

    int successCount() const
    {
        return m_successCount;
    }
    int failureCount() const
    {
        return static_cast<int>(m_failedPaths.size());
    }
    const std::vector<std::string> &failedPaths() const
    {
        return m_failedPaths;
    }
    const std::vector<std::string> &failedErrors() const
    {
        return m_failedErrors;
    }
    // Parallel to failedErrors(). Values match ImageRotateError.
    const std::vector<int> &failedErrorCodes() const
    {
        return m_failedCodes;
    }
    bool canceled() const
    {
        return m_canceled;
    }
    bool droppedOldestBackup() const
    {
        return m_droppedOldestBackup;
    }
    bool blockedByPerFileLimit() const
    {
        return m_blockedByPerFileLimit;
    }

    static void setBackupLimits(std::uint64_t perFileBytes, std::uint64_t totalBytes);
    static std::pair<std::uint64_t, std::uint64_t> backupLimits();
    // Drop every live backup and restore the default 200 MB / 1 GB caps.
    static void resetBackupBudget();

  private:
    enum class Phase
    {
        Fresh,
        Applied,
        Undone
    };

    struct Entry
    {
        std::string path;
        bool ok = false;
        bool exif = false;
        bool backupAlive = false;
        std::uint64_t backupId = 0;
        std::string backupPath;
    };

    void runFirst();
    void runRedo();
    bool transformEntry(Entry &entry, bool takeBackup);
    bool restoreEntry(const Entry &entry);
    bool applyInverse(const Entry &entry);
    void rememberFailure(const std::string &path, const std::string &error, int code);
    void notifyPaths();
    void releaseEntryBackup(Entry &entry);
    void revokeBackup(size_t entryIndex);
    bool storeBackup(Entry &entry, const std::vector<std::uint8_t> &bytes);

    std::vector<std::string> m_paths;
    Op m_op = Op::Rotate;
    int m_degrees = 0;
    Phase m_phase = Phase::Fresh;
    std::vector<Entry> m_entries;
    std::vector<std::string> m_failedPaths;
    std::vector<std::string> m_failedErrors;
    std::vector<int> m_failedCodes;
    int m_successCount = 0;
    bool m_canceled = false;
    bool m_droppedOldestBackup = false;
    bool m_blockedByPerFileLimit = false;
    std::string m_lastError;
    std::function<bool(int, int)> m_progress;
    std::function<void(const std::vector<std::string> &)> m_onPaths;
};
