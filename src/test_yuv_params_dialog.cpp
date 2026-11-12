// NOLINTBEGIN
#include "yuvextensionmapeditor.h"
#include "yuvparamsdialog.h"
#include "yuvparamsettings.h"
#include "yuvpixfmtpicker.h"

#include "core/image/ImageFormats.h"
#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvSource.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QStringList>

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    std::cout << (condition ? "[ok] " : "[FAIL] ") << message << "\n";
    if (!condition)
    {
        ++g_failures;
    }
}

mviewer::core::yuv::Format makeFormat(const char *pix, int width, int height)
{
    mviewer::core::yuv::Format format;
    format.width = width;
    format.height = height;
    mviewer::core::yuv::applyPixFmtName(pix, format);
    return format;
}

void testDialogSummary()
{
    using mviewer::core::yuv::Matrix;
    const mviewer::core::yuv::Format initial = makeFormat("yuv420p", 16, 10);
    YuvParamsDialog dialog(QStringLiteral("clip.yuv"), 3 * 240 + 7, initial, {}, false);
    check(dialog.canAccept(), "16x10 yuv420p accepts three frames");
    const QString summary = dialog.summaryText();
    check(summary.contains(QStringLiteral("240")), "summary names 240-byte frames");
    check(summary.contains(QStringLiteral("3")), "summary names 3 frames");
    check(summary.contains(QStringLiteral("7")), "summary names 7 trailing bytes");
    const mviewer::core::yuv::Format shown = dialog.format();
    check(shown.width == 16 && shown.height == 10, "format keeps 16x10");
    check(shown.matrix == Matrix::BT709, "default matrix is BT.709");

    dialog.setPixFmt(QStringLiteral("yuv444p"));
    check(dialog.summaryText().contains(QStringLiteral("480")), "yuv444p frame is 480 bytes");
    check(dialog.canAccept(), "one yuv444p frame still fits");

    dialog.setFrameSize(64, 64);
    check(dialog.summaryText().contains(QStringLiteral("smaller than one frame")),
          "64x64 is smaller than one frame");
    check(!dialog.canAccept(), "undersized frame cannot be accepted");

    const mviewer::core::yuv::Format before = dialog.format();
    dialog.setPixFmt(QStringLiteral("not-a-format"));
    const mviewer::core::yuv::Format after = dialog.format();
    check(after == before, "unknown pixel format is ignored");
}

void testSizePresetCombo()
{
    const mviewer::core::yuv::Format initial = makeFormat("yuv420p", 16, 10);
    const std::vector<std::pair<int, int>> candidates = {{16, 10}, {8, 20}};
    YuvParamsDialog dialog(QStringLiteral("clip.yuv"), 240, initial, candidates, false);
    const QList<QComboBox *> boxes = dialog.findChildren<QComboBox *>();
    check(boxes.size() >= 4, "size preset adds a fourth combo box");
}

bool comboItemEnabled(QComboBox *box, int data)
{
    auto *model = qobject_cast<QStandardItemModel *>(box->model());
    const int index = box->findData(data);
    return model && index >= 0 && model->item(index)->isEnabled();
}

