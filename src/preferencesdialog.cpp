#include "preferencesdialog.h"
#include "thumbnailpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

// F1 (M22) Preferences dialog.
PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("首选项"));
    resize(440, 380);
    QSettings s;

    auto *tabs = new QTabWidget(this);

    // --- 常规 ---
    QWidget *general = new QWidget;
    auto *gl = new QFormLayout(general);

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
    m_viewMode->setCurrentIndex(
        m_viewMode->findData(s.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt()));
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
    m_sortMode->setCurrentIndex(
        m_sortMode->findData(s.value("thumbSortMode", ThumbnailPanel::SortName).toInt()));
    gl->addRow(tr("默认排序"), m_sortMode);

    m_thumbSize = new QSpinBox;
    m_thumbSize->setRange(64, 512);
    m_thumbSize->setValue(s.value("thumbSize", 160).toInt());
    gl->addRow(tr("缩略图尺寸"), m_thumbSize);

    m_slideshowInterval = new QSpinBox;
    m_slideshowInterval->setRange(500, 30000);
    m_slideshowInterval->setSingleStep(500);
    m_slideshowInterval->setValue(s.value("slideshowInterval", 3000).toInt());
    gl->addRow(tr("幻灯片间隔(ms)"), m_slideshowInterval);

    m_confirmDelete = new QCheckBox(tr("删除前确认"));
    m_confirmDelete->setChecked(s.value("confirmDelete", true).toBool());
    gl->addRow(m_confirmDelete);

    // --- 对比 ---
    QWidget *compare = new QWidget;
    auto *cl = new QFormLayout(compare);
    m_autoAlign = new QCheckBox(tr("对比前自动对齐（消除平移错位）"));
    m_autoAlign->setChecked(s.value("autoAlignBeforeDiff", false).toBool());
    cl->addRow(m_autoAlign);

    // --- 分析 ---
    QWidget *analysis = new QWidget;
    auto *al = new QFormLayout(analysis);
    m_analysisOverlay = new QComboBox;
    m_analysisOverlay->addItem(tr("无"), 0);
    m_analysisOverlay->addItem(tr("过曝/欠曝斑马线"), 1);
    m_analysisOverlay->addItem(tr("伪彩色"), 2);
    m_analysisOverlay->setCurrentIndex(
        m_analysisOverlay->findData(s.value("defaultAnalysisOverlay", 0).toInt()));
    al->addRow(tr("默认分析叠加层"), m_analysisOverlay);

    tabs->addTab(general, tr("常规"));
    tabs->addTab(compare, tr("对比"));
    tabs->addTab(analysis, tr("分析"));

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
    s.setValue("thumbViewMode", m_viewMode->currentData().toInt());
    s.setValue("thumbSortMode", m_sortMode->currentData().toInt());
    s.setValue("thumbSize", m_thumbSize->value());
    s.setValue("slideshowInterval", m_slideshowInterval->value());
    s.setValue("confirmDelete", m_confirmDelete->isChecked());
    s.setValue("autoAlignBeforeDiff", m_autoAlign->isChecked());
    s.setValue("defaultAnalysisOverlay", m_analysisOverlay->currentData().toInt());
    s.sync();
    emit settingsChanged();
    QDialog::accept();
}
