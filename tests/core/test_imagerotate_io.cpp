// ImageFileRotate / replaceFileContents I/O tests: real files, pixel checks.
#include "core/image/ImageFileReplace.h"
#include "core/image/ImageFileRotate.h"
#include "core/image/JpegExif.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QGuiApplication>
#include <QIODevice>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
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

using mviewer::core::FileReplaceResult;
using mviewer::core::flipImageFile;
using mviewer::core::ImageRotateError;
using mviewer::core::ImageRotateMethod;
using mviewer::core::readFileBytes;
using mviewer::core::replaceFileContents;
using mviewer::core::rotateImageFile;
using mviewer::core::spliceJpegApp1Exif;

struct RgbAvg
{
    int r = 0;
    int g = 0;
    int b = 0;
};

static std::string utf8Path(const QString &path)
{
    const QByteArray u = path.toUtf8();
    return std::string(u.constData(), static_cast<size_t>(u.size()));
}

static QImage makePattern()
{
    QImage img(40, 20, QImage::Format_RGB32);
    for (int y = 0; y < 20; ++y)
    {
        for (int x = 0; x < 40; ++x)
            img.setPixel(x, y, x < 20 ? qRgb(220, 20, 20) : qRgb(20, 20, 220));
    }
    return img;
}

static bool savePattern(const QImage &img, const QString &path, const char *format, int quality)
{
    QImageWriter writer(path, format);
    if (quality >= 0)
        writer.setQuality(quality);
    return writer.write(img);
}

