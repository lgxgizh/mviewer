// NOLINTBEGIN
#include "yuvcomparedialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <limits>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    std::cout << (condition ? "[ok] " : "[FAIL] ") << message << "\n";
    if (!condition)
    {
        ++g_failures;
    }
}

bool writeRaw(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

void waitUntilFinished(YuvCompareDialog *dialog)
{
    QElapsedTimer timer;
    timer.start();
    while (!dialog->isFinished() && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

void testPsnrAndGrayRow()
{
    const double inf = std::numeric_limits<double>::infinity();
    check(YuvCompareDialog::psnrText(inf) == QStringLiteral("inf"), "infinite PSNR prints inf");
    check(YuvCompareDialog::psnrText(42.126) == QStringLiteral("42.13"),
          "PSNR rounds to two decimals");

    mviewer::core::yuv::YuvFrameMetrics metrics;
    metrics.valid = true;
    metrics.planeCount = 1;
    metrics.planes[0].psnr = 10.0;
    const QString row = YuvCompareDialog::frameRowText(0, metrics);
    check(row.contains(QStringLiteral("-")), "gray frame row dashes chroma cells");
}

void testIdenticalPair(const QString &pathA, const QString &pathB)
{
    YuvCompareDialog dialog(pathA, pathB);
    dialog.start();
    waitUntilFinished(&dialog);
    check(dialog.isFinished(), "identical pair finishes");
    const QString text = dialog.resultText();
    check(text.contains(QStringLiteral("inf")), "identical frames report inf PSNR");
    check(text.contains(QStringLiteral("2 frames")), "report names two frames");
}

void testMissingPath(const QString &pathA)
{
    YuvCompareDialog dialog(pathA, QStringLiteral("/no/such/yuv-compare-missing.yuv"));
    dialog.start();
    waitUntilFinished(&dialog);
    check(dialog.isFinished(), "missing path finishes");
    check(dialog.resultText().contains(QStringLiteral("Cannot compare as YUV")),
          "missing path explains the failure");
}

void testDeleteWhileRunning(const QString &pathA, const QString &pathB)
{
    auto *dialog = new YuvCompareDialog(pathA, pathB);
    dialog->start();
    delete dialog;
    check(true, "deleting a running dialog returns");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testPsnrAndGrayRow();

    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory is created");
    const QString folder = temp.filePath(QStringLiteral("04x02"));
    check(QDir().mkpath(folder), "04x02 folder is created");
    const QByteArray raw(24, '\0');
    const QString pathA = QDir(folder).filePath(QStringLiteral("a.yuv"));
    const QString pathB = QDir(folder).filePath(QStringLiteral("b.yuv"));
    check(writeRaw(pathA, raw) && writeRaw(pathB, raw), "two 2-frame 4x2 files are written");

    testIdenticalPair(pathA, pathB);
    testMissingPath(pathA);
    testDeleteWhileRunning(pathA, pathB);
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
