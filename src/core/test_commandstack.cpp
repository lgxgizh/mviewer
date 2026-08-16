// M7 ④ Undo/Redo Command pattern: CommandStack history + Rotate/Label commands
// with real undo. Domain-free; no display.
#include "core/command/CommandStack.h"
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileSystemAdapter.h"
#include "core/command/FileMoveCommand.h"
#include "core/command/FileRenameCommand.h"
#include "core/command/LabelCommand.h"
#include "core/command/RotateCommand.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static ImageData makeRGB(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        (*d.buffer)[i * 3 + 0] = r;
        (*d.buffer)[i * 3 + 1] = g;
        (*d.buffer)[i * 3 + 2] = b;
    }
    return d;
}

static void testRotateCommand()
{
    printf("\n[RotateCommand + undo]\n");
    fflush(stdout);
    auto frame = std::make_shared<ImageData>();
    *frame = makeRGB(2, 3, 9, 9, 9); // 2 wide, 3 tall, all gray
    auto f = std::make_shared<ImageFrame>(ImageFrame::create("rot.jpg", *frame));

    RotateCommand rot(f);
    CHECK(rot.canExecute() && rot.canUndo(), "rotate command can execute + undo");

    rot.execute();
    // 90 CW of 2x3 -> 3x2.
    CHECK(f->width() == 3 && f->height() == 2, "after rotate: 2x3 -> 3x2");

    rot.undo();
    CHECK(f->width() == 2 && f->height() == 3, "after undo: back to 2x3");
    // Pixel content restored exactly.
    const ImageBuffer v = f->pixels().view();
    bool restored = true;
    for (int i = 0; i < 2 * 3; ++i)
        if (v.data[i * 3] != 9)
            restored = false;
    CHECK(restored, "undo restored original pixels exactly");
}

static void testLabelCommand()
{
    printf("\n[LabelCommand + undo]\n");
    fflush(stdout);
    auto frame = std::make_shared<ImageData>();
    *frame = makeRGB(4, 4, 1, 2, 3);
    auto f = std::make_shared<ImageFrame>(ImageFrame::create("label.jpg", *frame));

    LabelCommand add(f, "favorite", LabelCommand::Mode::Add);
    add.execute();
    CHECK(f->hasTag("favorite"), "tag added after execute");
    add.undo();
    CHECK(!f->hasTag("favorite"), "tag removed after undo");
    add.execute();
    CHECK(f->hasTag("favorite"), "tag re-added after second execute");
}

static void testCommandStack()
{
    printf("\n[CommandStack undo/redo]\n");
    fflush(stdout);
    CommandStack stack;
    auto frame = std::make_shared<ImageData>();
    *frame = makeRGB(4, 4, 5, 5, 5);
    auto f = std::make_shared<ImageFrame>(ImageFrame::create("stack.jpg", *frame));

    CHECK(!stack.canUndo() && !stack.canRedo(), "empty stack: no undo/redo");

    stack.execute(std::make_unique<LabelCommand>(f, "keep", LabelCommand::Mode::Add));
    CHECK(stack.canUndo() && !stack.canRedo(), "after execute: can undo, not redo");
    CHECK(f->hasTag("keep"), "command applied via stack");

    stack.undo();
    CHECK(!f->hasTag("keep"), "stack.undo reversed the command");
    CHECK(stack.canRedo(), "after undo: can redo");

    stack.redo();
    CHECK(f->hasTag("keep"), "stack.redo re-applied the command");

    // New action clears the redo branch.
    stack.execute(std::make_unique<LabelCommand>(f, "another", LabelCommand::Mode::Add));
    CHECK(!stack.canRedo(), "new execute clears redo branch");

    // rotate via stack, then undo restores dimensions.
    stack.execute(std::make_unique<RotateCommand>(f));
    CHECK(f->width() == 4 && f->height() == 4, "rotate via stack keeps 4x4 (square)");

    stack.undo(); // undo rotate
    CHECK(f->width() == 4 && f->height() == 4, "undo rotate keeps 4x4");
    stack.undo(); // undo 'another' label
    stack.undo(); // undo 'keep' label
    CHECK(!f->hasTag("keep") && !f->hasTag("another"), "all label commands undone");
    CHECK(!stack.canUndo(), "fully undone: no more undo");
}

