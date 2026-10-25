#include "RotateFilesCommand.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/JpegExif.h"

#include <filesystem>
#include <mutex>

namespace
{

constexpr std::uint64_t kDefaultPerFile = 200ull * 1024ull * 1024ull;
constexpr std::uint64_t kDefaultTotal = 1024ull * 1024ull * 1024ull;

struct BackupSlot
{
    std::uint64_t id = 0;
    std::uint64_t bytes = 0;
    std::string path;
    RotateFilesCommand *owner = nullptr;
    size_t entryIndex = 0;
};

struct BackupBudget
{
    std::mutex mutex;
    std::vector<BackupSlot> slots;
    std::uint64_t used = 0;
    std::uint64_t perFile = kDefaultPerFile;
    std::uint64_t total = kDefaultTotal;
    std::uint64_t nextId = 1;
};

BackupBudget &budget()
{
    static BackupBudget state;
    return state;
}

std::string backupDirectory()
{
    const auto dir = std::filesystem::temp_directory_path() / "mviewer-rotate-undo";
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return mviewer::core::pathToUtf8(dir);
}

bool isJpegPath(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size())
        return false;
    std::string ext = path.substr(dot + 1);
    for (char &ch : ext)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return ext == "jpg" || ext == "jpeg";
}

int normalizedDegrees(int degrees)
{
    int value = degrees % 360;
    if (value < 0)
        value += 360;
    return value;
}

mviewer::core::ImageFileRotateResult applyForward(const std::string &path,
                                                  RotateFilesCommand::Op op, int degrees)
{
    if (op == RotateFilesCommand::Op::Rotate)
        return mviewer::core::rotateImageFile(path, degrees);
    return mviewer::core::flipImageFile(path, op == RotateFilesCommand::Op::FlipH);
}

} // namespace

// Defined below so the budget table can revoke an entry without including
// the method body in the header.
void RotateFilesCommand::releaseEntryBackup(Entry &entry)
{
    if (entry.backupId == 0 && entry.backupPath.empty())
    {
        entry.backupAlive = false;
        return;
    }
    const std::uint64_t id = entry.backupId;
    const std::string path = entry.backupPath;
    entry.backupAlive = false;
    entry.backupId = 0;
    entry.backupPath.clear();

    std::string removePath = path;
    {
        auto &state = budget();
        std::lock_guard<std::mutex> lock(state.mutex);
        for (auto it = state.slots.begin(); it != state.slots.end(); ++it)
        {
            if (it->id != id)
                continue;
            if (state.used >= it->bytes)
                state.used -= it->bytes;
            else
                state.used = 0;
            if (removePath.empty())
                removePath = it->path;
            state.slots.erase(it);
            break;
        }
    }
    if (!removePath.empty())
    {
        std::error_code error;
        std::filesystem::remove(mviewer::core::pathFromUtf8(removePath), error);
    }
}

RotateFilesCommand::RotateFilesCommand(std::vector<std::string> paths, Op op, int degreesCw)
    : m_paths(std::move(paths)), m_op(op), m_degrees(normalizedDegrees(degreesCw))
{
}

RotateFilesCommand::~RotateFilesCommand()
{
    std::vector<std::string> files;
    {
        auto &state = budget();
        std::lock_guard<std::mutex> lock(state.mutex);
        for (auto it = state.slots.begin(); it != state.slots.end();)
        {
            if (it->owner != this)
            {
                ++it;
                continue;
            }
            if (state.used >= it->bytes)
                state.used -= it->bytes;
            else
                state.used = 0;
            files.push_back(it->path);
            it = state.slots.erase(it);
        }
    }
    for (const std::string &file : files)
    {
        std::error_code error;
        std::filesystem::remove(mviewer::core::pathFromUtf8(file), error);
    }
}

std::string RotateFilesCommand::id() const
{
    return m_op == Op::Rotate ? "file.rotate" : "file.flip";
}

std::string RotateFilesCommand::description() const
{
    const char *verb = m_op == Op::Rotate ? "Rotate" : "Flip";
    if (m_paths.size() == 1)
        return std::string(verb) + " 1 file";
    return std::string(verb) + " " + std::to_string(m_paths.size()) + " files";
}

void RotateFilesCommand::execute()
{
    m_lastError.clear();
    if (m_phase == Phase::Applied)
        return;
    if (m_phase == Phase::Undone)
    {
        runRedo();
        return;
    }
    runFirst();
}

