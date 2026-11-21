// Item 5: undo for a clipboard paste. "Undo" deletes exactly the copies the
// paste created, and never touches anything outside that folder.
#include "core/command/ClipboardPasteCommand.h"
#include "core/command/CommandStack.h"
#include "core/filesystem/Utf8Path.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ClipboardPasteCommand = ::ClipboardPasteCommand;

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

void writeFile(const std::string &path, char byte)
{
    std::ofstream out(mviewer::core::pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    out << byte;
}

bool exists(const std::string &path)
{
    std::error_code ec;
    return fs::exists(mviewer::core::pathFromUtf8(path), ec);
}
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer_paste_undo_test";
    const fs::path outside = root / "outside";
    const fs::path folder = root / "folder";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(folder, ec);
    fs::create_directories(outside, ec);

    const std::string a = mviewer::core::pathToUtf8(folder / "copy-a.png");
    const std::string b = mviewer::core::pathToUtf8(folder / "copy-b.png");
    const std::string foreign = mviewer::core::pathToUtf8(outside / "keep-me.png");
    writeFile(a, 'a');
    writeFile(b, 'b');
    writeFile(foreign, 'f');

    CommandStack stack;
    CHECK(!stack.canUndo(), "a fresh stack has nothing to undo");

    stack.recordExecuted(std::make_unique<ClipboardPasteCommand>(
        std::vector<std::string>{a, b}, mviewer::core::pathToUtf8(folder)));
    CHECK(stack.canUndo(), "a recorded paste can be undone");
    CHECK(stack.undoLabel() == "Paste into folder", "the undo label names the paste");
    CHECK(stack.undo(), "undo succeeds");
    CHECK(!exists(a) && !exists(b), "undo deletes exactly the copied files");
    CHECK(!stack.canUndo(), "the paste leaves nothing else to undo");
    CHECK(!stack.canRedo(), "an undo-only paste does not offer a no-op redo");
    CHECK(exists(foreign), "the untouched file outside the folder survives");

    // A history entry that points at another folder must never delete in the
    // folder the user is now looking at.
    const std::string otherFolder = mviewer::core::pathToUtf8(root / "other");
    fs::create_directories(otherFolder, ec);
    const std::string stray = otherFolder + "/stray.png";
    writeFile(stray, 's');
    stack.recordExecuted(std::make_unique<ClipboardPasteCommand>(
        std::vector<std::string>{stray}, mviewer::core::pathToUtf8(folder)));
    CHECK(stack.undo(), "undo succeeds for a stale entry");
    CHECK(exists(stray), "a copy recorded for another folder is left alone");

    // A non-ASCII path: the command used to build fs::path from a narrow UTF-8
    // string, which fails silently on a Chinese directory name.
    const std::string wide = mviewer::core::pathToUtf8(root) + "/中文目录";
    fs::create_directories(wide, ec);
    const std::string wideA = wide + "/副本-a.png";
    writeFile(wideA, 'w');
    CommandStack wideStack;
    wideStack.recordExecuted(
        std::make_unique<ClipboardPasteCommand>(std::vector<std::string>{wideA}, wide));
    CHECK(wideStack.undo(), "undo succeeds for a non-ASCII path");
    CHECK(!exists(wideA), "undo deletes the copy in a non-ASCII folder");

    fs::remove_all(root, ec);
    printf("\n==== clipboard paste undo test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
