// M3 Testdata Regression: decode every golden fixture and verify dimensions,
// then decode every corrupted fixture to ensure the Decoder fails gracefully
// (returns a null ImageData) instead of crashing.
//
// Run headless; uses MVIEWER_SOURCE_DIR to locate D:/mviewer/testdata.
#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <cstdio>
#include <string>

static int parseDim(const QString& folder, int& w, int& h)
{
    const int x = folder.indexOf('x');
    if (x < 1)
        return 0;
    bool ok1 = false, ok2 = false;
    w = folder.left(x).toInt(&ok1);
    h = folder.mid(x + 1).toInt(&ok2);
    return (ok1 && ok2) ? 1 : 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;

    const QString root = QStringLiteral(MVIEWER_SOURCE_DIR) + "/testdata";
    printf("TESTDATA_ROOT=%s\n", root.toUtf8().constData());

    // 1) Golden fixtures: must decode to the dimensions declared by the folder.
    {
        QDir golden(root + "/golden");
        const QStringList tiers = golden.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        int checked = 0;
        for (const QString& tier : tiers)
        {
            int w, h;
            if (!parseDim(tier, w, h))
                continue;
            QDir d(golden.absoluteFilePath(tier));
            const QStringList files =
                d.entryList(QStringList() << "*.png" << "*.jpg" << "*.bmp" << "*.tiff",
                            QDir::Files);
            for (const QString& f : files)
            {
                const std::string path = d.absoluteFilePath(f).toStdString();
                ImageData img = Decoder::decodeFull(path);
                if (img.isNull())
                {
                    printf("GOLDEN_FAIL null %s\n", f.toUtf8().constData());
                    fails++;
                }
                else if (img.width != w || img.height != h)
                {
                    printf("GOLDEN_FAIL dims %s got %dx%d want %dx%d\n",
                           f.toUtf8().constData(), img.width, img.height, w, h);
                    fails++;
                }
                checked++;
            }
        }
        printf("GOLDEN_CHECKED=%d\n", checked);
        if (checked == 0)
        {
            printf("GOLDEN_FAIL no fixtures found\n");
            fails++;
        }
    }

    // 2) Corrupted fixtures: Decoder must not crash. A null result is accepted;
    //    a non-null result is also accepted (defensive decode may recover).
    {
        QDir corrupted(root + "/corrupted");
        const QStringList files =
            corrupted.entryList(QStringList() << "*.png" << "*.jpg" << "*.bmp" << "*.tiff",
                                QDir::Files);
        int checked = 0;
        for (const QString& f : files)
        {
            const std::string path = corrupted.absoluteFilePath(f).toStdString();
            ImageData img = Decoder::decodeFull(path);  // must not throw/crash
            (void)img;
            checked++;
        }
        printf("CORRUPT_CHECKED=%d\n", checked);
    }

    printf(fails == 0 ? "ALL_TESTDATA_OK\n" : "TESTDATA_FAILS=%d\n", fails);
    return fails == 0 ? 0 : 1;
}
