#include "preferencesdialog.h"
#include "Language.h"
#include "Theme.h"
#include "core/image/decoder/RawDecodePlan.h"
#include "display/DisplayColorContextProvider.h"
#include "gpu/GpuTileUploader.h"
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

void PreferencesDialog::addUiAppearanceRows(QFormLayout *gl)
{
    m_uiTheme = new QComboBox;
    m_uiTheme->setObjectName(QStringLiteral("preferencesUiTheme"));
    m_uiTheme->addItem(tr("Follow system (default)"),
                       static_cast<int>(mviewer::ui::ThemeMode::System));
    m_uiTheme->addItem(tr("Dark mode"), static_cast<int>(mviewer::ui::ThemeMode::Dark));
    m_uiTheme->addItem(tr("Light mode"), static_cast<int>(mviewer::ui::ThemeMode::Light));
    m_uiTheme->setToolTip(tr(
        "Follow system switches automatically with the Windows \"app mode\" dark/light setting"));
    m_uiTheme->setCurrentIndex(
        m_uiTheme->findData(static_cast<int>(mviewer::ui::Theme::currentTheme())));
    gl->addRow(tr("Interface theme"), m_uiTheme);

    m_uiLanguage = new QComboBox;
    m_uiLanguage->setObjectName(QStringLiteral("preferencesUiLanguage"));
    m_uiLanguage->addItem(tr("Follow system (default)"),
                          static_cast<int>(mviewer::ui::LanguageMode::System));
    m_uiLanguage->addItem(QStringLiteral("English"),
                          static_cast<int>(mviewer::ui::LanguageMode::English));
    // i18n-allow: language endonym (always shown in its own script)
    m_uiLanguage->addItem(QStringLiteral("简体中文"),
                          static_cast<int>(mviewer::ui::LanguageMode::ChineseSimplified));
    m_uiLanguage->setToolTip(tr("UI language. Takes effect immediately without restarting."));
    {
        QSettings langSettings;
        const auto lang = mviewer::ui::Language::languageModeFromKey(
            langSettings.value(QStringLiteral("uiLanguage"), QStringLiteral("system")).toString());
        const int langIdx = m_uiLanguage->findData(static_cast<int>(lang));
        m_uiLanguage->setCurrentIndex(langIdx >= 0 ? langIdx : 0);
    }
    gl->addRow(tr("UI language"), m_uiLanguage);
    auto *languageHint = new QLabel(tr("UI language switches immediately for open windows."));
    languageHint->setObjectName(QStringLiteral("preferencesUiLanguageHint"));
    gl->addRow(QString(), languageHint);
}

