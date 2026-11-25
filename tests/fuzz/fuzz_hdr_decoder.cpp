// Real path decoder. Each input is written twice, once as .pfm and once as .exr,
// into one process-private temp directory. The cap matches the Session ZIP
// target: 64 KiB in, two reused files, no unbounded temp growth.
#include "core/image/decoder/HdrDecoder.h"
#include "core/image/decoder/HdrDecoderInternal.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size)
{
    if (size > 65536 || (size > 0 && data == nullptr))
        return 0;
    static QTemporaryDir directory;
    if (!directory.isValid())
        std::abort();
    HdrDecoder decoder;
    for (const char *name : {"input.pfm", "input.exr"})
    {
        const auto input = directory.filePath(QString::fromLatin1(name));
        QFile file(input);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            std::abort();
        if (size > 0 && file.write(reinterpret_cast<const char *>(data),
                                   static_cast<qint64>(size)) != static_cast<qint64>(size))
            std::abort();
        file.close();
        const auto utf8 = input.toUtf8().toStdString();
        const auto image = decoder.decodeFull(utf8);
        (void)decoder.decodeScaled(utf8, 8);
        if (!image.isNull() && (image.width <= 0 || image.height <= 0 ||
                                image.width > mviewer::core::hdr_detail::kMaxHdrEdge ||
                                image.height > mviewer::core::hdr_detail::kMaxHdrEdge))
            std::abort();
    }
    return 0;
}
