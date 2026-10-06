#include "preferencesdialog.h"
#include "Theme.h"
#include "thumbnailpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

QWidget *PreferencesDialog::buildGeneralTab(QSettings &s)
{
    auto *general = new QWidget;
    auto *gl = new QFormLayout(general);

    m_uiTheme = new QComboBox;
    m_uiTheme->addItem(tr("深色模式 (专业 / 推荐)"),
                       static_cast<int>(mviewer::ui::ThemeMode::Dark));
    m_uiTheme->addItem(tr("系统默认 (亮色)"), static_cast<int>(mviewer::ui::ThemeMode::System));
    m_uiTheme->setCurrentIndex(
        m_uiTheme->findData(static_cast<int>(mviewer::ui::Theme::currentTheme())));
    gl->addRow(tr("界面主题"), m_uiTheme);

    m_viewMode = new QComboBox;
    struct VM
    {
        const char *name;
        ThumbnailPanel::ViewMode v;
    };
    static const VM kVM[] = {{"缩略图", ThumbnailPanel::Thumbnail},
                             {"大图标", ThumbnailPanel::LargeIcon},
                             {"小图标", ThumbnailPanel::SmallIcon},
                             {"详情", ThumbnailPanel::Details},
                             {"胶片", ThumbnailPanel::Filmstrip}};
    for (const auto &e : kVM)
        m_viewMode->addItem(tr(e.name), static_cast<int>(e.v));
    const int vmIdx =
        m_viewMode->findData(s.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt());
    m_viewMode->setCurrentIndex(vmIdx >= 0 ? vmIdx : 0);
    gl->addRow(tr("默认视图模式"), m_viewMode);

    m_sortMode = new QComboBox;
    struct SM
    {
        const char *name;
        ThumbnailPanel::SortMode v;
    };
    static const SM kSM[] = {
        {"文件名", ThumbnailPanel::SortName}, {"日期", ThumbnailPanel::SortDate},
        {"大小", ThumbnailPanel::SortSize},   {"分辨率", ThumbnailPanel::SortResolution},
        {"类型", ThumbnailPanel::SortType},   {"评分", ThumbnailPanel::SortRating},
        {"相机", ThumbnailPanel::SortCamera}, {"镜头", ThumbnailPanel::SortLens}};
    for (const auto &e : kSM)
        m_sortMode->addItem(tr(e.name), static_cast<int>(e.v));
    const int smIdx =
        m_sortMode->findData(s.value("thumbSortMode", ThumbnailPanel::SortName).toInt());
    m_sortMode->setCurrentIndex(smIdx >= 0 ? smIdx : 0);
    gl->addRow(tr("默认排序"), m_sortMode);

    m_thumbSize = new QSpinBox;
    m_thumbSize->setRange(64, 512);
    m_thumbSize->setValue(std::clamp(s.value("thumbSize", 160).toInt(), 64, 512));
    gl->addRow(tr("缩略图尺寸"), m_thumbSize);

    m_slideshowInterval = new QSpinBox;
    m_slideshowInterval->setRange(500, 30000);
    m_slideshowInterval->setSingleStep(500);
    m_slideshowInterval->setValue(
        std::clamp(s.value("slideshowInterval", 3000).toInt(), 500, 30000));
    gl->addRow(tr("幻灯片间隔(ms)"), m_slideshowInterval);

    m_slideshowWrap = new QCheckBox(tr("幻灯片循环播放"));
    m_slideshowWrap->setChecked(s.value("slideshowWrap", true).toBool());
    gl->addRow(m_slideshowWrap);

    m_confirmDelete = new QCheckBox(tr("删除前确认"));
    m_confirmDelete->setChecked(s.value("confirmDelete", true).toBool());
    gl->addRow(m_confirmDelete);

    m_gpuAcceleration = new QCheckBox(tr("启用 GPU 硬件加速渲染"));
    m_gpuAcceleration->setChecked(s.value("gpuAcceleration", true).toBool());
    gl->addRow(m_gpuAcceleration);

    return general;
}

