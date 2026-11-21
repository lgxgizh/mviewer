// Case-only rename and private-trash fallback. Invoked from test_commandstack.cpp
// so that file stays under the 800-line cap.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileRenameCommand.h"
#include "core/command/FileSystemAdapter.h"
#include "core/filesystem/Utf8Path.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

namespace
{

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
    } while (0)

void writeFile(const std::filesystem::path &path, const std::string &content)
{
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool spelled(const std::filesystem::path &dir, const std::string &name)
{
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (mviewer::core::pathToUtf8(entry.path().filename()) == name)
            return true;
    }
    return false;
}

bool asciiSame(const std::string &left, const std::string &right)
{
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i)
    {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

// exists() is true for a sibling whose name differs only by ASCII case.
class CaseFoldExistsFileSystem final : public mviewer::core::FileSystemAdapter
{
  public:
    bool exists(const std::filesystem::path &path, std::error_code &ec) const override
    {
        if (base()->exists(path, ec))
            return true;
        ec.clear();
        const std::string want = mviewer::core::pathToUtf8(path.filename());
        std::error_code iterEc;
        const auto parent = path.parent_path();
        std::filesystem::directory_iterator it(parent, iterEc);
        const std::filesystem::directory_iterator end;
        for (; !iterEc && it != end; it.increment(iterEc))
        {
            if (asciiSame(mviewer::core::pathToUtf8(it->path().filename()), want))
                return true;
        }
        return false;
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
        return base()->rename(from, to, ec);
    }
    bool copyFile(const std::filesystem::path &from, const std::filesystem::path &to,
                  std::error_code &ec) override
    {
        return base()->copyFile(from, to, ec);
    }
    bool remove(const std::filesystem::path &path, std::error_code &ec) override
    {
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

} // namespace

void testCaseOnlyFileRename(const std::filesystem::path &dir, int &g_pass, int &g_fail)
{
    namespace fs = std::filesystem;
    writeFile(dir / "IMG.JPG", "CaseMe");
    auto folded = std::make_shared<CaseFoldExistsFileSystem>();
    FileRenameCommand caseCmd((dir / "IMG.JPG").string(), (dir / "img.jpg").string(), folded);
    caseCmd.execute();
    CHECK(caseCmd.lastError().empty() && readFile(dir / "img.jpg") == "CaseMe" &&
              spelled(dir, "img.jpg") && !spelled(dir, "IMG.JPG"),
          "case-only rename succeeds when the destination exists only by case");
    caseCmd.undo();
    CHECK(caseCmd.lastError().empty() && readFile(dir / "IMG.JPG") == "CaseMe" &&
              spelled(dir, "IMG.JPG") && !spelled(dir, "img.jpg"),
          "undo restores the original spelling of a case-only rename");

    // Two directory entries that differ only by case. Skip when the volume
    // already treats photo.jpg as the Photo.JPG created above.
    writeFile(dir / "Photo.JPG", "upper");
    std::error_code clashEc;
    if (!fs::exists(dir / "photo.jpg", clashEc))
    {
        writeFile(dir / "photo.jpg", "lower");
        FileRenameCommand clash((dir / "Photo.JPG").string(), (dir / "photo.jpg").string(), folded);
        clash.execute();
        CHECK(!clash.lastError().empty() && readFile(dir / "Photo.JPG") == "upper" &&
                  readFile(dir / "photo.jpg") == "lower",
              "a different file whose name differs only by case is still a collision");
    }
}

void testDeleteWithoutPrivateTrash(const std::filesystem::path &dir,
                                   const std::filesystem::path &bin,
                                   const std::filesystem::path &source, int &g_pass, int &g_fail)
{
    namespace fs = std::filesystem;
    const fs::path blocked = dir / "not-a-directory";
    writeFile(blocked, "x");
    writeFile(source, "content");
    FileDeleteCommand viaBin({source.string()}, (blocked / "trash").string());
    viaBin.setSystemTrash(
        [&bin](const std::string &from, std::string &pathInTrash)
        {
            const fs::path target = bin / "via-bin.txt";
            std::error_code renameEc;
            fs::rename(from, target, renameEc);
            pathInTrash = target.string();
            return !renameEc;
        });
    viaBin.execute();
    CHECK(viaBin.lastError().empty() && !fs::exists(source) && fs::exists(bin / "via-bin.txt"),
          "system recycle bin is used when the private trash directory cannot be created");
    viaBin.undo();
    CHECK(viaBin.lastError().empty() && fs::exists(source) && !fs::exists(bin / "via-bin.txt"),
          "undo restores a system-trash delete that had no private directory");

    writeFile(source, "content");
    FileDeleteCommand neither({source.string()}, (blocked / "trash").string());
    neither.setSystemTrash([](const std::string &, std::string &) { return false; });
    neither.execute();
    CHECK(!neither.lastError().empty() && fs::exists(source),
          "delete fails when the recycle bin and the private trash directory both fail");
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