void RotateFilesCommand::undo()
{
    m_lastError.clear();
    if (m_phase != Phase::Applied)
    {
        m_lastError = "Nothing to undo.";
        return;
    }
    for (const Entry &entry : m_entries)
    {
        if (!entry.ok)
            continue;
        if (entry.backupAlive)
        {
            if (!restoreEntry(entry))
                return;
        }
        else if (entry.exif)
        {
            if (!applyInverse(entry))
                return;
        }
    }
    m_phase = Phase::Undone;
    notifyPaths();
}

bool RotateFilesCommand::canUndo() const
{
    if (m_phase != Phase::Applied)
        return false;
    for (const Entry &entry : m_entries)
    {
        if (entry.ok && (entry.backupAlive || entry.exif))
            return true;
    }
    return false;
}

void RotateFilesCommand::setBackupLimits(std::uint64_t perFileBytes, std::uint64_t totalBytes)
{
    auto &state = budget();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.perFile = perFileBytes;
    state.total = totalBytes;
}

std::pair<std::uint64_t, std::uint64_t> RotateFilesCommand::backupLimits()
{
    auto &state = budget();
    std::lock_guard<std::mutex> lock(state.mutex);
    return {state.perFile, state.total};
}

void RotateFilesCommand::resetBackupBudget()
{
    std::vector<std::string> files;
    {
        auto &state = budget();
        std::lock_guard<std::mutex> lock(state.mutex);
        files.reserve(state.slots.size());
        for (const BackupSlot &slot : state.slots)
            files.push_back(slot.path);
        state.slots.clear();
        state.used = 0;
        state.perFile = kDefaultPerFile;
        state.total = kDefaultTotal;
    }
    for (const std::string &file : files)
    {
        std::error_code error;
        std::filesystem::remove(mviewer::core::pathFromUtf8(file), error);
    }
}

void RotateFilesCommand::runFirst()
{
    m_entries.clear();
    m_failedPaths.clear();
    m_failedErrors.clear();
    m_failedCodes.clear();
    m_successCount = 0;
    m_canceled = false;
    const int total = static_cast<int>(m_paths.size());
    int finished = 0;
    for (const std::string &path : m_paths)
    {
        if (m_progress && !m_progress(finished, total))
        {
            m_canceled = true;
            break;
        }
        Entry entry;
        entry.path = path;
        transformEntry(entry, true);
        if (entry.ok)
            ++m_successCount;
        m_entries.push_back(std::move(entry));
        ++finished;
    }
    if (m_progress)
        m_progress(finished, total);

    if (m_successCount == 0)
    {
        m_lastError = m_canceled
                          ? "canceled"
                          : (m_failedErrors.empty() ? "rotate failed" : m_failedErrors.front());
        return;
    }
    m_phase = Phase::Applied;
    notifyPaths();
}

void RotateFilesCommand::runRedo()
{
    bool failed = false;
    for (Entry &entry : m_entries)
    {
        if (!entry.ok || !(entry.exif || entry.backupAlive))
            continue;
        const auto result = applyForward(entry.path, m_op, m_degrees);
        if (!result.ok)
        {
            m_lastError = result.error.empty() ? "rotate failed" : result.error;
            failed = true;
            break;
        }
    }
    if (failed)
        return;
    m_phase = Phase::Applied;
    notifyPaths();
}

bool RotateFilesCommand::transformEntry(Entry &entry, bool takeBackup)
{
    std::error_code sizeError;
    const auto nativeSize =
        std::filesystem::file_size(mviewer::core::pathFromUtf8(entry.path), sizeError);
    if (sizeError)
    {
        rememberFailure(entry.path, "read failed",
                        static_cast<int>(mviewer::core::ImageRotateError::ReadFailed));
        return false;
    }
    const auto size = static_cast<std::uint64_t>(nativeSize);
    const auto limits = backupLimits();
    const bool fitsBackup = size <= limits.first && size <= limits.second;

    std::vector<std::uint8_t> bytes;
    bool exifTagged = false;
    if (isJpegPath(entry.path) && fitsBackup)
    {
        std::string error;
        if (!mviewer::core::readFileBytes(entry.path, bytes, &error))
        {
            rememberFailure(entry.path, error.empty() ? "read failed" : error,
                            static_cast<int>(mviewer::core::ImageRotateError::ReadFailed));
            return false;
        }
        exifTagged = mviewer::core::readJpegOrientation(bytes).has_value();
    }

    const bool needBackup = takeBackup && !exifTagged;
    bool refusedBackup = false;
    if (needBackup)
    {
        if (!fitsBackup)
            refusedBackup = true;
        else
            storeBackup(entry, bytes);
    }

    const auto result = applyForward(entry.path, m_op, m_degrees);
    if (!result.ok)
    {
        releaseEntryBackup(entry);
        rememberFailure(entry.path, result.error.empty() ? "rotate failed" : result.error,
                        static_cast<int>(result.errorCode));
        return false;
    }
    if (result.method == mviewer::core::ImageRotateMethod::ExifOrientation)
    {
        releaseEntryBackup(entry);
        entry.exif = true;
    }
    else if (result.method != mviewer::core::ImageRotateMethod::PixelRewrite)
    {
        releaseEntryBackup(entry);
    }
    else if (refusedBackup)
    {
        m_blockedByPerFileLimit = true;
    }
    entry.ok = true;
    return true;
}

