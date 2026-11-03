// NOLINTBEGIN
#include "yuvparamsdialog.h"
#include "yuvparamsettings.h"

#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvSource.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QStringList>

#include <iostream>
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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test-yuvparams"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-test-yuvparams"));
    testDialogSummary();
    testSizePresetCombo();
    testSettingsRoundTrip();
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
