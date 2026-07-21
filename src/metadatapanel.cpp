#include "metadatapanel.h"

#include "core/RatingStore.h"
#include "core/image/MetadataReader.h"
#include "widgets/ratingwidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
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

    // P1: star-rating editor, persists to RatingStore.
    auto *ratingBox = new QWidget(this);
    auto *ratingLay = new QHBoxLayout(ratingBox);
    ratingLay->setContentsMargins(0, 0, 0, 0);
    ratingLay->setSpacing(6);
    ratingLay->addWidget(new QLabel(tr("评分:"), this));
    m_rating = new RatingWidget(this);
    ratingLay->addWidget(m_rating);
    ratingLay->addStretch(1);
    connect(m_rating, &RatingWidget::ratingChanged, this, [this](int stars)
            {
                if (m_currentPath.isEmpty())
                    return;
                mviewer::core::RatingStore::instance().setRating(m_currentPath.toStdString(),
                                                                stars);
                emit ratingEdited(m_currentPath, stars);
            });

    layout->addWidget(ratingBox);

    // P3 tail: color label + reject / pick (favorite) controls.
    auto *flagBox = new QWidget(this);
    auto *flagLay = new QHBoxLayout(flagBox);
    flagLay->setContentsMargins(0, 0, 0, 0);
    flagLay->setSpacing(6);
    flagLay->addWidget(new QLabel(tr("色标:"), this));
    m_colorLabel = new QComboBox(this);
    m_colorLabel->addItem(tr("无"), 0);
    m_colorLabel->addItem(tr("红"), 1);
    m_colorLabel->addItem(tr("橙"), 2);
    m_colorLabel->addItem(tr("黄"), 3);
    m_colorLabel->addItem(tr("绿"), 4);
    m_colorLabel->addItem(tr("蓝"), 5);
    m_colorLabel->addItem(tr("紫"), 6);
    flagLay->addWidget(m_colorLabel);
    m_rejectBtn = new QPushButton(tr("拒绝"), this);
    m_rejectBtn->setCheckable(true);
    m_pickBtn = new QPushButton(tr("收藏"), this);
    m_pickBtn->setCheckable(true);
    flagLay->addWidget(m_rejectBtn);
    flagLay->addWidget(m_pickBtn);
    flagLay->addStretch(1);
    connect(m_colorLabel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_currentPath.isEmpty())
                    return;
                const int label = m_colorLabel->currentData().toInt();
                mviewer::core::RatingStore::instance().setColorLabel(m_currentPath.toStdString(),
                                                                     label);
                emit flagsEdited(m_currentPath, label, m_rejectBtn->isChecked(),
                                 m_pickBtn->isChecked());
            });
    connect(m_rejectBtn, &QPushButton::toggled, this, [this](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
                mviewer::core::RatingStore::instance().setRejected(m_currentPath.toStdString(),
                                                                   on);
                emit flagsEdited(m_currentPath, m_colorLabel->currentData().toInt(), on,
                                 m_pickBtn->isChecked());
            });
    connect(m_pickBtn, &QPushButton::toggled, this, [this](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
                mviewer::core::RatingStore::instance().setPicked(m_currentPath.toStdString(), on);
                emit flagsEdited(m_currentPath, m_colorLabel->currentData().toInt(),
                                 m_rejectBtn->isChecked(), on);
            });

    layout->addWidget(flagBox);
    layout->addWidget(m_table, 1);

    clear();
}

void MetadataPanel::clear()
{
    m_currentPath.clear();
    if (m_rating)
        m_rating->setRating(0);
    if (m_colorLabel)
        m_colorLabel->setCurrentIndex(0);
    if (m_rejectBtn)
        m_rejectBtn->setChecked(false);
    if (m_pickBtn)
        m_pickBtn->setChecked(false);
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
    m_currentPath = path;
    const auto &rs = mviewer::core::RatingStore::instance();
    if (m_rating)
        m_rating->setRating(path.isEmpty() ? 0 : rs.rating(path.toStdString()));
    if (m_colorLabel)
    {
        const int label = path.isEmpty() ? 0 : rs.colorLabel(path.toStdString());
        m_colorLabel->setCurrentIndex(m_colorLabel->findData(label));
    }
    if (m_rejectBtn)
        m_rejectBtn->setChecked(!path.isEmpty() && rs.rejected(path.toStdString()));
    if (m_pickBtn)
        m_pickBtn->setChecked(!path.isEmpty() && rs.picked(path.toStdString()));
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

    // M14-2: if the file is a RAW format, also show sensor metadata.
    const mviewer::core::RawMetadata rm = mviewer::core::parseRawMetadata(path.toStdString());
    if (rm.parsed)
        renderRaw(rm);
}

void MetadataPanel::renderRaw(const mviewer::core::RawMetadata &rm)
{
    addRow(tr("── RAW ──"), QString());
    if (!rm.make.empty())
        addRow(tr("相机厂商"), QString::fromStdString(rm.make));
    if (!rm.model.empty())
        addRow(tr("相机型号"), QString::fromStdString(rm.model));
    if (!rm.lens.empty())
        addRow(tr("镜头"), QString::fromStdString(rm.lens));
    if (rm.iso > 0)
        addRow(tr("ISO"), QString::number(rm.iso));
    if (rm.exposureSec > 0.0)
    {
        if (rm.exposureSec >= 1.0)
            addRow(tr("曝光时间"), QString("%1 s").arg(rm.exposureSec, 0, 'f', 2));
        else
        {
            int den = qRound(1.0 / rm.exposureSec);
            addRow(tr("曝光时间"), QString("1/%1 s").arg(den));
        }
    }
    if (rm.fNumber > 0.0)
        addRow(tr("光圈"), QString("f/%1").arg(rm.fNumber, 0, 'f', 1));
    if (rm.focalLength > 0.0)
        addRow(tr("焦距"), QString("%1 mm").arg(rm.focalLength, 0, 'f', 1));
    if (!rm.bayerPattern.empty())
        addRow(tr("Bayer 阵列"), QString::fromStdString(rm.bayerPattern));
    if (rm.blackLevel > 0)
        addRow(tr("黑电平"), QString::number(rm.blackLevel));
    if (rm.whiteLevel > 0)
        addRow(tr("白电平"), QString::number(rm.whiteLevel));
    if (!rm.whiteBalance.empty())
        addRow(tr("白平衡"), QString::fromStdString(rm.whiteBalance));
    m_table->resizeColumnsToContents();
}
