// Item 5: undo for a clipboard paste. "Undo" deletes exactly the copies the
// paste created, and never touches anything outside that folder.
#include "core/command/ClipboardPasteCommand.h"
#include "core/command/CommandStack.h"

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

void writeFile(const fs::path &path, char byte)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << byte;
}

bool exists(const std::string &path)
{
    std::error_code ec;
    return fs::exists(path, ec);
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

    const std::string a = (folder / "copy-a.png").string();
    const std::string b = (folder / "copy-b.png").string();
    const std::string foreign = (outside / "keep-me.png").string();
    writeFile(a, 'a');
    writeFile(b, 'b');
    writeFile(foreign, 'f');

    CommandStack stack;
    CHECK(!stack.canUndo(), "a fresh stack has nothing to undo");

    stack.recordExecuted(
        std::make_unique<ClipboardPasteCommand>(std::vector<std::string>{a, b}, folder.string()));
    CHECK(stack.canUndo(), "a recorded paste can be undone");
    CHECK(stack.undoLabel() == "Paste into folder", "the undo label names the paste");
    CHECK(stack.undo(), "undo succeeds");
    CHECK(!exists(a) && !exists(b), "undo deletes exactly the copied files");
    CHECK(!stack.canUndo(), "the paste leaves nothing else to undo");
    CHECK(!stack.canRedo(), "an undo-only paste does not offer a no-op redo");
    CHECK(exists(foreign), "the untouched file outside the folder survives");

    // A history entry that points at another folder must never delete in the
    // folder the user is now looking at.
    const std::string otherFolder = (root / "other").string();
    fs::create_directories(otherFolder, ec);
    const std::string stray = (fs::path(otherFolder) / "stray.png").string();
    writeFile(stray, 's');
    stack.recordExecuted(
        std::make_unique<ClipboardPasteCommand>(std::vector<std::string>{stray}, folder.string()));
    CHECK(stack.undo(), "undo succeeds for a stale entry");
    CHECK(exists(stray), "a copy recorded for another folder is left alone");

    fs::remove_all(root, ec);
    printf("\n==== clipboard paste undo test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