void testSplitPixelFormatControls()
{
    using namespace mviewer::core::yuv;
    // The bug report: a 4:2:0 file with interleaved UV. Start from what inference
    // guessed (planar yuv420p) and fix it with the layout and order controls.
    YuvParamsDialog dialog(QStringLiteral("cam.yuv"), 240, makeFormat("yuv420p", 16, 10), {},
                           false);
    YuvPixFmtPicker *picker = dialog.pixFmtPicker();
    check(picker != nullptr, "dialog has the split pixel-format picker");
    if (!picker)
        return;
    check(picker->pixFmtName() == QStringLiteral("yuv420p"), "initial yuv420p shown");
    check(picker->endianBox()->isHidden(), "byte order hidden at 8 bit");
    check(picker->alignmentBox()->isHidden(), "alignment hidden at 8 bit");
    check(!comboItemEnabled(picker->layoutBox(), int(Layout::Packed)), "packed disabled for 4:2:0");

    picker->layoutBox()->setCurrentIndex(picker->layoutBox()->findData(int(Layout::SemiPlanar)));
    check(picker->pixFmtName() == QStringLiteral("nv12"), "UV interleaved 4:2:0 is nv12");
    check(dialog.format().layout == Layout::SemiPlanar, "dialog format is semi-planar");
    check(dialog.canAccept(), "nv12 16x10 frame fits 240 bytes");

    picker->orderBox()->setCurrentIndex(picker->orderBox()->findData(int(PlaneOrder::VU)));
    check(picker->pixFmtName() == QStringLiteral("nv21"), "VU order gives nv21");

    picker->layoutBox()->setCurrentIndex(picker->layoutBox()->findData(int(Layout::Planar)));
    check(picker->pixFmtName() == QStringLiteral("yv12"), "planar VU is yv12");

    picker->layoutBox()->setCurrentIndex(picker->layoutBox()->findData(int(Layout::SemiPlanar)));
    picker->orderBox()->setCurrentIndex(picker->orderBox()->findData(int(PlaneOrder::UV)));
    picker->depthBox()->setCurrentIndex(picker->depthBox()->findData(10));
    check(!picker->endianBox()->isHidden(), "byte order shown above 8 bit");
    check(!picker->alignmentBox()->isHidden(), "alignment shown for 10-bit semi-planar");
    check(picker->pixFmtName() == QStringLiteral("p010le"),
          "10-bit UV interleaved defaults to P010");
    picker->alignmentBox()->setCurrentIndex(picker->alignmentBox()->findData(0));
    check(picker->pixFmtName() == QStringLiteral("nv12p10lelsb"), "LSB 10-bit semi-planar name");
    Format lsb = dialog.format();
    check(lsb.bitDepth == 10 && !lsb.msbAligned && lsb.layout == Layout::SemiPlanar,
          "LSB 10-bit semi-planar format");
    picker->endianBox()->setCurrentIndex(picker->endianBox()->findData(int(Endian::Big)));
    check(dialog.format().endian == Endian::Big, "big-endian applies");

    picker->subsamplingBox()->setCurrentIndex(
        picker->subsamplingBox()->findData(int(Subsampling::S422)));
    picker->depthBox()->setCurrentIndex(picker->depthBox()->findData(8));
    check(comboItemEnabled(picker->layoutBox(), int(Layout::Packed)), "packed enabled for 4:2:2");
    picker->layoutBox()->setCurrentIndex(picker->layoutBox()->findData(int(Layout::Packed)));
    check(picker->pixFmtName() == QStringLiteral("yuyv422"), "packed 4:2:2 defaults to YUYV");
    picker->orderBox()->setCurrentIndex(picker->orderBox()->findData(int(Packing::UYVY)));
    check(picker->pixFmtName() == QStringLiteral("uyvy422"), "UYVY order");
    check(!comboItemEnabled(picker->depthBox(), 10), "packed is 8-bit only");

    picker->subsamplingBox()->setCurrentIndex(
        picker->subsamplingBox()->findData(int(Subsampling::S400)));
    check(picker->pixFmtName() == QStringLiteral("gray"), "4:0:0 is gray");
    check(!picker->orderBox()->isEnabled(), "order disabled without chroma");

    dialog.setPixFmt(QStringLiteral("p010le"));
    const Format p010 = dialog.format();
    check(p010.layout == Layout::SemiPlanar && p010.bitDepth == 10 && p010.msbAligned,
          "setPixFmt(p010le) selects the controls");
    dialog.setPixFmt(QStringLiteral("NV21"));
    check(picker->pixFmtName() == QStringLiteral("nv21"), "setPixFmt is case-insensitive");
    check(dialog.format().width == 16 && dialog.format().height == 10, "size kept across edits");
}

