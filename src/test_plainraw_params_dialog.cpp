// NOLINTBEGIN
#include "application/Shutdown.h"
#include "mainwindow.h"
#include "plainrawparamsdialog.h"
#include "plainrawparamseditor.h"
#include "plainrawsettings.h"
#include "preferencesdialog.h"
#include "runtime_storage.h"
#include "thumbnailpanel_testaccess.h"

#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawFormat.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

using mviewer::core::rawplain::ByteOrder;
using mviewer::core::rawplain::Cfa;
using mviewer::core::rawplain::DumpKind;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::WhiteBalance;

Params sampleParams()
{
    Params params;
    params.width = 128;
    params.height = 64;
    params.bitDepth = 12;
    params.packing = Packing::Msb16;
    params.byteOrder = ByteOrder::Big;
    params.headerOffset = 16;
    params.rowStride = 8200;
    params.blackLevel = 64;
    params.whiteLevel = 4000;
    params.cfa = Cfa::GBRG;
    params.wb = WhiteBalance::Manual;
    params.wbGainR = 1.5;
    params.wbGainB = 2.25;
    params.gain = 4.0;
    params.srgbGamma = false;
    return params;
}

QComboBox *combo(QWidget *parent, const char *name)
{
    return parent->findChild<QComboBox *>(QString::fromLatin1(name));
}

void testEditor()
{
    const Params source = sampleParams();
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults);
    editor.setParams(source);
    CHECK(editor.params() == source);
    CHECK(editor.validationError().isEmpty());

    QComboBox *preset = combo(&editor, "plainRawGainPreset");
    CHECK(preset != nullptr);
    CHECK(preset->currentText() == QStringLiteral("4x"));

    auto *gain = editor.findChild<QDoubleSpinBox *>(QStringLiteral("plainRawGain"));
    CHECK(gain != nullptr);
    gain->setValue(3.0);
    CHECK(preset->currentText() == QStringLiteral("Custom"));
    CHECK(editor.params().gain == 3.0);

    QComboBox *packing = combo(&editor, "plainRawPacking");
    QComboBox *order = combo(&editor, "plainRawByteOrder");
    CHECK(packing != nullptr && order != nullptr);
    packing->setCurrentIndex(packing->findData(static_cast<int>(Packing::Mipi10)));
    CHECK(editor.params().bitDepth == 10);
    CHECK(editor.params().packing == Packing::Mipi10);
    CHECK(!order->isEnabled());
    QComboBox *depth = combo(&editor, "plainRawBitDepth");
    CHECK(depth != nullptr && !depth->isEnabled());

    Params fromName = source;
    fromName.width = 0;
    fromName.height = 0;
    editor.setParams(fromName);
    CHECK(editor.validationError().isEmpty());
    CHECK(editor.params().width == 0);

    PlainRawParamsEditor fileEditor(PlainRawParamsEditor::Mode::File);
    Params zeroWidth = source;
    zeroWidth.width = 0;
    fileEditor.setParams(zeroWidth);
    CHECK(!fileEditor.validationError().isEmpty());

    Params levels = source;
    levels.blackLevel = 4000;
    levels.whiteLevel = 4000;
    fileEditor.setParams(levels);
    CHECK(!fileEditor.validationError().isEmpty());
}

void testSettings()
{
    namespace raw = mviewer::core::rawplain;
    QSettings().clear();
    raw::PlainRawParamStore::instance().clearAll();
    const Params source = sampleParams();
    mviewer::ui::savePlainRawDefaults(source);
    raw::PlainRawParamStore::instance().clearAll();
    const Params loaded = mviewer::ui::loadPlainRawDefaults();
    CHECK(loaded == source);
    CHECK(raw::PlainRawParamStore::instance().defaults() == source);

    QSettings().setValue(QString::fromLatin1(mviewer::ui::kPlainRawDefaultsSettingsKey),
                         QStringLiteral("not-a-valid-raw-param-string"));
    const Params garbage = mviewer::ui::loadPlainRawDefaults();
    CHECK(garbage == Params{});
    CHECK(raw::PlainRawParamStore::instance().defaults() == Params{});
    QSettings().clear();
    raw::PlainRawParamStore::instance().clearAll();
}

