#include "Theme.h"
#include "ThemeMetrics.h"
#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "widgets/rawimageview.h"
#include <QSettings>

#include "core/image/QtConvert.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace
{
QString analysisReadoutStyle(bool monospace)
{
    QString extra = monospace ? QStringLiteral("font-family:monospace;") : QString();
    return QStringLiteral("QLabel{background:%1;color:%2;padding:8px;%3}")
        .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg0),
             mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary), extra);
}
} // namespace

void AnalysisPanel::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace8,
                                   mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace8);
    mainLayout->setSpacing(4);
    buildAnalyzerSection(*mainLayout);
    buildHistorySection(*mainLayout);
    buildResultTabs(*mainLayout);
    buildInspectorTab();
    m_analyzerCombo->setCurrentIndex(0);
    onAnalyzerSelected(0);
    updateHistogramPage();
}

void AnalysisPanel::buildAnalyzerSection(QVBoxLayout &layout)
{
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Analyzer:")));
    m_analyzerCombo = new QComboBox;
    fillAnalyzerCombo(QString());
    bar->addWidget(m_analyzerCombo, 1);

    auto *runButton = new QPushButton(tr("Run"));
    runButton->setToolTip(tr("Run the selected analyzer on the current image"));
    connect(runButton, &QPushButton::clicked, this, &AnalysisPanel::reanalyze);
    bar->addWidget(runButton);

    m_exportButton = new QPushButton(tr("Export Report"));
    m_exportButton->setObjectName(QStringLiteral("analysisExportReportButton"));
    connect(m_exportButton, &QPushButton::clicked, this, &AnalysisPanel::exportRequested);
    bar->addWidget(m_exportButton);
    m_pinBtn = new QPushButton(tr("Pin"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setToolTip(tr("Pin the current analysis result (kept in History, not discarded)"));
    connect(m_pinBtn, &QPushButton::clicked, this, &AnalysisPanel::onPinToggled);
    bar->addWidget(m_pinBtn);
    layout.addLayout(bar);
}

void AnalysisPanel::buildHistorySection(QVBoxLayout &layout)
{
    auto *history = new QListWidget;
    m_historyList = history;
    history->setMaximumHeight(72);
    history->setToolTip(tr("Analysis history (last 50) — double-click to open the image and restore the result"));
    connect(history, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onHistoryActivated(); });

    auto *pinned = new QListWidget;
    m_pinnedList = pinned;
    pinned->setMaximumHeight(72);
    pinned->setToolTip(tr("Pinned analysis results — double-click to open"));
    connect(pinned, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onPinnedActivated(); });

    auto *row = new QHBoxLayout;
    auto *historyColumn = new QVBoxLayout;
    historyColumn->addWidget(new QLabel(tr("History")));
    historyColumn->addWidget(history);
    auto *pinnedColumn = new QVBoxLayout;
    pinnedColumn->addWidget(new QLabel(tr("Pin")));
    pinnedColumn->addWidget(pinned);
    row->addLayout(historyColumn, 1);
    row->addLayout(pinnedColumn, 1);
    layout.addLayout(row);
    connect(m_analyzerCombo, QOverload<int>::of(&QComboBox::activated), this,
            &AnalysisPanel::onAnalyzerSelected);
}

void AnalysisPanel::buildResultTabs(QVBoxLayout &layout)
{
    m_tabs = new QTabWidget;
    m_imageView = std::make_unique<RawImageView>(this);
    layout.addWidget(m_imageView.get(), 2);
    layout.addWidget(m_tabs, 1);

    m_histogramLabel = new QLabel;
    m_histogramLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_histogramLabel->setStyleSheet("QLabel{background:#141414;}");
    m_statsLabel = new QLabel;
    m_statsLabel->setObjectName(QStringLiteral("analysisStatsLabel"));
    m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet(analysisReadoutStyle(false));
    auto *histogramPage = new QWidget;
    auto *histogramLayout = new QVBoxLayout(histogramPage);
    histogramLayout->setContentsMargins(0, 0, 0, 0);
    histogramLayout->setSpacing(4);
    histogramLayout->addWidget(m_histogramLabel, 1);
    histogramLayout->addWidget(m_statsLabel);
    m_tabs->addTab(histogramPage, tr("Histogram"));

    m_rgbLabel = new QLabel;
    m_rgbLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbLabel->setStyleSheet("QLabel{background:#141414;}");
    m_rgbStatsLabel = new QLabel;
    m_rgbStatsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbStatsLabel->setWordWrap(true);
    m_rgbStatsLabel->setStyleSheet(analysisReadoutStyle(false));
    auto *rgbPage = new QWidget;
    auto *rgbLayout = new QVBoxLayout(rgbPage);
    rgbLayout->setContentsMargins(0, 0, 0, 0);
    rgbLayout->setSpacing(4);
    rgbLayout->addWidget(m_rgbLabel, 1);
    rgbLayout->addWidget(m_rgbStatsLabel);
    m_tabs->addTab(rgbPage, tr("RGB channels"));

    m_exposureLabel = new QLabel;
    m_exposureLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_exposureLabel->setWordWrap(true);
    m_exposureLabel->setStyleSheet(analysisReadoutStyle(false));
    m_tabs->addTab(m_exposureLabel, tr("Exposure"));
    m_focusLabel = new QLabel;
    m_focusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_focusLabel->setWordWrap(true);
    m_focusLabel->setStyleSheet(analysisReadoutStyle(false));
    m_tabs->addTab(m_focusLabel, tr("Focus"));
    m_metaLabel = new QLabel;
    m_metaLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet(analysisReadoutStyle(false));
    m_tabs->addTab(m_metaLabel, tr("Metadata"));
    m_pluginResult = new QLabel;
    m_pluginResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_pluginResult->setWordWrap(true);
    m_pluginResult->setStyleSheet(analysisReadoutStyle(false));
    m_tabs->addTab(m_pluginResult, tr("Plugin"));
}