static void writeFile(const std::filesystem::path &p, const std::string &content)
{
    std::ofstream out(p, std::ios::binary);
    out << content;
}

static std::string readFile(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class FaultFileSystem final : public mviewer::core::FileSystemAdapter
{
  public:
    int failRenameAt = 0;
    int failRenameAt2 = 0;
    int failRemoveAt = 0;
    int renameCalls = 0;
    int removeCalls = 0;
    int copyProgressCalls = 0;
    bool failAsCrossVolume = false;
    bool incrementalCopy = false;

    bool exists(const std::filesystem::path &path, std::error_code &ec) const override
    {
        return base()->exists(path, ec);
    }
    bool isDirectory(const std::filesystem::path &path, std::error_code &ec) const override
    {
        return base()->isDirectory(path, ec);
    }
    bool isRegularFile(const std::filesystem::path &path, std::error_code &ec) const override
    {
        return base()->isRegularFile(path, ec);
    }
    bool createDirectories(const std::filesystem::path &path, std::error_code &ec) override
    {
        return base()->createDirectories(path, ec);
    }
    bool rename(const std::filesystem::path &from, const std::filesystem::path &to,
                std::error_code &ec) override
    {
        ++renameCalls;
        if ((failRenameAt > 0 && renameCalls == failRenameAt) ||
            (failRenameAt2 > 0 && renameCalls == failRenameAt2))
        {
            ec = std::make_error_code(failAsCrossVolume ? std::errc::cross_device_link
                                                         : std::errc::permission_denied);
            return false;
        }
        return base()->rename(from, to, ec);
    }
    bool copyFile(const std::filesystem::path &from, const std::filesystem::path &to,
                  std::error_code &ec) override
    {
        return base()->copyFile(from, to, ec);
    }
    bool copyFileWithProgress(const std::filesystem::path &from,
                              const std::filesystem::path &to,
                              const TransferObserver &observer,
                              std::error_code &ec) override
    {
        if (!incrementalCopy)
            return FileSystemAdapter::copyFileWithProgress(from, to, observer, ec);

        const uintmax_t total = base()->fileSize(from, ec);
        if (ec)
            return false;
        if (observer && !observer(0, total))
        {
            ec = std::make_error_code(std::errc::operation_canceled);
            return false;
        }

        std::ifstream input(from, std::ios::binary);
        std::ofstream output(to, std::ios::binary | std::ios::trunc);
        if (!input || !output)
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        std::vector<char> buffer(4096);
        uintmax_t copied = 0;
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count <= 0)
                continue;
            output.write(buffer.data(), count);
            if (!output)
            {
                ec = std::make_error_code(std::errc::io_error);
                return false;
            }
            copied += static_cast<uintmax_t>(count);
            ++copyProgressCalls;
            if (observer && !observer(copied, total))
            {
                ec = std::make_error_code(std::errc::operation_canceled);
                return false;
            }
        }
        if (input.bad())
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        output.close();
        if (!output)
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        return true;
    }
    bool remove(const std::filesystem::path &path, std::error_code &ec) override
    {
        ++removeCalls;
        if (failRemoveAt > 0 && removeCalls == failRemoveAt)
        {
            ec = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        return base()->remove(path, ec);
    }
    uintmax_t fileSize(const std::filesystem::path &path, std::error_code &ec) const override
    {
        return base()->fileSize(path, ec);
    }

  private:
    static std::shared_ptr<mviewer::core::FileSystemAdapter> base()
    {
        return mviewer::core::defaultFileSystemAdapter();
    }
};

class ScriptedFailureCommand final : public ICommand
{
  public:
    explicit ScriptedFailureCommand(bool unresolved) : m_unresolved(unresolved)
    {
    }