void testDialog()
{
    Params initial;
    initial.width = 64;
    initial.height = 32;
    initial.bitDepth = 10;
    initial.packing = Packing::Lsb16;
    const int64_t exact = int64_t{64} * 32 * 2;
    PlainRawParamsDialog matched(QStringLiteral("frame.raw"), exact, initial, false);
    CHECK(matched.canAccept());
    const QString matchedSummary = matched.summaryText();
    CHECK(matchedSummary.contains(QStringLiteral("4096")));
    CHECK(matchedSummary.contains(QStringLiteral("matches")));

    const int64_t packed = int64_t{64} * 32 * 5 / 4;
    PlainRawParamsDialog undersized(QStringLiteral("frame.raw"), packed, initial, false);
    CHECK(!undersized.canAccept());
    CHECK(undersized.summaryText().contains(QStringLiteral("only")));
    CHECK(undersized.guessCount() >= 1);
    undersized.applyGuess(0);
    CHECK(undersized.params().packing == Packing::Mipi10);
    CHECK(undersized.canAccept());

    PlainRawParamsDialog trailing(QStringLiteral("frame.raw"), exact + 100, initial, false);
    CHECK(trailing.canAccept());
    CHECK(trailing.summaryText().contains(QStringLiteral("100")));
    CHECK(!trailing.summaryText().contains(QStringLiteral("edit it to confirm")));
}

void testUntrustedLarger()
{
    Params initial;
    initial.width = 64;
    initial.height = 32;
    initial.bitDepth = 10;
    initial.packing = Packing::Lsb16;
    const int64_t exact = int64_t{64} * 32 * 2;

    PlainRawParamsDialog exactFrame(QStringLiteral("frame.raw"), exact, initial, false);
    exactFrame.setSizeTrusted(false);
    CHECK(exactFrame.canAccept());

    PlainRawParamsDialog larger(QStringLiteral("frame.raw"), exact + 50, initial, false);
    larger.setSizeTrusted(false);
    CHECK(!larger.canAccept());
    CHECK(larger.summaryText().contains(QStringLiteral("edit it to confirm")));

    auto *depth = larger.editor()->findChild<QComboBox *>(QStringLiteral("plainRawBitDepth"));
    CHECK(depth != nullptr);
    const int twelve = depth->findData(12);
    CHECK(twelve >= 0);
    depth->setCurrentIndex(twelve);
    CHECK(larger.params().bitDepth == 12);
    CHECK(!larger.canAccept());

    auto *width = larger.editor()->findChild<QSpinBox *>(QStringLiteral("plainRawWidth"));
    CHECK(width != nullptr);
    width->setValue(48);
    CHECK(larger.params().width == 48);
    CHECK(larger.canAccept());
    CHECK(!larger.summaryText().contains(QStringLiteral("edit it to confirm")));
}

void testBitDepthRoundTrip()
{
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults);
    QComboBox *depth = combo(&editor, "plainRawBitDepth");
    CHECK(depth != nullptr);
    if (depth == nullptr)
    {
        return;
    }
    for (int bits = 8; bits <= 16; ++bits)
    {
        CHECK(depth->findData(bits) >= 0);
    }

    Params odd;
    odd.packing = Packing::Lsb16;
    odd.bitDepth = 11;
    editor.setParams(odd);
    CHECK(editor.params().bitDepth == 11);
    CHECK(editor.params().packing == Packing::Lsb16);

    odd.bitDepth = 13;
    editor.setParams(odd);
    CHECK(editor.params().bitDepth == 13);

    odd.bitDepth = 17;
    editor.setParams(odd);
    CHECK(editor.params().bitDepth == 17);
}

QWidget *namedWidget(QWidget *parent, const char *name)
{
    return parent->findChild<QWidget *>(QString::fromLatin1(name));
}

// ADR 034: Gray defaults editor hides Bayer / white balance and offers
// Bits1, Bits8, Lsb16, Msb16. Constructor installs grayDefaultParams().
void testGrayEditor()
{
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults, nullptr, DumpKind::Gray);
    editor.show();
    const char *hiddenNames[] = {"plainRawCfa", "plainRawWb", "plainRawWbR", "plainRawWbB"};
    for (const char *name : hiddenNames)
    {
        QWidget *widget = namedWidget(&editor, name);
        CHECK(widget != nullptr);
        if (widget == nullptr)
        {
            continue;
        }
        CHECK(!widget->isVisibleTo(&editor));
        CHECK(widget->isHidden());
    }

    QComboBox *packing = combo(&editor, "plainRawPacking");
    CHECK(packing != nullptr);
    if (packing != nullptr)
    {
        CHECK(packing->count() == 4);
        const Packing expected[] = {Packing::Bits1, Packing::Bits8, Packing::Lsb16, Packing::Msb16};
        for (int index = 0; index < 4; ++index)
        {
            CHECK(packing->itemData(index).toInt() == static_cast<int>(expected[index]));
        }
    }

    const Params got = editor.params();
    CHECK(got == mviewer::core::rawplain::grayDefaultParams());
    CHECK(got.bitDepth == 8);
    CHECK(got.packing == Packing::Bits8);
    CHECK(got.cfa == Cfa::Mono);
    CHECK(got.wb == WhiteBalance::None);
    CHECK(!got.srgbGamma);
}