void RotateFilesCommand::revokeBackup(size_t entryIndex)
{
    if (entryIndex >= m_entries.size())
        return;
    Entry &entry = m_entries[entryIndex];
    entry.backupAlive = false;
    entry.backupId = 0;
    entry.backupPath.clear();
}

bool RotateFilesCommand::storeBackup(Entry &entry, const std::vector<std::uint8_t> &bytes)
{
    std::vector<std::uint8_t> payload = bytes;
    if (payload.empty())
    {
        std::string error;
        if (!mviewer::core::readFileBytes(entry.path, payload, &error) || payload.empty())
            return false;
    }

    std::string storedPath;
    std::uint64_t storedId = 0;
    {
        auto &state = budget();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (payload.size() > state.perFile || payload.size() > state.total)
            return false;
        while (state.used + payload.size() > state.total && !state.slots.empty())
        {
            BackupSlot oldest = state.slots.front();
            state.slots.erase(state.slots.begin());
            if (state.used >= oldest.bytes)
                state.used -= oldest.bytes;
            else
                state.used = 0;
            if (oldest.owner)
                oldest.owner->revokeBackup(oldest.entryIndex);
            std::error_code removeError;
            std::filesystem::remove(mviewer::core::pathFromUtf8(oldest.path), removeError);
            m_droppedOldestBackup = true;
        }
        if (state.used + payload.size() > state.total)
            return false;

        const std::string dir = backupDirectory();
        if (dir.empty())
            return false;
        storedId = state.nextId++;
        storedPath = dir + "/" + std::to_string(storedId) + ".bin";
        BackupSlot slot;
        slot.id = storedId;
        slot.bytes = payload.size();
        slot.path = storedPath;
        slot.owner = this;
        slot.entryIndex = m_entries.size();
        state.slots.push_back(slot);
        state.used += payload.size();
    }

    std::string error;
    if (!mviewer::core::writeFileBytesAtomic(storedPath, payload, &error))
    {
        entry.backupId = storedId;
        entry.backupPath = storedPath;
        releaseEntryBackup(entry);
        return false;
    }
    entry.backupAlive = true;
    entry.backupId = storedId;
    entry.backupPath = storedPath;
    return true;
}

bool RotateFilesCommand::restoreEntry(const Entry &entry)
{
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!mviewer::core::readFileBytes(entry.backupPath, bytes, &error) ||
        !mviewer::core::writeFileBytesAtomic(entry.path, bytes, &error))
    {
        m_lastError = error.empty() ? "undo write failed" : error;
        return false;
    }
    return true;
}

bool RotateFilesCommand::applyInverse(const Entry &entry)
{
    mviewer::core::ImageFileRotateResult result;
    if (m_op == Op::Rotate)
    {
        const int inverse = (360 - normalizedDegrees(m_degrees)) % 360;
        result = mviewer::core::rotateImageFile(entry.path, inverse == 0 ? 0 : inverse);
    }
    else
    {
        result = mviewer::core::flipImageFile(entry.path, m_op == Op::FlipH);
    }
    if (!result.ok)
    {
        m_lastError = result.error.empty() ? "undo rotate failed" : result.error;
        return false;
    }
    return true;
}

void RotateFilesCommand::rememberFailure(const std::string &path, const std::string &error,
                                         int code)
{
    m_failedPaths.push_back(path);
    m_failedErrors.push_back(error);
    m_failedCodes.push_back(code);
}

void RotateFilesCommand::notifyPaths()
{
    if (!m_onPaths)
        return;
    std::vector<std::string> changed;
    changed.reserve(m_entries.size());
    for (const Entry &entry : m_entries)
    {
        if (entry.ok)
            changed.push_back(entry.path);
    }
    if (!changed.empty())
        m_onPaths(changed);
}