    std::string id() const override
    {
        return "scripted_failure";
    }
    std::string description() const override
    {
        return "Scripted failure";
    }
    void execute() override
    {
        m_error = m_unresolved ? "partial state" : "deterministic failure";
    }
    void undo() override
    {
        m_error.clear();
        m_unresolved = false;
    }
    bool canUndo() const override
    {
        return m_unresolved;
    }
    std::string lastError() const override
    {
        return m_error;
    }
    bool hasUnresolvedState() const override
    {
        return m_unresolved;
    }

  private:
    bool m_unresolved = false;
    std::string m_error;
};

static void testCommandStackChangeCallback()
{
    printf("\n[CommandStack callback contract]\n");
    fflush(stdout);
    CommandStack stack;
    int callbackCount = 0;
    bool callbackQueriesWereSafe = true;
    stack.setChangeCallback(
        [&]()
        {
            ++callbackCount;
            // This is the exact MainWindow query set. It must be safe while
            // execute/undo/redo/clear has just completed.
            const bool canUndo = stack.canUndo();
            (void)canUndo;
            (void)stack.undoLabel();
            (void)stack.canRedo();
            (void)stack.redoLabel();
            (void)stack.undoDepth();
            (void)stack.redoDepth();
            (void)stack.lastError();
        });

    auto frame = std::make_shared<ImageData>();
    *frame = makeRGB(2, 2, 1, 2, 3);
    auto image = std::make_shared<ImageFrame>(ImageFrame::create("callback.jpg", *frame));
    CHECK(stack.execute(std::make_unique<LabelCommand>(image, "callback", LabelCommand::Mode::Add)),
          "execute completes with a callback that queries stack state");
    CHECK(stack.undo(), "undo completes with a callback that queries stack state");
    CHECK(stack.redo(), "redo completes with a callback that queries stack state");
    stack.clear();
    CHECK(stack.undoDepth() == 0 && stack.redoDepth() == 0,
          "clear completes and leaves both histories empty");
    CHECK(callbackCount == 4 && callbackQueriesWereSafe,
          "execute/undo/redo/clear each notify outside the state mutex");

    CHECK(!stack.execute(std::make_unique<ScriptedFailureCommand>(false)) &&
              stack.undoDepth() == 0 && !stack.lastError().empty(),
          "ordinary failure reports an error without adding history");
    CHECK(!stack.execute(std::make_unique<ScriptedFailureCommand>(true)) &&
              stack.undoDepth() == 1 && stack.canUndo(),
          "unresolved failure remains recoverable in history");
    CHECK(stack.undo() && stack.undoDepth() == 0 && stack.redoDepth() == 1,
          "unresolved state can be recovered through undo");
}