void AnalysisPanel::buildInspectorTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Color space:")));
    auto *colorSpace = new QComboBox;
    colorSpace->addItem(tr("RGB"), static_cast<int>(mviewer::core::ColorSpace::RGB));
    colorSpace->addItem(tr("HSV"), static_cast<int>(mviewer::core::ColorSpace::HSV));
    colorSpace->addItem(tr("Lab"), static_cast<int>(mviewer::core::ColorSpace::Lab));
    colorSpace->addItem(tr("YUV"), static_cast<int>(mviewer::core::ColorSpace::YUV));
    colorSpace->addItem(tr("YCbCr"), static_cast<int>(mviewer::core::ColorSpace::YCbCr));
    colorSpace->addItem(tr("XYZ"), static_cast<int>(mviewer::core::ColorSpace::XYZ));
    colorSpace->addItem(tr("HEX"), static_cast<int>(mviewer::core::ColorSpace::HEX));
    bar->addWidget(colorSpace, 1);

    auto *freeze = new QPushButton(tr("Freeze"));
    freeze->setCheckable(true);
    freeze->setToolTip(tr("Freeze the inspected pixel so it stays visible while the mouse moves"));
    bar->addWidget(freeze);
    bar->addWidget(new QLabel(tr("Neighborhood:")));
    auto *kernel = new QComboBox;
    kernel->addItem(tr("1×1"), 1);
    kernel->addItem(tr("3×3"), 3);
    kernel->addItem(tr("5×5"), 5);
    kernel->addItem(tr("7×7"), 7);
    bar->addWidget(kernel, 1);
    layout->addLayout(bar);

    m_inspectorLabel = new QLabel;
    m_inspectorLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_inspectorLabel->setWordWrap(true);
    m_inspectorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_inspectorLabel->setStyleSheet(analysisReadoutStyle(true));
    m_inspectorLabel->setText(tr("Move the mouse over the image to inspect pixels."));
    layout->addWidget(m_inspectorLabel, 1);
    buildInspectorActions(*layout);
    m_tabs->insertTab(1, page, tr("Pixel Inspector"));

    connect(colorSpace, QOverload<int>::of(&QComboBox::activated), this,
            [this, colorSpace](int)
            {
                m_colorSpace =
                    static_cast<mviewer::core::ColorSpace>(colorSpace->currentData().toInt());
                updateInspectorPage();
            });
    connect(freeze, &QPushButton::toggled, this,
            [this, freeze](bool on)
            {
                m_frozen = on;
                freeze->setText(on ? tr("Frozen") : tr("Freeze"));
                updateInspectorPage();
            });
    connect(kernel, QOverload<int>::of(&QComboBox::activated), this,
            [this, kernel](int)
            {
                m_kernel = kernel->currentData().toInt();
                updateInspectorPage();
            });
}
void AnalysisPanel::buildInspectorActions(QVBoxLayout &layout)
{
    auto *bar = new QHBoxLayout;
    auto *copyRgb = new QPushButton(tr("Copy RGB"));
    auto *copyHex = new QPushButton(tr("Copy HEX"));
    auto *copyXyz = new QPushButton(tr("Copy XYZ"));
    copyRgb->setToolTip(tr("Copy current pixel RGB to clipboard"));
    copyHex->setToolTip(tr("Copy current pixel HEX color to clipboard"));
    copyXyz->setToolTip(tr("Copy current pixel XYZ to clipboard"));
    bar->addWidget(copyRgb);
    bar->addWidget(copyHex);
    bar->addWidget(copyXyz);
    bar->addStretch();
    layout.addLayout(bar);

    connect(copyRgb, &QPushButton::clicked, this,
            [this]()
            {
                if (!m_pValid)
                    return;
                QApplication::clipboard()->setText(
                    QString("RGB(%1, %2, %3)").arg(m_pR).arg(m_pG).arg(m_pB));
            });
    connect(copyHex, &QPushButton::clicked, this,
            [this]()
            {
                if (!m_pValid)
                    return;
                QApplication::clipboard()->setText(QString("#%1%2%3")
                                                       .arg(m_pR, 2, 16, QChar('0'))
                                                       .arg(m_pG, 2, 16, QChar('0'))
                                                       .arg(m_pB, 2, 16, QChar('0')));
            });
    connect(
        copyXyz, &QPushButton::clicked, this,
        [this]()
        {
            if (!m_pValid)
                return;
            auto toLinear = [](uint8_t c) -> double
            {
                const double value = c / 255.0;
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            const double r = toLinear(static_cast<uint8_t>(m_pR));
            const double g = toLinear(static_cast<uint8_t>(m_pG));
            const double b = toLinear(static_cast<uint8_t>(m_pB));
            const double x = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
            const double y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
            const double z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
            QApplication::clipboard()->setText(
                QString("XYZ(%1, %2, %3)").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3).arg(z, 0, 'f', 3));
        });
}
