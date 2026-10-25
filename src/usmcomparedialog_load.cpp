#include "usmcomparedialog.h"

#include "core/image/Encoder.h"
#include "core/image/ImageFormats.h"
#include "core/image/QtConvert.h"
#include "display/DisplayColorContextProvider.h"

#include <QApplication>
#include <QByteArray>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace
{
UsmCompareDialog::FailurePresenter g_failurePresenter;

std::vector<uint8_t> iccBytesFromMetadata(const mviewer::domain::ImageMetadata &meta)
{
    const auto it = meta.textKeys.find("MViewer.DisplayICC.Base64");
    if (it == meta.textKeys.end() || it->second.empty())
        return {};
    const QByteArray encoded(it->second.data(), static_cast<int>(it->second.size()));
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    if (decoded.isEmpty())
        return {};
    const auto *begin = reinterpret_cast<const uint8_t *>(decoded.constData());
    return std::vector<uint8_t>(begin, begin + decoded.size());
}

QString formatName(PixelFormat fmt)
{
    switch (fmt)
    {
    case PixelFormat::RGB24:
        return QStringLiteral("RGB24");
    case PixelFormat::BGR24:
        return QStringLiteral("BGR24");
    case PixelFormat::RGBA32:
        return QStringLiteral("RGBA32");
    case PixelFormat::BGRA32:
        return QStringLiteral("BGRA32");
    case PixelFormat::Grayscale8:
        return QStringLiteral("Grayscale8");
    default:
        return QStringLiteral("Unknown");
    }
}
} // namespace

void UsmCompareDialog::setFailurePresenter(FailurePresenter presenter)
{
    g_failurePresenter = std::move(presenter);
}

QString UsmCompareDialog::defaultSaveExtension(const QString &sourceSuffix)
{
    QString ext = sourceSuffix;
    const int dot = ext.lastIndexOf('.');
    if (dot >= 0)
        ext = ext.mid(dot + 1);
    ext = ext.toLower();
    if (mviewer::core::isRawExtension(ext.toStdString()))
        return Encoder::formatForExtension("tif") == "tiff" ? QStringLiteral("tif")
                                                            : QStringLiteral("png");
    return ext.isEmpty() ? QStringLiteral("png") : ext;
}

void UsmCompareDialog::loadImageSource(const QString &filePath, const ImageData &initialData,
                                       const mviewer::domain::ImageMetadata &meta)
{
    if (!initialData.isNull())
    {
        m_sourceData = initialData;
        m_meta = meta;
        if (m_meta.filePath.empty())
            m_meta.filePath = filePath.toUtf8().toStdString();
        m_iccProfile = iccBytesFromMetadata(m_meta);
    }
    else if (!filePath.isEmpty())
    {
        // Immediate opens may still read a non-RAW file here. Background opens
        // never reach this branch: the worker loader owns that decode.
        QImage loaded(filePath);
        if (!loaded.isNull())
        {
            m_sourceData = mvcore::fromQImagePreservingAlpha(loaded);
            m_meta.filePath = filePath.toStdString();
            m_meta.width = m_sourceData.width;
            m_meta.height = m_sourceData.height;
            if (loaded.colorSpace().isValid())
            {
                const QByteArray icc = loaded.colorSpace().iccProfile();
                if (!icc.isEmpty())
                {
                    m_iccProfile.assign(reinterpret_cast<const uint8_t *>(icc.constData()),
                                        reinterpret_cast<const uint8_t *>(icc.constData()) +
                                            icc.size());
                    const QByteArray encoded = icc.toBase64();
                    m_meta.textKeys["MViewer.DisplayICC.Base64"] =
                        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
                    m_meta.hasIccProfile = true;
                }
            }
        }
    }

    if (m_sourceData.isNull())
        return;

    const auto displayCtx = DisplayColorContextProvider::forWindow(windowHandle());
    m_origQImage = mvcore::toDisplayQImage(m_sourceData, m_meta, displayCtx);
    m_leftView->setImage(m_origQImage, m_origQImage.size());
    m_rightView->setImage(m_origQImage, m_origQImage.size());

    QString csDesc = QStringLiteral("sRGB");
    if (m_origQImage.colorSpace().isValid())
        csDesc = m_origQImage.colorSpace().description();

    m_lblImageInfo->setText(QString("%1×%2 | %3 | %4")
                                .arg(m_sourceData.width)
                                .arg(m_sourceData.height)
                                .arg(formatName(m_sourceData.format))
                                .arg(csDesc));
}

void UsmCompareDialog::presentOpenFailure(const QString &detail)
{
    if (m_leftView)
        m_leftView->setSoftLoading(false);
    if (m_rightView)
        m_rightView->setSoftLoading(false);
    const QString title = tr("USM sharpen comparison");
    const QString body =
        tr("Cannot open %1 for USM sharpen comparison: %2").arg(m_filePath, detail);
    if (g_failurePresenter)
        g_failurePresenter(title, body);
    else if (!m_closing)
        QMessageBox::warning(this, title, body);
}

void UsmCompareDialog::applyBackgroundLoad(const UsmLoadedSource &loaded, bool offUiThread)
{
    m_loadedOffUiThread = offUiThread;
    if (m_leftView)
        m_leftView->setSoftLoading(false);
    if (m_rightView)
        m_rightView->setSoftLoading(false);
    if (m_closing)
        return;
    if (!loaded.ok())
    {
        const QString detail =
            loaded.error.empty() ? tr("decode failed") : QString::fromStdString(loaded.error);
        presentOpenFailure(detail);
        return;
    }
    loadImageSource(m_filePath, loaded.pixels, loaded.meta);
    if (!m_sourceData.isNull())
        scheduleSharpen();
}

void UsmCompareDialog::beginBackgroundLoad()
{
    if (m_leftView)
        m_leftView->setSoftLoading(true);
    if (m_rightView)
        m_rightView->setSoftLoading(true);
    const std::string path = m_filePath.toUtf8().toStdString();
    const SourceLoader loader = m_sourceLoader;
    const QPointer<UsmCompareDialog> guard(this);
    m_activeTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [loader, path, guard](const TaskScheduler::TaskContext &)
        {
            UsmLoadedSource loaded;
            if (loader)
                loaded = loader(path);
            else
                loaded.error = "no loader";
            const bool offUi = !qApp || QThread::currentThread() != qApp->thread();
            QMetaObject::invokeMethod(
                qApp,
                [guard, loaded, offUi]()
                {
                    if (!guard || guard->m_closing)
                        return;
                    guard->applyBackgroundLoad(loaded, offUi);
                },
                Qt::QueuedConnection);
        });
    if (!m_activeTask)
        presentOpenFailure(tr("scheduler rejected submission"));
}
