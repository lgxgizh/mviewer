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

void AnalysisPanel::buildUi()
{
    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(4);

    // Plugin selector — every analyzer is reachable through the AnalyzerPipeline
    // (the orchestration layer over the AnalyzerRegistry). The combo items carry
    // the pipeline id as user data so switching the active analyzer routes
    // through the pipeline, never the registry directly (M15 P0#3).
    QHBoxLayout *plugBar = new QHBoxLayout;
    plugBar->addWidget(new QLabel(tr("Analyzer:")));
    m_analyzerCombo = new QComboBox;
    auto &reg = m_pipeline ? m_pipeline->registry() : AnalyzerRegistry::instance();
    m_pluginIds = reg.availableAnalyzers();
    for (const auto &id : m_pluginIds)
    {
        const auto info = reg.infoFor(id);
        const QString label =
            info ? QString::fromStdString(info->name) : QString::fromStdString(id);
        m_analyzerCombo->addItem(label, QString::fromStdString(id));
    }
    // Dual-image comparison (PSNR/SSIM) is a built-in composite view, not a single
    // registry analyzer, so it stays as an extra option.
    m_analyzerCombo->addItem(tr("Dual Compare (PSNR/SSIM)"), QString("builtin_compare"));
    plugBar->addWidget(m_analyzerCombo, 1);
    // A-7.1: explicit Run button so the unified analyzer entry is discoverable
    // without relying solely on combo activation.
    auto *runBtn = new QPushButton(tr("运行"));
    runBtn->setToolTip(tr("对当前图片运行所选分析器"));
    connect(runBtn, &QPushButton::clicked, this, &AnalysisPanel::reanalyze);
    plugBar->addWidget(runBtn);

    // P1-6: one-click export of the current analysis report, so the analyzer
    // workflow (Image -> pipeline -> result panel -> export) stays inside the panel
    // instead of forcing a trip to the File menu.
    auto *exportBtn = new QPushButton(tr("导出报告"));
    connect(exportBtn, &QPushButton::clicked, this, &AnalysisPanel::exportRequested);
    plugBar->addWidget(exportBtn);
    // M21: pin current result so AnalyzerModel never evicts it.
    m_pinBtn = new QPushButton(tr("钉住"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setToolTip(tr("钉住当前分析结果（History 中保留，不被淘汰）"));
    connect(m_pinBtn, &QPushButton::clicked, this, &AnalysisPanel::onPinToggled);
    plugBar->addWidget(m_pinBtn);
    mainLay->addLayout(plugBar);

    // M21: History + Pinned lists (compact, under the toolbar).
    auto *histRow = new QHBoxLayout;
    m_historyList = new QListWidget;
    m_historyList->setMaximumHeight(72);
    m_historyList->setToolTip(tr("分析历史（最近 50 条）— 双击打开图片并恢复结果"));
    connect(m_historyList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onHistoryActivated(); });
    m_pinnedList = new QListWidget;
    m_pinnedList->setMaximumHeight(72);
    m_pinnedList->setToolTip(tr("钉住的分析结果 — 双击打开"));
    connect(m_pinnedList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onPinnedActivated(); });
    auto *histCol = new QVBoxLayout;
    histCol->addWidget(new QLabel(tr("历史")));
    histCol->addWidget(m_historyList);
    auto *pinCol = new QVBoxLayout;
    pinCol->addWidget(new QLabel(tr("钉住")));
    pinCol->addWidget(m_pinnedList);
    histRow->addLayout(histCol, 1);
    histRow->addLayout(pinCol, 1);
    mainLay->addLayout(histRow);

    connect(m_analyzerCombo, QOverload<int>::of(&QComboBox::activated), this,
            &AnalysisPanel::onAnalyzerSelected);

    m_tabs = new QTabWidget;

    m_imageView = std::make_unique<RawImageView>(this);
    mainLay->addWidget(m_imageView.get(), 2);

    mainLay->addWidget(m_tabs, 1);

    // P1-1: Histogram tab: viz + stats text
    m_histogramLabel = new QLabel;
    m_histogramLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_histogramLabel->setStyleSheet("QLabel{background:#141414;}");
    m_statsLabel = new QLabel;
    m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    auto *histPage = new QWidget;
    auto *histLay = new QVBoxLayout(histPage);
    histLay->setContentsMargins(0, 0, 0, 0);
    histLay->setSpacing(4);
    histLay->addWidget(m_histogramLabel, 1);
    histLay->addWidget(m_statsLabel);
    m_tabs->addTab(histPage, tr("Histogram"));

    // P1-1: RGB tab
    m_rgbLabel = new QLabel;
    m_rgbLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbLabel->setStyleSheet("QLabel{background:#141414;}");
    m_rgbStatsLabel = new QLabel;
    m_rgbStatsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbStatsLabel->setWordWrap(true);
    m_rgbStatsLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    auto *rgbPage = new QWidget;
    auto *rgbLay = new QVBoxLayout(rgbPage);
    rgbLay->setContentsMargins(0, 0, 0, 0);
    rgbLay->setSpacing(4);
    rgbLay->addWidget(m_rgbLabel, 1);
    rgbLay->addWidget(m_rgbStatsLabel);
    m_tabs->addTab(rgbPage, tr("RGB"));

    // P1-1: Exposure tab
    m_exposureLabel = new QLabel;
    m_exposureLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_exposureLabel->setWordWrap(true);
    m_exposureLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_exposureLabel, tr("Exposure"));

    // P1-1: Focus tab
    m_focusLabel = new QLabel;
    m_focusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_focusLabel->setWordWrap(true);
    m_focusLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_focusLabel, tr("Focus"));

    // P1-1: Metadata tab (inside the analysis workspace)
    m_metaLabel = new QLabel;
    m_metaLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_metaLabel, tr("Metadata"));

    // Compare tab
    m_compareLabel = new QLabel;
    m_compareLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_compareLabel->setWordWrap(true);
    m_compareLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_compareLabel, tr("Compare"));

    // Diff preview
    m_diffPreview = new QLabel;
    m_diffPreview->setMinimumHeight(kPreviewSize);
    m_diffPreview->setAlignment(Qt::AlignCenter);
    m_diffPreview->setStyleSheet("QLabel{background:#1e1e1e;}");
    m_tabs->addTab(m_diffPreview, tr("Diff Map"));

    // Plugin tab
    m_pluginResult = new QLabel;
    m_pluginResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_pluginResult->setWordWrap(true);
    m_pluginResult->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_pluginResult, tr("Plugin"));

    // Pixel Inspector tab (M3 Phase-2, upgraded to Pro in M15 P0 #2)
    auto *inspectorPage = new QWidget;
    auto *insLay = new QVBoxLayout(inspectorPage);
    insLay->setContentsMargins(0, 0, 0, 0);
    insLay->setSpacing(4);

    // Color-space + kernel selectors (cheap; changing them just re-renders the
    // inspector text — no histogram recompute, so mouse-move stays smooth).
    QHBoxLayout *insBar = new QHBoxLayout;
    insBar->addWidget(new QLabel(tr("Space:")));
    QComboBox *csCombo = new QComboBox;
    csCombo->addItem(tr("RGB"), static_cast<int>(mviewer::core::ColorSpace::RGB));
    csCombo->addItem(tr("HSV"), static_cast<int>(mviewer::core::ColorSpace::HSV));
    csCombo->addItem(tr("Lab"), static_cast<int>(mviewer::core::ColorSpace::Lab));
    csCombo->addItem(tr("YUV"), static_cast<int>(mviewer::core::ColorSpace::YUV));
    csCombo->addItem(tr("YCbCr"), static_cast<int>(mviewer::core::ColorSpace::YCbCr));
    csCombo->addItem(tr("XYZ"), static_cast<int>(mviewer::core::ColorSpace::XYZ));
    csCombo->addItem(tr("HEX"), static_cast<int>(mviewer::core::ColorSpace::HEX));
    insBar->addWidget(csCombo, 1);
    auto *freezeBtn = new QPushButton(tr("Freeze"));
    freezeBtn->setCheckable(true);
    freezeBtn->setToolTip(
        tr("Freeze the inspected pixel so it stays shown while you move the mouse"));
    insBar->addWidget(freezeBtn);
    insBar->addWidget(new QLabel(tr("Kernel:")));
    QComboBox *kCombo = new QComboBox;
    kCombo->addItem(tr("1×1"), 1);
    kCombo->addItem(tr("3×3"), 3);
    kCombo->addItem(tr("5×5"), 5);
    kCombo->addItem(tr("7×7"), 7);
    insBar->addWidget(kCombo, 1);
    insLay->addLayout(insBar);

    m_inspectorLabel = new QLabel;
    m_inspectorLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_inspectorLabel->setWordWrap(true);
    m_inspectorLabel->setStyleSheet(
        "QLabel{background:#1e1e1e;color:#eee;padding:8px;font-family:monospace;}");
    m_inspectorLabel->setText(tr("Move the mouse over an image to inspect pixels."));
    insLay->addWidget(m_inspectorLabel, 1);

    // Copy buttons for RGB / HEX / XYZ (Pixel Inspector enhancement)
    auto *copyBar = new QHBoxLayout;
    auto *btnCopyRGB = new QPushButton(tr("Copy RGB"));
    auto *btnCopyHEX = new QPushButton(tr("Copy HEX"));
    auto *btnCopyXYZ = new QPushButton(tr("Copy XYZ"));
    btnCopyRGB->setToolTip(tr("Copy current pixel RGB to clipboard"));
    btnCopyHEX->setToolTip(tr("Copy current pixel HEX color to clipboard"));
    btnCopyXYZ->setToolTip(tr("Copy current pixel XYZ to clipboard"));
    copyBar->addWidget(btnCopyRGB);
    copyBar->addWidget(btnCopyHEX);
    copyBar->addWidget(btnCopyXYZ);
    copyBar->addStretch();
    insLay->addLayout(copyBar);

    connect(btnCopyRGB, &QPushButton::clicked, this,
            [this]()
            {
                if (!m_pValid)
                    return;
                QApplication::clipboard()->setText(
                    QString("RGB(%1, %2, %3)").arg(m_pR).arg(m_pG).arg(m_pB));
            });
    connect(btnCopyHEX, &QPushButton::clicked, this,
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
        btnCopyXYZ, &QPushButton::clicked, this,
        [this]()
        {
            if (!m_pValid)
                return;
            // sRGB to linear then to XYZ (D65)
            auto srgbToLinear = [](uint8_t c) -> double
            {
                double v = c / 255.0;
                return (v <= 0.04045) ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
            };
            const double r = srgbToLinear(static_cast<uint8_t>(m_pR));
            const double g = srgbToLinear(static_cast<uint8_t>(m_pG));
            const double b = srgbToLinear(static_cast<uint8_t>(m_pB));
            const double X = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
            const double Y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
            const double Z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
            QApplication::clipboard()->setText(
                QString("XYZ(%1, %2, %3)").arg(X, 0, 'f', 3).arg(Y, 0, 'f', 3).arg(Z, 0, 'f', 3));
        });

    m_tabs->addTab(inspectorPage, tr("Inspector"));

    connect(csCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, csCombo](int)
            {
                m_colorSpace =
                    static_cast<mviewer::core::ColorSpace>(csCombo->currentData().toInt());
                updateInspectorPage();
            });
    connect(freezeBtn, &QPushButton::toggled, this,
            [this, freezeBtn](bool on)
            {
                m_frozen = on;
                freezeBtn->setText(on ? tr("Frozen") : tr("Freeze"));
                updateInspectorPage();
            });
    connect(kCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, kCombo](int)
            {
                m_kernel = kCombo->currentData().toInt();
                updateInspectorPage();
            });

    m_analyzerCombo->setCurrentIndex(0);
    onAnalyzerSelected(0);
}
