#include "batchdialog_panels.h"
#include "ThemeMetrics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace batchdialog_detail
{

QWidget *makeResizePanel(QSpinBox *&maxEdge)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchResizePanel"));
    auto *row = new QHBoxLayout(panel);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel("缩放最大边:"));
    maxEdge = new QSpinBox;
    maxEdge->setRange(64, 32768);
    maxEdge->setValue(1920);
    maxEdge->setSuffix(" px");
    row->addWidget(maxEdge);
    row->addStretch();
    return panel;
}

QWidget *makeCropPanel(QSpinBox *&x, QSpinBox *&y, QSpinBox *&w, QSpinBox *&h)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchCropPanel"));
    panel->setToolTip("Crop by pixel rectangle; clamp to the valid region when outside the image");
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    x = new QSpinBox;
    y = new QSpinBox;
    w = new QSpinBox;
    h = new QSpinBox;
    x->setObjectName(QStringLiteral("batchCropX"));
    y->setObjectName(QStringLiteral("batchCropY"));
    w->setObjectName(QStringLiteral("batchCropW"));
    h->setObjectName(QStringLiteral("batchCropH"));
    x->setRange(0, 100000);
    y->setRange(0, 100000);
    w->setRange(1, 100000);
    h->setRange(1, 100000);
    w->setValue(256);
    h->setValue(256);
    grid->addWidget(new QLabel("X:"), 0, 0);
    grid->addWidget(x, 0, 1);
    grid->addWidget(new QLabel("Y:"), 0, 2);
    grid->addWidget(y, 0, 3);
    grid->addWidget(new QLabel("宽:"), 1, 0);
    grid->addWidget(w, 1, 1);
    grid->addWidget(new QLabel("高:"), 1, 2);
    grid->addWidget(h, 1, 3);
    grid->setColumnStretch(4, 1);
    return panel;
}

QWidget *makeWatermarkPanel(QLineEdit *&text, QComboBox *&pos, QDoubleSpinBox *&opacity,
                            QSpinBox *&fontSize)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchWatermarkPanel"));
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(mviewer::ui::metrics::kSpace8);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel("水印文字:"));
    text = new QLineEdit;
    text->setPlaceholderText("© 2025");
    row1->addWidget(text, 1);
    row1->addWidget(new QLabel("Position:"));
    pos = new QComboBox;
    pos->addItems({"Top Left", "Top right", "Bottom Left", "Bottom Right", "Center", "Tile"});
    pos->setCurrentIndex(4);
    row1->addWidget(pos);
    lay->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel("不透明度:"));
    opacity = new QDoubleSpinBox;
    opacity->setRange(0.0, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setValue(0.3);
    row2->addWidget(opacity);
    row2->addWidget(new QLabel("字号:"));
    fontSize = new QSpinBox;
    fontSize->setRange(8, 200);
    fontSize->setValue(24);
    row2->addWidget(fontSize);
    row2->addStretch();
    lay->addLayout(row2);
    return panel;
}

QWidget *makeExportPanel(QComboBox *&format, QSpinBox *&quality, QLineEdit *&outputDir,
                         QPushButton *&browseBtn, QCheckBox *&overwriteExisting)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchExportPanel"));
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(mviewer::ui::metrics::kSpace8);
    auto *fmtRow = new QHBoxLayout;
    fmtRow->addWidget(new QLabel("导出格式:"));
    format = new QComboBox;
    format->addItems({"png", "jpg", "bmp", "webp"});
    fmtRow->addWidget(format);
    fmtRow->addWidget(new QLabel("Quality:"));
    quality = new QSpinBox;
    quality->setRange(1, 100);
    quality->setValue(90);
    fmtRow->addWidget(quality);
    fmtRow->addStretch();
    lay->addLayout(fmtRow);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(new QLabel("Output directory:"));
    outputDir = new QLineEdit;
    outputDir->setObjectName(QStringLiteral("batchOutputDir"));
    outputDir->setPlaceholderText("(Leave empty = original directory)");
    outputRow->addWidget(outputDir, 1);
    browseBtn = new QPushButton("Browse...");
    outputRow->addWidget(browseBtn);
    lay->addLayout(outputRow);
    overwriteExisting = new QCheckBox("覆盖已存在的文件");
    overwriteExisting->setObjectName(QStringLiteral("batchOverwriteExisting"));
    lay->addWidget(overwriteExisting);
    return panel;
}

} // namespace batchdialog_detail