void testExtensionMapSettings()
{
    using namespace mviewer::core::yuv;
    QSettings().clear();
    const auto defaults = mviewer::ui::loadYuvExtensionMapSettings();
    check(defaults == extensionRules() && !defaults.empty(), "missing key installs the defaults");
    check(isYuvExtension("nv12") && mappedPixFmt("nv12") == std::optional<std::string>("nv12"),
          "default .nv12 mapping");

    auto rules = defaults;
    for (auto &r : rules)
        if (r.ext == "yuv")
            r.pixFmt = "nv12";
    rules.push_back({"cam", "nv21"});
    mviewer::ui::saveYuvExtensionMap(rules);
    const QStringList lines =
        QSettings()
            .value(QString::fromLatin1(mviewer::ui::kYuvExtensionMapSettingsKey))
            .toStringList();
    check(lines.contains(QStringLiteral("yuv=nv12")) && lines.contains(QStringLiteral("cam=nv21")),
          "mapping persisted as ext=pixfmt lines");

    setExtensionRules(defaultExtensionRules());
    const auto reloaded = mviewer::ui::loadYuvExtensionMapSettings();
    check(reloaded.size() == rules.size(), "every saved rule reloads");
    check(isYuvExtension("cam") && mappedPixFmt("yuv") == std::optional<std::string>("nv12"),
          "saved mapping reloads");

    QSettings().setValue(QString::fromLatin1(mviewer::ui::kYuvExtensionMapSettingsKey),
                         QStringList{QStringLiteral("raw=nv12"), QStringLiteral("x=bogus"),
                                     QStringLiteral("garbage")});
    mviewer::ui::loadYuvExtensionMapSettings();
    check(isYuvExtension("raw") && !isYuvExtension("x"), "bad lines skipped");
    check(isYuvExtension("yuv") && isYuvExtension("y4m"), ".yuv/.y4m always readable");

    // Editor widget: round trip and validation.
    setExtensionRules(defaultExtensionRules());
    YuvExtensionMapEditor editor;
    editor.setRules(extensionRules());
    check(editor.rowCount() == int(extensionRules().size()), "editor lists every rule");
    check(editor.validationError().isEmpty(), "defaults validate");
    check(editor.rules() == extensionRules(), "editor round-trips the rules");
    editor.addRow(QStringLiteral(".Bin"), QStringLiteral("nv12"));
    check(editor.validationError().isEmpty(), "new .bin row validates");
    check(editor.rules().back() == (ExtensionRule{"bin", "nv12"}), "row normalised");
    editor.addRow(QStringLiteral("bin"), QString());
    check(!editor.validationError().isEmpty(), "duplicate extension rejected");
    editor.setRules(extensionRules());
    editor.addRow(QStringLiteral("png"), QStringLiteral("nv12"));
    // Only meaningful when a decoder registered .png in this process.
    if (mviewer::core::ImageFormats::isSupportedSuffix("png"))
        check(!editor.validationError().isEmpty(), "existing image format rejected");
    editor.setRules(extensionRules());
    editor.addRow(QStringLiteral("foo"), QStringLiteral("not-a-format"));
    check(!editor.validationError().isEmpty(), "unknown pixel format rejected");

    setExtensionRules(defaultExtensionRules());
    QSettings().clear();
}

