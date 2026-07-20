#include "metadatapanel.h"

#include "core/image/MetadataReader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("元数据 (Metadata)"), this));

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("字段"), tr("值")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);

    clear();
}

void MetadataPanel::clear()
{
    m_table->setRowCount(0);
    addRow(tr("提示"), tr("在画廊中选择一张图片以查看元数据"));
}

void MetadataPanel::addRow(const QString &key, const QString &value)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(key));
    m_table->setItem(r, 1, new QTableWidgetItem(value));
}

void MetadataPanel::render(const mviewer::domain::ImageMetadata &meta)
{
    m_table->setRowCount(0);

    addRow(tr("文件名"), QString::fromStdString(meta.fileName));
    addRow(tr("路径"), QString::fromStdString(meta.filePath));
    addRow(tr("格式"), QString::fromStdString(meta.format).isEmpty() ? tr("(未知)")
                                                                     : QString::fromStdString(meta.format));
    addRow(tr("尺寸"), QString("%1 × %2 px")
                           .arg(meta.width)
                           .arg(meta.height));
    const double mp = (meta.width > 0 && meta.height > 0)
                          ? (meta.width * meta.height) / 1'000'000.0
                          : 0.0;
    if (mp > 0.0)
        addRow(tr("像素数"), QString::number(mp, 'f', 2) + " MP");
    addRow(tr("文件大小"), QString::number(meta.fileSize / 1024.0, 'f', 1) + " KB");
    addRow(tr("色深"), meta.bitDepth > 0 ? QString::number(meta.bitDepth) + " bit" : tr("(未知)"));
    addRow(tr("通道"), meta.channels > 0 ? QString::number(meta.channels) : tr("(未知)"));
    addRow(tr("色彩空间"), QString::fromStdString(meta.colorSpace).isEmpty()
                               ? tr("(未知)")
                               : QString::fromStdString(meta.colorSpace));
    if (meta.dpiX > 0 || meta.dpiY > 0)
        addRow(tr("分辨率"), QString("%1 × %2 DPI").arg(meta.dpiX).arg(meta.dpiY));
    addRow(tr("EXIF 方向"), QString::number(meta.orientation));
    addRow(tr("ICC 色彩配置"), meta.hasIccProfile ? tr("已嵌入") : tr("无"));

    const QDateTime mod = QDateTime::fromSecsSinceEpoch(meta.modifiedEpochSec);
    addRow(tr("修改时间"),
           mod.isValid() ? mod.toString("yyyy-MM-dd HH:mm:ss") : tr("(未知)"));

    // Embedded EXIF/XMP/IPTCCore text keys (best-effort, plugin-dependent).
    if (!meta.textKeys.empty())
    {
        for (const auto &[k, v] : meta.textKeys)
            addRow(QString::fromStdString(k), QString::fromStdString(v));
    }

    m_table->resizeColumnsToContents();
}

void MetadataPanel::setImage(const QString &path)
{
    if (path.isEmpty())
    {
        clear();
        return;
    }
    const mviewer::domain::ImageMetadata meta =
        mviewer::core::MetadataReader::read(path.toStdString());
    if (meta.filePath.empty())
    {
        clear();
        addRow(tr("错误"), tr("无法读取: ") + QFileInfo(path).fileName());
        return;
    }
    render(meta);
}