void testGrayBits1Depth()
{
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults, nullptr, DumpKind::Gray);
    QComboBox *packing = combo(&editor, "plainRawPacking");
    CHECK(packing != nullptr);
    if (packing == nullptr)
    {
        return;
    }
    const int bits1 = packing->findData(static_cast<int>(Packing::Bits1));
    CHECK(bits1 >= 0);
    packing->setCurrentIndex(bits1);
    CHECK(editor.params().bitDepth == 1);
    CHECK(editor.params().packing == Packing::Bits1);

    const int lsb16 = packing->findData(static_cast<int>(Packing::Lsb16));
    CHECK(lsb16 >= 0);
    packing->setCurrentIndex(lsb16);
    CHECK(editor.params().packing == Packing::Lsb16);
    CHECK(editor.params().bitDepth != 1);
}

void testGraySetParamsNormalizes()
{
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults, nullptr, DumpKind::Gray);
    Params incoming = mviewer::core::rawplain::grayDefaultParams();
    incoming.cfa = Cfa::RGGB;
    incoming.wb = WhiteBalance::GrayWorld;
    editor.setParams(incoming);
    CHECK(editor.params().cfa == Cfa::Mono);
    CHECK(editor.params().wb == WhiteBalance::None);
}

void testSensorEditorPacking()
{
    PlainRawParamsEditor editor(PlainRawParamsEditor::Mode::Defaults);
    editor.show();
    QComboBox *packing = combo(&editor, "plainRawPacking");
    CHECK(packing != nullptr);
    if (packing != nullptr)
    {
        CHECK(packing->count() == 5);
        CHECK(packing->findData(static_cast<int>(Packing::Bits1)) < 0);
    }
    QWidget *cfa = namedWidget(&editor, "plainRawCfa");
    CHECK(cfa != nullptr);
    if (cfa != nullptr)
    {
        CHECK(!cfa->isHidden());
        CHECK(cfa->isVisibleTo(&editor));
    }
}

void testGraySettings()
{
    namespace raw = mviewer::core::rawplain;
    const QString grayKey = QString::fromLatin1(mviewer::ui::kGrayRawDefaultsSettingsKey);
    const QString sensorKey = QString::fromLatin1(mviewer::ui::kPlainRawDefaultsSettingsKey);
    QSettings().remove(grayKey);
    QSettings().remove(sensorKey);
    raw::PlainRawParamStore::instance().clearAll();

    Params gray = raw::grayDefaultParams();
    gray.packing = Packing::Lsb16;
    gray.bitDepth = 12;
    mviewer::ui::saveGrayRawDefaults(gray);

    const Params normalized = raw::normalizedForKind(gray, DumpKind::Gray);
    const QString stored = QSettings().value(grayKey).toString();
    CHECK(stored == QString::fromStdString(raw::toString(normalized)));
    CHECK(raw::PlainRawParamStore::instance().defaults(DumpKind::Gray).bitDepth == 12);
    CHECK(raw::PlainRawParamStore::instance().defaults() == Params{});

    QSettings().setValue(grayKey, QStringLiteral("garbage"));
    CHECK(mviewer::ui::loadGrayRawDefaults() == raw::grayDefaultParams());

    QSettings().remove(grayKey);
    QSettings().remove(sensorKey);
    raw::PlainRawParamStore::instance().clearAll();
}

void testGrayDialog()
{
    Params initial = mviewer::core::rawplain::grayDefaultParams();
    initial.width = 64;
    initial.height = 32;
    PlainRawParamsDialog dialog(QStringLiteral("a_64x32.gray"), 2048, initial, false, nullptr,
                                DumpKind::Gray);
    CHECK(dialog.windowTitle().startsWith(QStringLiteral("Gray Parameters")));
    CHECK(dialog.canAccept());
}

void testPreferencesGrayPage()
{
    PreferencesDialog dialog;
    QWidget *page = dialog.findChild<QWidget *>(QStringLiteral("preferencesPageGray"));
    CHECK(page != nullptr);
    auto *editor =
        dialog.findChild<PlainRawParamsEditor *>(QStringLiteral("preferencesGrayRawDefaults"));
    CHECK(editor != nullptr);
}

void pumpEvents(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

QAction *preferencesAction(MainWindow &window)
{
    const auto actions = window.findChildren<QAction *>();
    for (QAction *action : actions)
    {
        if (action->text().contains(QStringLiteral("Preferences")))
        {
            return action;
        }
    }
    return nullptr;
}

void clickModalOk()
{
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr)
    {
        return;
    }
    auto *buttons = modal->findChild<QDialogButtonBox *>();
    QPushButton *ok = buttons == nullptr ? nullptr : buttons->button(QDialogButtonBox::Ok);
    if (ok == nullptr)
    {
        modal->close();
        return;
    }
    ok->click();
}