void testSettingsRoundTrip()
{
    QSettings().clear();
    mviewer::core::yuv::YuvParamStore::instance().clearAll();

    mviewer::core::yuv::Format format = makeFormat("yuv420p", 32, 32);
    format.matrix = mviewer::core::yuv::Matrix::BT709;
    mviewer::ui::saveYuvParam("k1", format);
    format.matrix = mviewer::core::yuv::Matrix::BT601;
    mviewer::ui::saveYuvParam("k1", format);

    QSettings settings;
    const QStringList lines =
        settings.value(QString::fromLatin1(mviewer::ui::kYuvParamsSettingsKey)).toStringList();
    check(lines.size() == 1, "second save replaces the line for the same key");

    mviewer::core::yuv::YuvParamStore::instance().clearAll();
    check(mviewer::ui::loadYuvParamSettings() == 1, "one valid line loads");
    const auto loaded = mviewer::core::yuv::YuvParamStore::instance().get("k1");
    check(loaded.has_value() && *loaded == format, "loaded format matches the second save");

    mviewer::ui::clearYuvParam("k1");
    const QStringList afterClear =
        QSettings().value(QString::fromLatin1(mviewer::ui::kYuvParamsSettingsKey)).toStringList();
    check(afterClear.isEmpty(), "clear removes the saved line");
    check(!mviewer::core::yuv::YuvParamStore::instance().get("k1").has_value(),
          "clear drops the store entry");

    QSettings().setValue(QString::fromLatin1(mviewer::ui::kYuvParamsSettingsKey),
                         QStringList{QStringLiteral("nokey"), QStringLiteral("k2\tgarbage")});
    mviewer::core::yuv::YuvParamStore::instance().clearAll();
    check(mviewer::ui::loadYuvParamSettings() == 0, "garbage lines are skipped");
    check(!mviewer::core::yuv::YuvParamStore::instance().get("k2").has_value(),
          "unparsed format is not stored");

    QSettings().clear();
    mviewer::core::yuv::YuvParamStore::instance().clearAll();
}

QComboBox *chromaCombo(QWidget *root)
{
    const QList<QComboBox *> boxes = root->findChildren<QComboBox *>();
    for (QComboBox *box : boxes)
    {
        if (box->findText(QStringLiteral("Bilinear")) >= 0)
        {
            return box;
        }
    }
    return nullptr;
}

void testChromaUpsampleSetting()
{
    using mviewer::core::yuv::ChromaUpsample;
    const QString key = QString::fromLatin1(mviewer::ui::kYuvChromaUpsampleSettingsKey);
    QSettings().remove(key);
    mviewer::core::yuv::setDefaultChromaUpsample(ChromaUpsample::Nearest);
    check(mviewer::ui::loadYuvChromaUpsampleSetting() == ChromaUpsample::Bilinear,
          "missing chroma key loads bilinear");
    check(mviewer::core::yuv::defaultChromaUpsample() == ChromaUpsample::Bilinear,
          "load installs bilinear");

    QSettings().setValue(key, QStringLiteral("nearest"));
    check(mviewer::ui::loadYuvChromaUpsampleSetting() == ChromaUpsample::Nearest,
          "nearest setting loads");

    YuvParamsDialog dialog(QStringLiteral("clip.yuv"), 240, makeFormat("yuv420p", 16, 10), {},
                           false);
    QComboBox *chroma = chromaCombo(&dialog);
    check(chroma != nullptr && chroma->currentIndex() == 1, "dialog shows saved nearest");
    if (!chroma)
    {
        QSettings().remove(key);
        mviewer::core::yuv::setDefaultChromaUpsample(ChromaUpsample::Bilinear);
        return;
    }
    chroma->setCurrentIndex(0);
    check(mviewer::core::yuv::defaultChromaUpsample() == ChromaUpsample::Bilinear,
          "combo change applies bilinear");
    check(QSettings().value(key).toString() == QStringLiteral("bilinear"),
          "combo change persists bilinear");

    chroma->setCurrentIndex(1);
    chroma->setCurrentIndex(0);
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QPushButton *ok = buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
    check(ok != nullptr, "dialog has OK");
    if (ok != nullptr)
    {
        ok->click();
    }
    check(QSettings().value(key).toString() == QStringLiteral("bilinear"),
          "accept persists bilinear");
    check(mviewer::core::yuv::defaultChromaUpsample() == ChromaUpsample::Bilinear,
          "accept installs bilinear");
    QSettings().remove(key);
    mviewer::core::yuv::setDefaultChromaUpsample(ChromaUpsample::Bilinear);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test-yuvparams"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-test-yuvparams"));
    testDialogSummary();
    testSizePresetCombo();
    testSplitPixelFormatControls();
    testExtensionMapSettings();
    testSettingsRoundTrip();
    testChromaUpsampleSetting();
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
