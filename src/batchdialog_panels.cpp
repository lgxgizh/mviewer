#include "batchdialog_panels.h"
#include "ThemeMetrics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
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
    row->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Max edge:")));
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
    panel->setToolTip(QCoreApplication::translate(
        "BatchDialog",
        "Crop by pixel rectangle; clamp to the valid region when outside the image"));
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
    grid->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "X:")), 0, 0);
    grid->addWidget(x, 0, 1);
    grid->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Y:")), 0, 2);
    grid->addWidget(y, 0, 3);
    grid->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "W:")), 1, 0);
    grid->addWidget(w, 1, 1);
    grid->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "H:")), 1, 2);
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
    row1->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Watermark text:")));
    text = new QLineEdit;
    text->setPlaceholderText(QCoreApplication::translate("BatchDialog", "© 2025"));
    row1->addWidget(text, 1);
    row1->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Position:")));
    pos = new QComboBox;
    pos->addItem(QCoreApplication::translate("BatchDialog", "Top Left"));
    pos->addItem(QCoreApplication::translate("BatchDialog", "Top right"));
    pos->addItem(QCoreApplication::translate("BatchDialog", "Bottom Left"));
    pos->addItem(QCoreApplication::translate("BatchDialog", "Bottom Right"));
    pos->addItem(QCoreApplication::translate("BatchDialog", "Center"));
    pos->addItem(QCoreApplication::translate("BatchDialog", "Tile"));
    pos->setCurrentIndex(4);
    row1->addWidget(pos);
    lay->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Opacity:")));
    opacity = new QDoubleSpinBox;
    opacity->setRange(0.0, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setValue(0.3);
    row2->addWidget(opacity);
    row2->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Font size:")));
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
    fmtRow->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Export format:")));
    format = new QComboBox;
    // currentText() is the format id written into the batch config.
    format->addItem(QStringLiteral("png"));
    format->addItem(QStringLiteral("jpg"));
    format->addItem(QStringLiteral("bmp"));
    format->addItem(QStringLiteral("webp"));
    fmtRow->addWidget(format);
    fmtRow->addWidget(new QLabel(QCoreApplication::translate("BatchDialog", "Quality:")));
    quality = new QSpinBox;
    quality->setRange(1, 100);
    quality->setValue(90);
    fmtRow->addWidget(quality);
    fmtRow->addStretch();
    lay->addLayout(fmtRow);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(
        new QLabel(QCoreApplication::translate("BatchDialog", "Output directory:")));
    outputDir = new QLineEdit;
    outputDir->setObjectName(QStringLiteral("batchOutputDir"));
    outputDir->setPlaceholderText(
        QCoreApplication::translate("BatchDialog", "(Leave empty = original directory)"));
    outputRow->addWidget(outputDir, 1);
    browseBtn = new QPushButton(QCoreApplication::translate("BatchDialog", "Browse..."));
    outputRow->addWidget(browseBtn);
    lay->addLayout(outputRow);
    overwriteExisting =
        new QCheckBox(QCoreApplication::translate("BatchDialog", "Overwrite existing files"));
    overwriteExisting->setObjectName(QStringLiteral("batchOverwriteExisting"));
    lay->addWidget(overwriteExisting);
    return panel;
}

} // namespace batchdialog_detail