static void testFileRenameCommand()
{
    printf("\n[FileRenameCommand + undo]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_rename";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path a = dir / "a.txt";
    const fs::path b = dir / "b.txt";
    writeFile(a, "hello");

    FileRenameCommand cmd(a.string(), b.string());
    CHECK(cmd.canExecute(), "rename command can execute");
    cmd.execute();
    CHECK(cmd.lastError().empty() && fs::exists(b) && !fs::exists(a),
          "rename moves file to new name");
    cmd.undo();
    CHECK(cmd.lastError().empty() && fs::exists(a) && !fs::exists(b),
          "undo rename restores original name");

    // Destination already exists -> execute must fail and leave source intact.
    writeFile(a, "hello");
    writeFile(b, "blocker");
    FileRenameCommand cmd2(a.string(), b.string());
    cmd2.execute();
    CHECK(!cmd2.lastError().empty() && fs::exists(a), "rename fails when destination exists");

    fs::remove_all(dir);
}

static void testFileDeleteCommand()
{
    printf("\n[FileDeleteCommand + undo]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_delete";
    const fs::path trash = dir / "trash";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path a = dir / "a.txt";
    writeFile(a, "content");

    FileDeleteCommand cmd({a.string()}, trash.string());
    cmd.execute();
    CHECK(cmd.lastError().empty() && !fs::exists(a), "delete moves source away");
    CHECK(cmd.canUndo(), "delete command can undo");
    cmd.undo();
    CHECK(cmd.lastError().empty() && fs::exists(a) && fs::is_regular_file(a),
          "undo delete restores source");

    // Missing source -> execute must fail atomically.
    FileDeleteCommand cmd2({(dir / "missing.txt").string()}, trash.string());
    cmd2.execute();
    CHECK(!cmd2.lastError().empty(), "delete fails when source is missing");

    fs::remove_all(dir);
}

static void testFileMoveCommand()
{
    printf("\n[FileMoveCommand + undo]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_move";
    const fs::path srcDir = dir / "src";
    const fs::path dstDir = dir / "dst";
    fs::remove_all(dir);
    fs::create_directories(srcDir);
    fs::create_directories(dstDir);
    const fs::path a = srcDir / "a.txt";
    writeFile(a, "move me");

    FileMoveCommand cmd({a.string()}, dstDir.string());
    cmd.execute();
    CHECK(cmd.lastError().empty() && !fs::exists(a) && fs::exists(dstDir / "a.txt"),
          "move relocates file to destination");
    cmd.undo();
    CHECK(cmd.lastError().empty() && fs::exists(a) && !fs::exists(dstDir / "a.txt"),
          "undo move restores original location");

    fs::remove_all(dir);
}

static void testCommandStackReportsErrors()
{
    printf("\n[CommandStack error reporting]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_stack_err";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path a = dir / "a.txt";
    const fs::path b = dir / "b.txt";
    writeFile(a, "a");
    writeFile(b, "b");

    CommandStack stack;
    bool ok = stack.execute(std::make_unique<FileRenameCommand>(a.string(), b.string()));
    CHECK(!ok && !stack.lastError().empty(), "stack reports failed execute");
    CHECK(!stack.canUndo(), "failed execute is not added to undo history");

    fs::remove_all(dir);
}

static void testFileOperationSafety()
{
    printf("\n[M44 file operation safety]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_m44_fileops";
    fs::remove_all(dir);
    fs::create_directories(dir / "source");
    fs::create_directories(dir / "destination");

    const fs::path first = dir / "source" / "same.txt";
    const fs::path second = dir / "source" / "second.txt";
    writeFile(first, "first");
    writeFile(second, "second");
    writeFile(dir / "destination" / "same.txt", "existing");
    FileMoveCommand collision({first.string()}, (dir / "destination").string());
    collision.execute();
    CHECK(collision.lastError().empty() && fs::exists(dir / "destination" / "same_1.txt"),
          "collision chooses a new destination without overwriting");
    collision.undo();
    CHECK(collision.lastError().empty() && fs::exists(first), "collision move remains undoable");

    auto crossVolume = std::make_shared<FaultFileSystem>();
    crossVolume->failRenameAt = 1;
    crossVolume->failAsCrossVolume = true;
    const fs::path crossSource = dir / "source" / "cross.txt";
    writeFile(crossSource, "cross-volume fallback");
    FileMoveCommand cross({crossSource.string()}, (dir / "destination").string(), crossVolume);
    cross.execute();
    CHECK(cross.lastError().empty() && !fs::exists(crossSource) &&
              fs::exists(dir / "destination" / "cross.txt"),
          "cross-volume rename failure uses verified copy then source removal");
    cross.undo();
    CHECK(cross.lastError().empty() && fs::exists(crossSource),
          "cross-volume fallback remains reversible");

    auto rollbackOk = std::make_shared<FaultFileSystem>();
    rollbackOk->failRenameAt = 2;
    const fs::path batchA = dir / "source" / "batch-a.txt";
    const fs::path batchB = dir / "source" / "batch-b.txt";
    writeFile(batchA, "a");
    writeFile(batchB, "b");
    FileMoveCommand partial({batchA.string(), batchB.string()}, (dir / "destination").string(),
                            rollbackOk);
    partial.execute();
    CHECK(partial.state() == FileMoveCommand::State::RolledBack && fs::exists(batchA) &&
              fs::exists(batchB),
          "partial batch failure rolls completed moves back");
    CHECK(!partial.hasUnresolvedState() && partial.moved().empty(),
          "successful rollback clears only fully resolved bookkeeping");

    auto rollbackFail = std::make_shared<FaultFileSystem>();
    rollbackFail->failRenameAt = 2;
    rollbackFail->failRenameAt2 = 3;
    const fs::path stuckA = dir / "source" / "stuck-a.txt";
    const fs::path stuckB = dir / "source" / "stuck-b.txt";
    writeFile(stuckA, "a");
    writeFile(stuckB, "b");
    FileMoveCommand stuck({stuckA.string(), stuckB.string()}, (dir / "destination").string(),
                          rollbackFail);
    stuck.execute();
    CHECK(stuck.state() == FileMoveCommand::State::RollbackFailed && stuck.hasUnresolvedState() &&
              stuck.canUndo(),
          "rollback failure retains recoverable command state");
    rollbackFail->failRenameAt = 0;
    stuck.undo();
    CHECK(stuck.lastError().empty() && fs::exists(stuckA) && fs::exists(stuckB),
          "retained partial state can be recovered by undo");

    const std::string unicodeDirUtf8 =
        mviewer::core::pathToUtf8(dir) + "/中文目录-😀";
    const fs::path unicodeDir = mviewer::core::pathFromUtf8(unicodeDirUtf8);
    fs::create_directories(unicodeDir);
    const fs::path unicodeSource = unicodeDir / mviewer::core::pathFromUtf8("文件-😀.txt");
    const fs::path unicodeRenamed = unicodeDir / mviewer::core::pathFromUtf8("重命名-😀.txt");
    writeFile(unicodeSource, "unicode");
    FileRenameCommand unicode(mviewer::core::pathToUtf8(unicodeSource),
                              mviewer::core::pathToUtf8(unicodeRenamed));
    unicode.execute();
    CHECK(unicode.lastError().empty() && fs::exists(unicodeRenamed),
          "Unicode directory, Chinese filename, and emoji round-trip");
    unicode.undo();
    CHECK(unicode.lastError().empty() && fs::exists(unicodeSource),
          "Unicode rename undo restores the original path");

    FileMoveCommand samePath({first.string()}, (dir / "source").string());
    samePath.execute();
    CHECK(!samePath.lastError().empty() && fs::exists(first),
          "source equals destination directory is rejected explicitly");

    const fs::path cancelledSource = dir / "source" / "cancelled.txt";
    writeFile(cancelledSource, "cancel me");
    int cancelObservations = 0;
    FileMoveCommand cancelled({cancelledSource.string()}, (dir / "destination").string(),
                               crossVolume);
    cancelled.setTransferObserver(
        [&](uintmax_t, uintmax_t)
        {
            ++cancelObservations;
            return false;
        });
    cancelled.execute();
    CHECK(cancelObservations > 0 && !cancelled.lastError().empty() &&
              fs::exists(cancelledSource) && !fs::exists(dir / "destination" / "cancelled.txt"),
          "cancelled transfer leaves source and destination in a safe state");

    fs::remove_all(dir);
}

static void testFileTransferProgressAndCancellation()
{
    printf("\n[M45 transfer progress + cancellation]\n");
    fflush(stdout);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mviewer_test_m45_transfer";
    fs::remove_all(dir);
    fs::create_directories(dir / "source");
    fs::create_directories(dir / "destination");

    const std::string payload(3 * 1024 * 1024 + 17, 'x');
    const fs::path source = dir / "source" / "payload.bin";
    const fs::path destination = dir / "destination" / "payload.bin";
    writeFile(source, payload);
    auto progressive = std::make_shared<FaultFileSystem>();
    progressive->incrementalCopy = true;

    int progressCalls = 0;
    const auto copied = mviewer::core::copyFileAtomically(
        source, destination, progressive,
        [&](uintmax_t bytes, uintmax_t total)
        {
            ++progressCalls;
            return bytes <= total;
        });
    CHECK(copied.state == mviewer::core::FileTransferState::Succeeded &&
              readFile(destination) == payload && progressCalls > 3,
          "atomic copy reports real incremental progress and verifies the destination");

    int batchSuccess = 0;
    for (int i = 0; i < 101; ++i)
    {
        const fs::path batchSource = dir / "source" / ("batch_" + std::to_string(i) + ".bin");
        const fs::path batchDestination = dir / "destination" / ("batch_" + std::to_string(i) +
                                                                    ".bin");
        writeFile(batchSource, "batch-" + std::to_string(i));
        const auto result = mviewer::core::copyFileAtomically(batchSource, batchDestination,
                                                               progressive);
        if (result.state == mviewer::core::FileTransferState::Succeeded &&
            readFile(batchDestination) == "batch-" + std::to_string(i))
            ++batchSuccess;
    }
    CHECK(batchSuccess == 101, "101-file copy batch preserves atomic copy semantics");

    const fs::path earlySource = dir / "source" / "cancel-before-first.bin";
    const fs::path earlyDestination = dir / "destination" / "cancel-before-first.bin";
    writeFile(earlySource, "cancel before first");
    int earlyObservations = 0;
    const auto early = mviewer::core::copyFileAtomically(
        earlySource, earlyDestination, progressive,
        [&](uintmax_t, uintmax_t)
        {
            ++earlyObservations;
            return false;
        });
    CHECK(early.state == mviewer::core::FileTransferState::Failed && earlyObservations == 1 &&
              fs::exists(earlySource) && !fs::exists(earlyDestination),
          "cancellation before the first byte leaves source and destination safe");

    const fs::path halfwaySource = dir / "source" / "cancel-halfway.bin";
    const fs::path halfwayDestination = dir / "destination" / "cancel-halfway.bin";
    writeFile(halfwaySource, payload);
    uintmax_t cancelledAt = 0;
    const auto halfway = mviewer::core::copyFileAtomically(
        halfwaySource, halfwayDestination, progressive,
        [&](uintmax_t bytes, uintmax_t total)
        {
            cancelledAt = bytes;
            return total == 0 || bytes < total / 2;
        });
    const std::string halfwayTempPrefix = halfwayDestination.string() + ".mviewer-part-";
    bool temporaryRemains = false;
    for (const auto &entry : fs::directory_iterator(halfwayDestination.parent_path()))
    {
        if (entry.path().string().rfind(halfwayTempPrefix, 0) == 0)
            temporaryRemains = true;
    }
    CHECK(halfway.state == mviewer::core::FileTransferState::Failed && cancelledAt > 0 &&
              fs::exists(halfwaySource) && !fs::exists(halfwayDestination) && !temporaryRemains,
          "halfway cancellation removes the partial file and preserves the source");

    fs::remove_all(dir);
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--callback-probe")
    {
        // Watchdog-safe regression mode. Before the M45 fix the callback
        // re-enters canUndo() while execute() owns the same non-recursive
        // mutex, so CTest's timeout terminates this child instead of hanging
        // the full suite forever.
        CommandStack stack;
        stack.setChangeCallback(
            [&stack]()
            {
                (void)stack.canUndo();
                (void)stack.undoLabel();
                (void)stack.canRedo();
                (void)stack.redoLabel();
            });
        auto frame = std::make_shared<ImageData>();
        *frame = makeRGB(1, 1, 1, 1, 1);
        auto image = std::make_shared<ImageFrame>(ImageFrame::create("probe.jpg", *frame));
        return stack.execute(std::make_unique<LabelCommand>(image, "probe", LabelCommand::Mode::Add))
                   ? 0
                   : 1;
    }
    printf("=== CommandStack + Rotate/Label tests (M7 ④) ===\n");
    fflush(stdout);
    testRotateCommand();
    testLabelCommand();
    testCommandStack();
    testCommandStackChangeCallback();
    testFileRenameCommand();
    testFileDeleteCommand();
    testFileMoveCommand();
    testCommandStackReportsErrors();
    testFileOperationSafety();
    testFileTransferProgressAndCancellation();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