void chooseRawTwelveAndAccept()
{
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr)
    {
        return;
    }
    auto *editor =
        modal->findChild<PlainRawParamsEditor *>(QStringLiteral("preferencesPlainRawDefaults"));
    QComboBox *depth = nullptr;
    if (editor != nullptr)
    {
        depth = editor->findChild<QComboBox *>(QStringLiteral("plainRawBitDepth"));
    }
    if (depth == nullptr)
    {
        modal->close();
        return;
    }
    const int twelve = depth->findData(12);
    if (twelve < 0)
    {
        modal->close();
        return;
    }
    depth->setCurrentIndex(twelve);
    clickModalOk();
}

void closeOpenDialogs()
{
    if (QWidget *modal = QApplication::activeModalWidget())
    {
        modal->close();
    }
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets)
    {
        auto *dialog = qobject_cast<QDialog *>(widget);
        if (dialog != nullptr && dialog->isVisible())
        {
            dialog->close();
        }
    }
}

void onPreferencesPoll(QTimer *poll, void (*edit)())
{
    const int ticks = poll->property("ticks").toInt() + 1;
    poll->setProperty("ticks", ticks);
    if (ticks >= 120)
    {
        closeOpenDialogs();
        poll->stop();
        return;
    }
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr)
    {
        return;
    }
    if (!poll->property("edited").toBool())
    {
        poll->setProperty("edited", true);
        edit();
        return;
    }
    if (ticks >= 80)
    {
        modal->close();
    }
}

void runPreferences(MainWindow &window, QAction *prefs, void (*edit)())
{
    auto *poll = new QTimer(&window);
    poll->setInterval(30);
    poll->setProperty("ticks", 0);
    poll->setProperty("edited", false);
    QObject::connect(poll, &QTimer::timeout, poll,
                     [poll, edit]() { onPreferencesPoll(poll, edit); });
    poll->start();
    prefs->trigger();
    poll->stop();
}

// Preferences > RAW is the public path into MainWindow::onPlainRawDefaultsChanged.
void testDefaultsRefreshThumbnails()
{
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    mviewer::runtime::configureSettings();
    QSettings().clear();
    const QString config = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    if (!config.isEmpty())
    {
        QFile::remove(QDir(config).filePath(QStringLiteral("recovery.json")));
        QFile::remove(QDir(config).filePath(QStringLiteral("mviewer.json")));
    }
    mviewer::core::rawplain::PlainRawParamStore::instance().clearAll();

    mviewer::application::ScopedProcessShutdown shutdown;
    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pumpEvents(150);

    QAction *prefs = preferencesAction(window);
    CHECK(prefs != nullptr);
    if (prefs == nullptr)
    {
        return;
    }
    runPreferences(window, prefs, clickModalOk);

    ThumbnailPanel *panel = window.activeThumbnailPanel();
    CHECK(panel != nullptr);
    if (panel == nullptr)
    {
        return;
    }
    const QString path =
        QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("mv_plain_sensor.raw"));
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(qRgb(12, 34, 56));
    ThumbnailPanelTestAccess::seedDisplayThumb(*panel, path, QPixmap::fromImage(image));
    const QPixmap seeded = panel->thumbReady(path);
    CHECK(panel->pathList().contains(path));
    CHECK(!seeded.isNull());
    if (seeded.isNull() || !panel->pathList().contains(path))
    {
        return;
    }
    const qint64 seededKey = seeded.cacheKey();

    runPreferences(window, prefs, chooseRawTwelveAndAccept);

    CHECK(mviewer::core::rawplain::PlainRawParamStore::instance().defaults().bitDepth == 12);
    CHECK(panel->pathList().contains(path));
    const QPixmap after = panel->thumbReady(path);
    CHECK(after.isNull() || after.cacheKey() != seededKey);
    mviewer::core::rawplain::PlainRawParamStore::instance().clearAll();
}

void testMissingPath()
{
    QTimer::singleShot(0,
                       []()
                       {
                           if (QWidget *box = QApplication::activeModalWidget())
                           {
                               box->close();
                           }
                       });
    const bool edited =
        editPlainRawParamsForPath(nullptr, QStringLiteral("missing-plain-sensor-dump.raw"));
    CHECK(!edited);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-plainraw-dialog-test"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-plainraw-dialog-test"));
    QSettings().clear();
    testEditor();
    testSettings();
    testDialog();
    testUntrustedLarger();
    testBitDepthRoundTrip();
    testGrayEditor();
    testGrayBits1Depth();
    testGraySetParamsNormalizes();
    testSensorEditorPacking();
    testGraySettings();
    testGrayDialog();
    testPreferencesGrayPage();
    testMissingPath();
    testDefaultsRefreshThumbnails();
    QSettings().clear();
    if (g_failures == 0)
    {
        std::printf("test_plainraw_params_dialog: all passed\n");
        return 0;
    }
    return 1;
}
// NOLINTEND