QWidget *PreferencesDialog::buildGeneralTab(QSettings &s)
{
    auto *general = new QWidget;
    auto *gl = new QFormLayout(general);

    addUiAppearanceRows(gl);

    m_viewMode = new QComboBox;
    struct VM
    {
        const char *name;
        ThumbnailPanel::ViewMode v;
    };
    static const VM kVM[] = {{QT_TR_NOOP("Thumbnails"), ThumbnailPanel::Thumbnail},
                             {QT_TR_NOOP("Large icons"), ThumbnailPanel::LargeIcon},
                             {QT_TR_NOOP("Small icons"), ThumbnailPanel::SmallIcon},
                             {QT_TR_NOOP("Details"), ThumbnailPanel::Details},
                             {QT_TR_NOOP("Filmstrip"), ThumbnailPanel::Filmstrip},
                             {QT_TR_NOOP("Compact"), ThumbnailPanel::Compact},
                             {QT_TR_NOOP("List"), ThumbnailPanel::List}};
    for (const auto &e : kVM)
        m_viewMode->addItem(tr(e.name), static_cast<int>(e.v));
    const int vmIdx = m_viewMode->findData(static_cast<int>(ThumbnailPanel::viewModeFromInt(
        s.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt())));
    m_viewMode->setCurrentIndex(vmIdx >= 0 ? vmIdx : 0);
    gl->addRow(tr("Default view mode"), m_viewMode);

    m_sortMode = new QComboBox;
    struct SM
    {
        const char *name;
        ThumbnailPanel::SortMode v;
    };
    static const SM kSM[] = {{QT_TR_NOOP("File Name"), ThumbnailPanel::SortName},
                             {QT_TR_NOOP("Date"), ThumbnailPanel::SortDate},
                             {QT_TR_NOOP("Size"), ThumbnailPanel::SortSize},
                             {QT_TR_NOOP("Resolution"), ThumbnailPanel::SortResolution},
                             {QT_TR_NOOP("Type"), ThumbnailPanel::SortType},
                             {QT_TR_NOOP("Rating"), ThumbnailPanel::SortRating},
                             {QT_TR_NOOP("Camera"), ThumbnailPanel::SortCamera},
                             {QT_TR_NOOP("Lens"), ThumbnailPanel::SortLens}};
    for (const auto &e : kSM)
        m_sortMode->addItem(tr(e.name), static_cast<int>(e.v));
    const int smIdx =
        m_sortMode->findData(s.value("thumbSortMode", ThumbnailPanel::SortName).toInt());
    m_sortMode->setCurrentIndex(smIdx >= 0 ? smIdx : 0);
    gl->addRow(tr("Default sort"), m_sortMode);

    m_thumbSize = new QSpinBox;
    m_thumbSize->setRange(ThumbnailPanel::kMinThumbSize, ThumbnailPanel::kMaxThumbSize);
    m_thumbSize->setValue(
        std::clamp(s.value("thumbSize", ThumbnailPanel::kDefaultThumbSize).toInt(),
                   ThumbnailPanel::kMinThumbSize, ThumbnailPanel::kMaxThumbSize));
    gl->addRow(tr("Thumbnail size"), m_thumbSize);

    m_slideshowInterval = new QSpinBox;
    m_slideshowInterval->setRange(500, 30000);
    m_slideshowInterval->setSingleStep(500);
    m_slideshowInterval->setValue(
        std::clamp(s.value("slideshowInterval", 3000).toInt(), 500, 30000));
    gl->addRow(tr("Slideshow interval (ms)"), m_slideshowInterval);

    m_slideshowWrap = new QCheckBox(tr("Loop slideshow"));
    m_slideshowWrap->setChecked(s.value("slideshowWrap", true).toBool());
    gl->addRow(m_slideshowWrap);

    m_confirmDelete = new QCheckBox(tr("Confirm before delete"));
    m_confirmDelete->setChecked(s.value("confirmDelete", true).toBool());
    gl->addRow(m_confirmDelete);

    m_confirmFileTransfer = new QCheckBox(tr("Confirm before copying or moving files"));
    m_confirmFileTransfer->setChecked(s.value("confirmFileTransfer", true).toBool());
    gl->addRow(m_confirmFileTransfer);

    m_confirmModifyFile =
        new QCheckBox(tr("Confirm before modifying the original file (rotate/flip)"));
    m_confirmModifyFile->setChecked(s.value("confirmModifyFile", true).toBool());
    gl->addRow(m_confirmModifyFile);

    m_gpuAcceleration = new QCheckBox(tr("Enable GPU hardware-accelerated rendering"));
    m_gpuAcceleration->setChecked(s.value("gpuAcceleration", true).toBool());
    gl->addRow(m_gpuAcceleration);

    m_colorManagement = new QCheckBox(tr("Enable color management (CMS)"));
    m_colorManagement->setToolTip(tr("When enabled, convert colors using the display ICC profile; "
                                     "when disabled, use native RGB/sRGB"));
    m_colorManagement->setChecked(s.value("display/colorManagement", true).toBool());
    gl->addRow(m_colorManagement);

    m_autoCheckUpdates = new QCheckBox(tr("Automatically check for updates on startup"));
    m_autoCheckUpdates->setObjectName(QStringLiteral("preferencesAutoCheckUpdates"));
    m_autoCheckUpdates->setToolTip(
        tr("Only notifies when a newer release exists; Help → Check for Updates still works."));
    m_autoCheckUpdates->setChecked(s.value(QStringLiteral("autoCheckUpdates"), true).toBool());
    gl->addRow(m_autoCheckUpdates);

    m_rawDisplayPolicy = new QComboBox;
    m_rawDisplayPolicy->setObjectName(QStringLiteral("preferencesRawDisplayPolicy"));
    m_rawDisplayPolicy->addItem(tr("Prefer embedded preview (fast)"), 0);
    m_rawDisplayPolicy->addItem(tr("Full decode when the preview is under 90% of the sensor"), 1);
    m_rawDisplayPolicy->addItem(tr("Always decode full RAW"), 2);
    m_rawDisplayPolicy->setToolTip(
        tr("The camera JPEG preview is fast. A full demosaic uses the sensor. "
           "The default stays on the fast preview."));
    const int rawPolicy = s.value(QStringLiteral("raw/displayPolicy"), 0).toInt();
    const int rawIdx = m_rawDisplayPolicy->findData(rawPolicy);
    m_rawDisplayPolicy->setCurrentIndex(rawIdx >= 0 ? rawIdx : 0);
    gl->addRow(tr("RAW display"), m_rawDisplayPolicy);

    return general;
}

