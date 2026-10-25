// USM dialog keeps the source ICC profile and does not offer a RAW save suffix.
#include "usmcomparedialog.h"

#include "core/image/ImageBuffer.h"
#include "core/image/ImageFormats.h"

#include <QApplication>
#include <QByteArray>

#include <iostream>
#include <string>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    std::cout << (condition ? "[ok] " : "[FAIL] ") << message << "\n";
    if (!condition)
        ++g_failures;
}

void testDefaultSuffix()
{
    const QString rawExt = UsmCompareDialog::defaultSaveExtension(QStringLiteral("nef"));
    check(rawExt == QLatin1String("tif") || rawExt == QLatin1String("png"),
          "RAW default suffix is tif or png");
    check(!mviewer::core::isRawExtension(rawExt.toStdString()),
          "RAW default suffix is not a RAW extension");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("CR2")) == rawExt,
          "RAW suffix decision is case-insensitive");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("shot.nrw")) == rawExt,
          "RAW path uses the same default suffix");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("jpg")) == QLatin1String("jpg"),
          "JPEG keeps its suffix");
    check(UsmCompareDialog::defaultSaveExtension(QString()) == QLatin1String("png"),
          "empty suffix becomes png");
}

void testEmbeddedIcc()
{
    const std::vector<uint8_t> icc = {0x00, 0x01, 0x02, 0x61, 0x62, 0x63, 0x64};
    const QByteArray encoded =
        QByteArray(reinterpret_cast<const char *>(icc.data()), static_cast<int>(icc.size()))
            .toBase64();
    mviewer::domain::ImageMetadata meta;
    meta.textKeys["MViewer.DisplayICC.Base64"] =
        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
    meta.width = 2;
    meta.height = 2;
    ImageData pixels = makeImageData(2, 2, PixelFormat::RGB24);
    UsmCompareDialog dlg(QStringLiteral("shot.nef"), pixels, nullptr, meta);
    check(dlg.embeddedIccProfile() == icc, "USM embeds the source ICC bytes");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testDefaultSuffix();
    testEmbeddedIcc();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "usm dialog tests passed\n";
    return 0;
}
