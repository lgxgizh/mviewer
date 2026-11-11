// NOLINTBEGIN
#include "plainrawparamsdialog.h"
#include "plainrawparamseditor.h"
#include "plainrawsettings.h"

#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawFormat.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <cstdio>

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
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-plainraw-dialog-test"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-plainraw-dialog-test"));
    QSettings().clear();
    testEditor();
    testSettings();
    testDialog();
    testMissingPath();
    QSettings().clear();
    if (g_failures == 0)
    {
        std::printf("test_plainraw_params_dialog: all passed\n");
        return 0;
    }
    return 1;
}
// NOLINTEND