QWidget *PreferencesDialog::buildCompareTab(QSettings &s)
{
    auto *compare = new QWidget;
    auto *cl = new QFormLayout(compare);
    m_autoAlign = new QCheckBox(tr("Auto-align by integer-pixel shift before comparison (eliminate "
                                   "translation misalignment)"));
    m_autoAlign->setChecked(s.value("autoAlignBeforeDiff", false).toBool());
    cl->addRow(m_autoAlign);
    return compare;
}

QWidget *PreferencesDialog::buildAnalysisTab(QSettings &s)
{
    auto *analysis = new QWidget;
    auto *al = new QFormLayout(analysis);
    m_analysisOverlay = new QComboBox;
    m_analysisOverlay->addItem(tr("None"), 0);
    m_analysisOverlay->addItem(tr("Over/underexposure zebra"), 1);
    m_analysisOverlay->addItem(tr("False Color"), 2);
    m_analysisOverlay->addItem(tr("R Channel"), 3);
    m_analysisOverlay->addItem(tr("G Channel"), 4);
    m_analysisOverlay->addItem(tr("B Channel"), 5);
    m_analysisOverlay->addItem(tr("Y Luminance"), 6);
    const int ovIdx = m_analysisOverlay->findData(s.value("defaultAnalysisOverlay", 0).toInt());
    m_analysisOverlay->setCurrentIndex(ovIdx >= 0 ? ovIdx : 0);
    al->addRow(tr("Default analysis overlay"), m_analysisOverlay);

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
    al->addRow(tr("Zebra threshold (1–40)"), zbBox);

    return analysis;
}

// F1 (M22) Preferences dialog.
PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(440, 380);
    QSettings s;

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(s), tr("General"));
    tabs->addTab(buildCompareTab(s), tr("Compare"));
    tabs->addTab(buildAnalysisTab(s), tr("Analyze"));

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
        // Re-applying an unchanged theme restyles the whole app (visible
        // flash); only switch when the user picked a different mode.
        const auto mode = static_cast<mviewer::ui::ThemeMode>(m_uiTheme->currentData().toInt());
        if (mode != mviewer::ui::Theme::currentTheme())
            mviewer::ui::Theme::applyTheme(mode);
    }
    if (m_uiLanguage)
    {
        const auto lang =
            static_cast<mviewer::ui::LanguageMode>(m_uiLanguage->currentData().toInt());
        if (lang != mviewer::ui::Language::current())
            mviewer::ui::Language::apply(lang);
    }
    s.setValue("thumbViewMode", m_viewMode->currentData().toInt());
    s.setValue("thumbSortMode", m_sortMode->currentData().toInt());
    s.setValue("thumbSize", m_thumbSize->value());
    s.setValue("slideshowInterval", m_slideshowInterval->value());
    s.setValue("slideshowWrap", m_slideshowWrap->isChecked());
    s.setValue("confirmDelete", m_confirmDelete->isChecked());
    s.setValue("confirmFileTransfer", m_confirmFileTransfer->isChecked());
    s.setValue("confirmModifyFile", m_confirmModifyFile->isChecked());
    s.setValue("gpuAcceleration", m_gpuAcceleration->isChecked());
    s.setValue("autoAlignBeforeDiff", m_autoAlign->isChecked());
    s.setValue("defaultAnalysisOverlay", m_analysisOverlay->currentData().toInt());
    s.setValue("zebraThreshold", m_zebraThreshold->value());
    const bool colorManagement = m_colorManagement->isChecked();
    s.setValue("display/colorManagement", colorManagement);
    s.setValue(QStringLiteral("autoCheckUpdates"), m_autoCheckUpdates->isChecked());
    const int rawPolicy = m_rawDisplayPolicy ? m_rawDisplayPolicy->currentData().toInt() : 0;
    s.setValue(QStringLiteral("raw/displayPolicy"), rawPolicy);
    mviewer::core::setRawDisplayPolicy(mviewer::core::rawDisplayPolicyFromInt(rawPolicy));
    s.sync();
    GpuTileUploader::resetEnabledCache();
    DisplayColorContextProvider::setColorManagementEnabled(colorManagement);
    emit settingsChanged();
    QDialog::accept();
}
