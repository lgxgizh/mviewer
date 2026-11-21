// RotateFilesCommand undo/redo for EXIF tag edits and pixel-backup rewrites.
#include "core/command/CommandStack.h"
#include "core/command/RotateFilesCommand.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/JpegExif.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QString>

#include <iostream>
#include <string>
#include <vector>

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

static std::vector<uint8_t> readAll(const std::string &path)
{
    std::vector<uint8_t> bytes;
    std::string error;
    mviewer::core::readFileBytes(path, bytes, &error);
    return bytes;
}

static bool writeAll(const QString &path, const std::vector<uint8_t> &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
}

static std::vector<uint8_t> taggedJpeg(int orientation)
{
    std::vector<uint8_t> tiff = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                                 0x00, 0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00};
    tiff.push_back(static_cast<uint8_t>(orientation));
    tiff.insert(tiff.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
    payload.insert(payload.end(), tiff.begin(), tiff.end());
    std::vector<uint8_t> out = {0xFF, 0xD8, 0xFF, 0xE1};
    out.push_back(0x00);
    out.push_back(static_cast<uint8_t>(payload.size() + 2));
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), {0xFF, 0xD9});
    return out;
}

static std::string writePng(const QString &path, int w, int h)
{
    ImageData image = makeImageData(w, h, PixelFormat::RGB24);
    std::fill(image.buffer->data(), image.buffer->data() + static_cast<size_t>(w * h * 3), 90);
    Encoder::Params params;
    params.quality = -1;
    Encoder::encode(image, path.toStdString(), params);
    return path.toStdString();
}

static QSize readSize(const std::string &path)
{
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    return image.size();
}

static void testExifUndo()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/mviewer_rotate_exif");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/a.jpg");
    const auto original = taggedJpeg(6);
    CHECK(writeAll(path, original), "write exif fixture");
    CommandStack stack;
    auto command = std::make_unique<RotateFilesCommand>(
        std::vector<std::string>{path.toStdString()}, RotateFilesCommand::Op::Rotate, 90);
    CHECK(command->description() == "Rotate 1 file", "rotate description is singular");
    command->execute();
    CHECK(command->canUndo(), "exif rotate can undo");
    CHECK(!command->blockedByPerFileLimit(), "exif rotate does not need a byte backup");
    CHECK(mviewer::core::readJpegOrientation(readAll(path.toStdString())) == 3, "tag became 3");
    CHECK(stack.recordExecuted(std::move(command)), "exif command is recorded");
    CHECK(stack.undo(), "exif undo");
    CHECK(readAll(path.toStdString()) == original, "exif undo restores the original bytes");
    CHECK(stack.redo(), "exif redo");
    CHECK(mviewer::core::readJpegOrientation(readAll(path.toStdString())) == 3,
          "exif redo reapplies");
    QDir(dir).removeRecursively();
}

static void testPixelBackup()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/mviewer_rotate_png");
    QDir().mkpath(dir);
    const std::string path = writePng(dir + QStringLiteral("/a.png"), 8, 4);
    const auto original = readAll(path);
    CHECK(!original.empty(), "png fixture was encoded");
    CommandStack stack;
    auto command = std::make_unique<RotateFilesCommand>(std::vector<std::string>{path},
                                                        RotateFilesCommand::Op::Rotate, 90);
    command->execute();
    CHECK(command->canUndo() && !command->blockedByPerFileLimit(), "png backup is undoable");
    CHECK(readSize(path) == QSize(4, 8), "png pixels rotated");
    CHECK(stack.recordExecuted(std::move(command)), "png command is recorded");
    CHECK(stack.undo(), "png undo");
    CHECK(readAll(path) == original, "png undo restores the original bytes");
    CHECK(stack.redo(), "png redo");
    CHECK(readSize(path) == QSize(4, 8), "png redo rotates again");
    QDir(dir).removeRecursively();
}

static void testLimits()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/mviewer_rotate_limit");
    QDir().mkpath(dir);
    const std::string one = writePng(dir + QStringLiteral("/one.png"), 8, 4);
    const auto original = readAll(one);
    RotateFilesCommand::setBackupLimits(16, 1024ull * 1024ull);
    {
        RotateFilesCommand command({one}, RotateFilesCommand::Op::Rotate, 90);
        command.execute();
        CHECK(command.successCount() == 1, "oversized file is still rotated");
        CHECK(command.blockedByPerFileLimit(), "per-file cap blocks the backup");
        CHECK(!command.canUndo(), "oversized rotate is not undoable");
        CHECK(readSize(one) == QSize(4, 8), "pixels changed despite the cap");
    }
    writeAll(QString::fromStdString(one), original);

    const std::string two = writePng(dir + QStringLiteral("/two.png"), 8, 4);
    const auto bytesOne = readAll(one);
    const auto bytesTwo = readAll(two);
    const auto total = bytesOne.size() + bytesTwo.size() - 1;
    RotateFilesCommand::setBackupLimits(1024ull * 1024ull, static_cast<std::uint64_t>(total));
    {
        RotateFilesCommand first({one}, RotateFilesCommand::Op::FlipH, 0);
        first.execute();
        CHECK(first.canUndo(), "first backup fits alone");
        RotateFilesCommand second({two}, RotateFilesCommand::Op::FlipH, 0);
        second.execute();
        CHECK(second.droppedOldestBackup(), "second backup drops the oldest");
        CHECK(!first.canUndo(), "dropped backup can no longer undo");
        CHECK(second.canUndo(), "the newest backup stays undoable");
        second.undo();
        CHECK(readAll(two) == bytesTwo, "undo restores the file that kept its backup");
    }
    RotateFilesCommand::resetBackupBudget();
    QDir(dir).removeRecursively();
}

static void testDescriptions()
{
    RotateFilesCommand rotate({std::string("a"), std::string("b"), std::string("c")},
                              RotateFilesCommand::Op::Rotate, 90);
    CHECK(rotate.description() == "Rotate 3 files", "rotate description counts files");
    RotateFilesCommand flip({std::string("a")}, RotateFilesCommand::Op::FlipV, 0);
    CHECK(flip.description() == "Flip 1 file", "flip description is singular");
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    RotateFilesCommand::resetBackupBudget();
    testExifUndo();
    testPixelBackup();
    testLimits();
    testDescriptions();
    RotateFilesCommand::resetBackupBudget();
    if (g_fail > 0)
    {
        std::cerr << "rotatefiles_tests: FAIL (" << g_fail << ")\n";
        return 1;
    }
    std::cout << "rotatefiles_tests: PASS\n";
    return 0;
}