static QImage loadAuto(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

static RgbAvg avgRect(const QImage &img, int x0, int y0, int x1, int y1)
{
    long long r = 0;
    long long g = 0;
    long long b = 0;
    int n = 0;
    const int xEnd = std::min(x1, img.width());
    const int yEnd = std::min(y1, img.height());
    for (int y = std::max(0, y0); y < yEnd; ++y)
    {
        for (int x = std::max(0, x0); x < xEnd; ++x)
        {
            const QRgb p = img.pixel(x, y);
            r += qRed(p);
            g += qGreen(p);
            b += qBlue(p);
            ++n;
        }
    }
    if (n == 0)
        return {};
    return {static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

static bool isReddish(const RgbAvg &c)
{
    return c.r >= c.b + 40 && c.r > 80;
}

static bool isBlueish(const RgbAvg &c)
{
    return c.b >= c.r + 40 && c.b > 80;
}

static void checkRotated90(const QString &path, const std::string &tag)
{
    const QImage img = loadAuto(path);
    CHECK(!img.isNull(), tag + " reload");
    CHECK(img.width() == 20 && img.height() == 40, tag + " size 20x40");
    const RgbAvg top = avgRect(img, 0, 0, img.width(), img.height() / 2);
    const RgbAvg bot = avgRect(img, 0, img.height() / 2, img.width(), img.height());
    CHECK(isReddish(top), tag + " top reddish");
    CHECK(isBlueish(bot), tag + " bottom blueish");
}

static void checkOriginalColors(const QString &path, const std::string &tag)
{
    const QImage img = loadAuto(path);
    CHECK(!img.isNull(), tag + " reload");
    CHECK(img.width() == 40 && img.height() == 20, tag + " size 40x20");
    const RgbAvg left = avgRect(img, 0, 0, img.width() / 2, img.height());
    const RgbAvg right = avgRect(img, img.width() / 2, 0, img.width(), img.height());
    CHECK(isReddish(left), tag + " left reddish");
    CHECK(isBlueish(right), tag + " right blueish");
}

static bool writerSupports(const char *format)
{
    return QImageWriter::supportedImageFormats().contains(QByteArray(format));
}

static void testRotateFormats(const QTemporaryDir &tmp)
{
    std::cout << "\n[rotate 90 CW formats]\n";
    const QImage src = makePattern();
    struct Case
    {
        const char *ext;
        const char *format;
        int quality;
    };
    const Case cases[] = {
        {"png", "png", -1},
        {"bmp", "bmp", -1},
        {"jpg", "jpeg", 100},
        {"webp", "webp", 100},
    };
    for (const Case &c : cases)
    {
        if (!writerSupports(c.format))
        {
            std::cout << "SKIP: writer has no " << c.format << "\n";
            continue;
        }
        const QString path = tmp.filePath(QString("fmt.") + c.ext);
        CHECK(savePattern(src, path, c.format, c.quality), std::string("save ") + c.ext);
        const auto r = rotateImageFile(utf8Path(path), 90);
        CHECK(r.ok, std::string("CW90 ok .") + c.ext + " err=" + r.error);
        checkRotated90(path, std::string(".") + c.ext);
    }
}

static void testChineseNames(const QTemporaryDir &tmp)
{
    std::cout << "\n[Chinese path round-trip]\n";
    // i18n-allow-begin: non-ASCII path fixtures (exercise Unicode file and folder names)
    const QString folder = tmp.filePath(QString::fromUtf8("测试 目录"));
    const QString png = folder + QString::fromUtf8("/旋转 图片.png");
    const QString jpg = folder + QString::fromUtf8("/旋转 图片.jpg");
    // i18n-allow-end
    CHECK(QDir().mkpath(folder), "mkdir chinese folder");
    const QImage src = makePattern();
    CHECK(savePattern(src, png, "png", -1), "save chinese png");
    CHECK(savePattern(src, jpg, "jpeg", 100), "save chinese jpg");

    auto png90 = rotateImageFile(utf8Path(png), 90);
    CHECK(png90.ok, "chinese png CW90 err=" + png90.error);
    auto pngBack = rotateImageFile(utf8Path(png), -90);
    CHECK(pngBack.ok, "chinese png CCW90 err=" + pngBack.error);
    checkOriginalColors(png, "chinese png round-trip");

    auto jpg90 = rotateImageFile(utf8Path(jpg), 90);
    CHECK(jpg90.ok, "chinese jpg CW90 err=" + jpg90.error);
    auto jpgBack = rotateImageFile(utf8Path(jpg), -90);
    CHECK(jpgBack.ok, "chinese jpg CCW90 err=" + jpgBack.error);
    checkOriginalColors(jpg, "chinese jpg round-trip");
}

static void testHeldOpen(const QTemporaryDir &tmp)
{
    std::cout << "\n[file held open]\n";
    const QString path = tmp.filePath(QStringLiteral("held.png"));
    CHECK(savePattern(makePattern(), path, "png", -1), "save held png");
    {
        QFile holder(path);
        CHECK(holder.open(QIODevice::ReadOnly), "open target ReadOnly");
        const auto r = rotateImageFile(utf8Path(path), 90);
        CHECK(r.ok, "rotate while QFile held err=" + r.error);
    }
    checkRotated90(path, "held QFile");
}

static void testHeldOpenThread(const QTemporaryDir &tmp)
{
    std::cout << "\n[file held open on a thread]\n";
    const QString path = tmp.filePath(QStringLiteral("held_thread.png"));
    CHECK(savePattern(makePattern(), path, "png", -1), "save thread-held png");
    std::atomic<bool> opened{false};
    std::thread holder(
        [&]()
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return;
            opened.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
        });
    const auto start = std::chrono::steady_clock::now();
    while (!opened.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto r = rotateImageFile(utf8Path(path), 90);
    holder.join();
    CHECK(opened.load(), "thread holder opened the file");
    CHECK(r.ok, "rotate while thread held err=" + r.error);
    checkRotated90(path, "thread-held");
}

static void testFlipH(const QTemporaryDir &tmp)
{
    std::cout << "\n[flip horizontal]\n";
    const QString path = tmp.filePath(QStringLiteral("flip.png"));
    CHECK(savePattern(makePattern(), path, "png", -1), "save flip png");
    const auto r = flipImageFile(utf8Path(path), true);
    CHECK(r.ok, "H-flip ok err=" + r.error);
    const QImage img = loadAuto(path);
    CHECK(!img.isNull() && img.width() == 40 && img.height() == 20, "H-flip size");
    const RgbAvg left = avgRect(img, 0, 0, img.width() / 2, img.height());
    const RgbAvg right = avgRect(img, img.width() / 2, 0, img.width(), img.height());
    CHECK(isBlueish(left), "H-flip left is original right (blue)");
    CHECK(isReddish(right), "H-flip right is original left (red)");
}

static std::vector<uint8_t> makeExifApp1(int orientation)
{
    std::vector<uint8_t> tiff = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                                 0x00, 0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00};
    tiff.push_back(static_cast<uint8_t>(orientation));
    tiff.insert(tiff.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
    payload.insert(payload.end(), tiff.begin(), tiff.end());
    std::vector<uint8_t> out = {0xFF, 0xE1};
    const int len = static_cast<int>(payload.size() + 2);
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static bool writeBytes(const QString &path, const std::vector<uint8_t> &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 n = static_cast<qint64>(bytes.size());
    return file.write(reinterpret_cast<const char *>(bytes.data()), n) == n;
}

static void testJpegExif(const QTemporaryDir &tmp)
{
    std::cout << "\n[JPEG EXIF orientation]\n";
    const QString path = tmp.filePath(QStringLiteral("exif.jpg"));
    CHECK(savePattern(makePattern(), path, "jpeg", 100), "save exif jpeg");
    std::vector<uint8_t> jpeg;
    std::string err;
    CHECK(readFileBytes(utf8Path(path), jpeg, &err), "read jpeg bytes");
    jpeg = spliceJpegApp1Exif(jpeg, makeExifApp1(1), false);
    CHECK(mviewer::core::readJpegOrientation(jpeg) == 1, "spliced orientation 1");
    CHECK(writeBytes(path, jpeg), "write tagged jpeg");
    const auto r = rotateImageFile(utf8Path(path), 90);
    CHECK(r.ok, "EXIF rotate ok err=" + r.error);
    CHECK(r.method == ImageRotateMethod::ExifOrientation, "method is ExifOrientation");
    const QImage img = loadAuto(path);
    CHECK(!img.isNull() && img.width() == 20 && img.height() == 40, "EXIF autoTransform size");
}

static void testUnsupportedAndMissing(const QTemporaryDir &tmp)
{
    std::cout << "\n[unsupported and missing]\n";
    const QString tif = tmp.filePath(QStringLiteral("nope.tif"));
    const QByteArray payload("not a tiff file");
    {
        QFile file(tif);
        CHECK(file.open(QIODevice::WriteOnly), "write fake tif");
        file.write(payload);
    }
    const auto unsupported = rotateImageFile(utf8Path(tif), 90);
    CHECK(!unsupported.ok && unsupported.errorCode == ImageRotateError::UnsupportedFormat,
          "tif is UnsupportedFormat");
    {
        QFile file(tif);
        CHECK(file.open(QIODevice::ReadOnly), "reopen tif");
        CHECK(file.readAll() == payload, "tif bytes unchanged");
    }

    const QString missing = tmp.filePath(QStringLiteral("missing.png"));
    const auto notFound = rotateImageFile(utf8Path(missing), 90);
    CHECK(!notFound.ok && notFound.errorCode == ImageRotateError::NotFound, "missing is NotFound");
}

static void testReadOnlyDir(const QTemporaryDir &tmp)
{
#ifndef Q_OS_WIN
    std::cout << "\n[read-only folder, writable file]\n";
    const QString path = tmp.filePath(QStringLiteral("locked_dir.png"));
    CHECK(savePattern(makePattern(), path, "png", -1), "save in writable dir");
    const QString dirPath = tmp.path();
    const QFileDevice::Permissions previous = QFile::permissions(dirPath);
    const QFileDevice::Permissions locked = QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                            QFileDevice::ReadOther | QFileDevice::ExeOther;
    CHECK(QFile::setPermissions(dirPath, locked), "chmod 0555 folder");
    const auto r = rotateImageFile(utf8Path(path), 90);
    QFile::setPermissions(dirPath, previous);
    CHECK(r.ok, "rotate in read-only folder err=" + r.error);
    checkRotated90(path, "read-only folder");
#else
    (void)tmp;
#endif
}

static void testReplaceDirect(const QTemporaryDir &tmp)
{
    std::cout << "\n[replaceFileContents]\n";
    const QString dirPath = tmp.filePath(QStringLiteral("replace_dir"));
    CHECK(QDir().mkpath(dirPath), "mkdir replace_dir");
    const QString path = dirPath + QStringLiteral("/target.bin");
    CHECK(writeBytes(path, {'o', 'l', 'd'}), "seed target.bin");
    const std::vector<uint8_t> next = {'n', 'e', 'w', '!'};
    const FileReplaceResult r = replaceFileContents(utf8Path(path), next);
    CHECK(r.ok, "replaceFileContents ok err=" + r.error);
    std::vector<uint8_t> got;
    std::string err;
    CHECK(readFileBytes(utf8Path(path), got, &err) && got == next, "new contents written");
    const QStringList files =
        QDir(dirPath).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    CHECK(files.size() == 1 && files.front() == QStringLiteral("target.bin"),
          "no stray files left in the directory");
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");
    tmp.setAutoRemove(true);

    testRotateFormats(tmp);
    testChineseNames(tmp);
    testHeldOpen(tmp);
    testHeldOpenThread(tmp);
    testFlipH(tmp);
    testJpegExif(tmp);
    testUnsupportedAndMissing(tmp);
    testReadOnlyDir(tmp);
    testReplaceDirect(tmp);

    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "\nAll imagerotate_io tests passed.\n";
    return 0;
}