QWidget *PreferencesDialog::buildCompareTab(QSettings &s)
{
    auto *compare = new QWidget;
    auto *cl = new QFormLayout(compare);
    m_autoAlign = new QCheckBox(tr("对比前按整数像素平移自动对齐（消除平移错位）"));
    m_autoAlign->setChecked(s.value("autoAlignBeforeDiff", false).toBool());
    cl->addRow(m_autoAlign);
    return compare;
}

QWidget *PreferencesDialog::buildAnalysisTab(QSettings &s)
{
    auto *analysis = new QWidget;
    auto *al = new QFormLayout(analysis);
    m_analysisOverlay = new QComboBox;
    m_analysisOverlay->addItem(tr("无"), 0);
    m_analysisOverlay->addItem(tr("过曝/欠曝斑马线"), 1);
    m_analysisOverlay->addItem(tr("伪彩色"), 2);
    m_analysisOverlay->addItem(tr("R 通道"), 3);
    m_analysisOverlay->addItem(tr("G 通道"), 4);
    m_analysisOverlay->addItem(tr("B 通道"), 5);
    m_analysisOverlay->addItem(tr("Y 亮度"), 6);
    const int ovIdx = m_analysisOverlay->findData(s.value("defaultAnalysisOverlay", 0).toInt());
    m_analysisOverlay->setCurrentIndex(ovIdx >= 0 ? ovIdx : 0);
    al->addRow(tr("默认分析叠加层"), m_analysisOverlay);

    // F4 (M22): shared zebra threshold (1–40), persisted as "zebraThreshold".
    m_zebraThreshold = new QSlider(Qt::Horizontal);
    m_zebraThreshold->setRange(1, 40);
    m_zebraThreshold->setValue(std::clamp(s.value("zebraThreshold", 2).toInt(), 1, 40));
    auto *zbBox = new QWidget;
    auto *zbL = new QHBoxLayout(zbBox);
    zbL->setContentsMargins(0, 0, 0, 0);
    zbL->addWidget(m_zebraThreshold);
    auto *zbVal = new QLabel(QString::number(m_zebraThreshold->value()), this);
    zbL->addWidget(zbVal);
    connect(m_zebraThreshold, &QSlider::valueChanged, zbVal,
            [zbVal](int v) { zbVal->setText(QString::number(v)); });
    al->addRow(tr("斑马线阈值（1–40）"), zbBox);

    return analysis;
}

// F1 (M22) Preferences dialog.
PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("首选项"));
    resize(440, 380);
    QSettings s;

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(s), tr("常规"));
    tabs->addTab(buildCompareTab(s), tr("对比"));
    tabs->addTab(buildAnalysisTab(s), tr("分析"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *main = new QVBoxLayout(this);
    main->addWidget(tabs);
    main->addWidget(buttons);
}

void PreferencesDialog::accept()
{
    QSettings s;
    if (m_uiTheme)
    {
        const auto mode = static_cast<mviewer::ui::ThemeMode>(m_uiTheme->currentData().toInt());
        mviewer::ui::Theme::applyTheme(mode);
    }
    s.setValue("thumbViewMode", m_viewMode->currentData().toInt());
    s.setValue("thumbSortMode", m_sortMode->currentData().toInt());
    s.setValue("thumbSize", m_thumbSize->value());
    s.setValue("slideshowInterval", m_slideshowInterval->value());
    s.setValue("slideshowWrap", m_slideshowWrap->isChecked());
    s.setValue("confirmDelete", m_confirmDelete->isChecked());
    s.setValue("gpuAcceleration", m_gpuAcceleration->isChecked());
    s.setValue("autoAlignBeforeDiff", m_autoAlign->isChecked());
    s.setValue("defaultAnalysisOverlay", m_analysisOverlay->currentData().toInt());
    s.setValue("zebraThreshold", m_zebraThreshold->value());
    s.sync();
    emit settingsChanged();
    QDialog::accept();
}
