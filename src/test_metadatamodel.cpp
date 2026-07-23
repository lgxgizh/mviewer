// M15 P0#4 Metadata Center acceptance: the unified MetadataModel aggregates
// file-level ImageMetadata, embedded EXIF/XMP text keys, and RAW sensor
// metadata into grouped tree sections (File / Image / EXIF / RAW) and exposes
// them through the QAbstractItemModel API. This test drives the model with
// in-memory metadata (no disk I/O) and asserts the tree shape + values.
#include "metadatamodel.h"

#include <QCoreApplication>

#include <cstdio>
#include <map>
#include <string>

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

static QModelIndex findCategory(MetadataModel *m, const QString &name)
{
    for (int r = 0; r < m->rowCount(); ++r)
    {
        QModelIndex idx = m->index(r, 0);
        if (m->data(idx).toString() == name)
            return idx;
    }
    return {};
}

static QString leafValue(MetadataModel *m, const QModelIndex &cat, const QString &key)
{
    if (!cat.isValid())
        return QString("<no-category:%1>").arg(key);
    for (int r = 0; r < m->rowCount(cat); ++r)
    {
        QModelIndex k = m->index(r, 0, cat);
        if (m->data(k).toString() == key)
            return m->data(m->index(r, 1, cat)).toString();
    }
    return QString("<missing:%1>").arg(key);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("MetadataModel: unified metadata tree\n");

    // ── Empty model shows the hint category ────────────────────────────────
    {
        MetadataModel m;
        m.clear();
        CHECK(m.rowCount() == 1, "empty model has one (hint) category");
        QModelIndex hint = findCategory(&m, "提示");
        CHECK(hint.isValid(), "empty model shows 提示 category");
        CHECK(leafValue(&m, hint, "说明").contains("画廊"), "empty model hint mentions gallery");
        CHECK(m.columnCount() == 2, "model exposes 2 columns");
    }

    // ── Basic image metadata → File + Image sections ────────────────────────
    {
        mviewer::domain::ImageMetadata meta;
        meta.fileName = "sample.png";
        meta.filePath = "/pics/sample.png";
        meta.fileSize = 123456;
        meta.format = "PNG";
        meta.width = 1920;
        meta.height = 1080;
        meta.bitDepth = 8;
        meta.channels = 3;
        meta.colorSpace = "sRGB";
        meta.dpiX = 72;
        meta.dpiY = 72;
        meta.orientation = 6;
        meta.hasIccProfile = true;
        meta.textKeys["Make"] = "CameraCo";
        meta.textKeys["Model"] = "X100";

        MetadataModel m;
        m.setImage(meta);

        CHECK(m.rowCount() == 3, "jpg+exif yields File/Image/EXIF (3) categories");

        QModelIndex file = findCategory(&m, "文件信息");
        CHECK(file.isValid(), "File category present");
        CHECK(leafValue(&m, file, "文件名") == "sample.png", "file name leaf");
        CHECK(leafValue(&m, file, "路径") == "/pics/sample.png", "file path leaf");
        CHECK(leafValue(&m, file, "大小").contains("123456"), "file size leaf");

        QModelIndex img = findCategory(&m, "图像信息");
        CHECK(img.isValid(), "Image category present");
        CHECK(leafValue(&m, img, "格式") == "PNG", "format leaf");
        CHECK(leafValue(&m, img, "尺寸").contains("1920"), "dimensions leaf shows width");
        CHECK(leafValue(&m, img, "长宽比") == "16:9", "aspect ratio 16:9");
        CHECK(leafValue(&m, img, "像素量").contains("2.07"), "megapixel leaf");
        CHECK(leafValue(&m, img, "通道数") == "3", "channels leaf");
        CHECK(leafValue(&m, img, "色彩空间") == "sRGB", "color space leaf");
        CHECK(leafValue(&m, img, "方向").contains("90"), "orientation leaf mentions 90");
        CHECK(leafValue(&m, img, "ICC 配置") == "已嵌入", "ICC leaf");

        QModelIndex exif = findCategory(&m, "EXIF / 元数据");
        CHECK(exif.isValid(), "EXIF category present");
        CHECK(leafValue(&m, exif, "Make") == "CameraCo", "exif make leaf");
        CHECK(leafValue(&m, exif, "Model") == "X100", "exif model leaf");
    }

    // ── RAW metadata adds the RAW sensor section ────────────────────────────
    {
        mviewer::domain::ImageMetadata meta;
        meta.fileName = "shot.cr2";
        meta.filePath = "/raw/shot.cr2";
        meta.format = "CR2";
        meta.width = 4000;
        meta.height = 3000;
        meta.textKeys["Make"] = "Canon";

        mviewer::core::RawMetadata rm;
        rm.parsed = true;
        rm.make = "Canon";
        rm.model = "EOS R5";
        rm.lens = "RF 35mm F1.8";
        rm.iso = 400;
        rm.exposureSec = 0.0025;
        rm.fNumber = 2.8;
        rm.focalLength = 35.0;
        rm.bitsPerSample = 14;
        rm.width = 8192;
        rm.height = 5464;

        MetadataModel m;
        m.setImage(meta);
        m.setRaw(rm);

        CHECK(m.rowCount() == 4, "file+image+exif+raw yields 4 categories");
        QModelIndex raw = findCategory(&m, "RAW 传感器");
        CHECK(raw.isValid(), "RAW category present");
        CHECK(leafValue(&m, raw, "相机厂商") == "Canon", "raw make leaf");
        CHECK(leafValue(&m, raw, "相机型号") == "EOS R5", "raw model leaf");
        CHECK(leafValue(&m, raw, "镜头") == "RF 35mm F1.8", "raw lens leaf");
        CHECK(leafValue(&m, raw, "ISO") == "400", "raw iso leaf");
        CHECK(leafValue(&m, raw, "光圈") == "f/2.8", "raw aperture leaf");
        CHECK(leafValue(&m, raw, "焦距").contains("35"), "raw focal leaf");
        CHECK(leafValue(&m, raw, "采样位深") == "14 位", "raw bits leaf");
        CHECK(leafValue(&m, raw, "原始尺寸").contains("8192"), "raw size leaf");
    }

    // ── clear() returns to the hint state ───────────────────────────────────
    {
        mviewer::domain::ImageMetadata meta;
        meta.fileName = "x.jpg";
        meta.filePath = "/x.jpg";
        MetadataModel m;
        m.setImage(meta);
        CHECK(m.rowCount() == 2, "pre-clear has File/Image categories");
        m.clear();
        CHECK(m.rowCount() == 1, "clear() resets to hint category");
        CHECK(findCategory(&m, "提示").isValid(), "clear() shows hint again");
    }

    printf("\nMetadataModel tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
