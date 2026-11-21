// FileCopyCommand copies without removing the source and undo deletes the copy.
#include "core/command/CommandStack.h"
#include "core/command/FileCopyCommand.h"
#include "core/command/FileSystemAdapter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << (msg) << "\n";                                                \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << (msg) << "\n";                                                \
        }                                                                                          \
    } while (0)

static std::string readAll(const fs::path &path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer-filecopy-cmd";
    fs::remove_all(root);
    const fs::path srcDir = root / "src";
    const fs::path dstDir = root / "dst";
    fs::create_directories(srcDir);
    fs::create_directories(dstDir);
    const fs::path source = srcDir / "note.txt";
    const fs::path occupied = dstDir / "note.txt";
    {
        std::ofstream out(source);
        out << "hello-copy";
    }
    {
        std::ofstream out(occupied);
        out << "keep-me";
    }

    FileCopyCommand copy({mviewer::core::pathToUtf8(source)}, mviewer::core::pathToUtf8(dstDir));
    CHECK(copy.canExecute(), "a source and a destination can copy");
    copy.execute();
    CHECK(copy.lastError().empty(), "copy succeeds");
    CHECK(copy.copied().size() == 1, "one copy is recorded");
    CHECK(fs::exists(source), "the source stays in place");
    CHECK(readAll(occupied) == "keep-me", "an existing destination is not overwritten");
    const fs::path created = mviewer::core::pathFromUtf8(copy.copied().front().second);
    CHECK(created.filename() == "note_1.txt", "a name collision is auto-renamed");
    CHECK(readAll(created) == "hello-copy", "the copy has the source bytes");

    CommandStack stack;
    stack.recordExecuted(std::make_unique<FileCopyCommand>(std::move(copy)));
    CHECK(stack.canUndo(), "the copy is undoable");
    CHECK(stack.undo(), "undo runs");
    CHECK(!fs::exists(created), "undo removes the copy");
    CHECK(fs::exists(source) && readAll(source) == "hello-copy", "undo leaves the source");
    CHECK(fs::exists(occupied) && readAll(occupied) == "keep-me", "undo leaves the other file");

    fs::remove_all(root);
    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "filecopycommand_tests: PASS\n";
    return 0;
}
