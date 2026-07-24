// M7 ④ Undo/Redo Command pattern: CommandStack history + Rotate/Label commands
// with real undo. Domain-free; no display.
#include "core/command/CommandStack.h"
#include "core/command/FileDeleteCommand.h"
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
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
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

int main()
{
    printf("=== CommandStack + Rotate/Label tests (M7 ④) ===\n");
    fflush(stdout);
    testRotateCommand();
    testLabelCommand();
    testCommandStack();
    testFileRenameCommand();
    testFileDeleteCommand();
    testFileMoveCommand();
    testCommandStackReportsErrors();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
