// Rotate/flip file encode, extracted from imageviewer_contextmenu.cpp so that
// TU stays under the 800-line cap. The encode runs on the scheduler; the UI
// thread only releases the source, then applies the result. Slideshow ticks
// see mviewerFileTransformBusy and do not advance mid-write.

#include "imageviewer.h"

#include "core/image/ImageFileRotate.h"
#include "core/image/ImageLoadingFacade.h"
#include "core/scheduler/TaskScheduler.h"
#include "thumbnailprovider.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QPointer>

#include <functional>
#include <memory>
#include <string>

namespace
{

using RotateResult = mviewer::core::ImageFileRotateResult;

enum class LooseIo
{
    None,
    PathTooLong,
    Network
};

bool transformDialogAllowed(const QWidget *viewer)
{
    return qApp && !qApp->closingDown() && viewer && viewer->isVisible();
}

QString elidePathMiddle(const QString &path)
{
    if (path.size() <= 160)
        return path;
    return path.left(72) + QStringLiteral("…") + path.right(72);
}

bool containsIoToken(const QString &text, const char *token)
{
    return text.contains(QLatin1String(token));
}

LooseIo classifyLooseIo(const QString &error)
{
    const QString lower = error.toLower();
    if (containsIoToken(lower, "enametoolong") || containsIoToken(lower, "too long") ||
        error.contains(QString::fromUtf8("路径过长")) ||
        error.contains(QString::fromUtf8("文件名过长")))
        return LooseIo::PathTooLong;
    if (containsIoToken(lower, "network") || containsIoToken(lower, "unreachable") ||
        containsIoToken(lower, "timed out") || error.contains(QString::fromUtf8("网络")) ||
        error.contains(QString::fromUtf8("主机")) || error.contains(QLatin1String("\\\\")))
        return LooseIo::Network;
    return LooseIo::None;
}

struct TransformState
{
    RotateResult result;
};

void submitFileTransform(ImageViewer *viewer, const QString &path, const QString &failureTitle,
                         const QString &successText, const std::function<RotateResult()> &encode)
{
    auto state = std::make_shared<TransformState>();
    const QPointer<ImageViewer> guard(viewer);
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [state, encode](const TaskScheduler::TaskContext &ctx)
        {
            if (!ctx.isCancelled())
                state->result = encode();
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state, path, failureTitle, successText]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, state, path, failureTitle, successText]()
                {
                    ImageViewer *live = guard.data();
                    if (!live)
                        return;
                    live->completeFileTransform(path, state->result, failureTitle, successText);
                },
                Qt::QueuedConnection);
        });
    if (!handle)
    {
        viewer->setProperty("mviewerFileTransformBusy", false);
        if (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
        if (transformDialogAllowed(viewer))
            QMessageBox::warning(viewer, failureTitle, viewer->tr("后台任务被调度器拒绝。"));
        if (viewer->currentPath() == path)
            viewer->refreshSource(path);
    }
}

bool startFileTransform(ImageViewer *viewer, QString *pathOut)
{
    if (!viewer || viewer->currentPath().isEmpty())
        return false;
    if (viewer->property("mviewerFileTransformBusy").toBool())
        return false;
    const QString path = viewer->currentPath();
    viewer->releaseSourceHandles(path);
    viewer->setProperty("mviewerFileTransformBusy", true);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    *pathOut = path;
    return true;
}

} // namespace

QString ImageViewer::rotateFailureUserMessage(const mviewer::core::ImageFileRotateResult &result)
{
    using mviewer::core::ImageRotateError;
    switch (result.errorCode)
    {
    case ImageRotateError::NotWritable:
    case ImageRotateError::AccessDenied:
        return tr("文件或所在文件夹没有写入权限");
    case ImageRotateError::SharingViolation:
        return tr("文件正在被使用（含本程序解码），无法覆盖");
    case ImageRotateError::UnsupportedFormat:
    {
        std::string suffix = result.error;
        const auto pos = suffix.rfind(": ");
        if (pos != std::string::npos)
            suffix = suffix.substr(pos + 2);
        if (suffix.empty() || suffix == "(none)")
            suffix = "?";
        return tr("暂不支持改写 .%1（当前仅 PNG/JPEG/BMP/WebP）")
            .arg(QString::fromStdString(suffix));
    }
    case ImageRotateError::NotFound:
    case ImageRotateError::EmptyPath:
        return tr("找不到文件");
    case ImageRotateError::None:
    case ImageRotateError::InvalidAngle:
    case ImageRotateError::ReadFailed:
    case ImageRotateError::ConvertFailed:
    case ImageRotateError::RotateFailed:
    case ImageRotateError::EncodeFailed:
    case ImageRotateError::ShortWrite:
    case ImageRotateError::WriteFailed:
    {
        const QString error = QString::fromStdString(result.error);
        const LooseIo loose = classifyLooseIo(error);
        if (loose == LooseIo::PathTooLong)
            return tr("路径过长，系统无法打开或写入");
        if (loose == LooseIo::Network)
            return tr("网络路径不可达或超时");
        if (error.isEmpty())
            return tr("未知错误");
        return error;
    }
    }
    return tr("未知错误");
}

void ImageViewer::completeFileTransform(const QString &path,
                                        const mviewer::core::ImageFileRotateResult &result,
                                        const QString &failureTitle, const QString &successText)
{
    if (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
    setProperty("mviewerFileTransformBusy", false);
    if (!result.ok)
    {
        QString detail = rotateFailureUserMessage(result);
        const QString rawError = QString::fromStdString(result.error);
        const QString rawLower = rawError.toLower();
        const bool permission = rawLower.contains(QLatin1String("denied")) ||
                                rawLower.contains(QLatin1String("permission")) ||
                                rawLower.contains(QString::fromUtf8("拒绝"));
        if (path.size() >= 260 && !permission && (detail == rawError || detail == tr("未知错误")))
            detail = tr("路径过长，系统无法打开或写入");
        if (transformDialogAllowed(this))
        {
            QMessageBox::warning(this, failureTitle,
                                 tr("无法改写图片：%1\n%2").arg(elidePathMiddle(path), detail));
        }
        if (m_currentPath == path)
            refreshSource(path);
        return;
    }

    const std::string utf8 = path.toUtf8().toStdString();
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
    emit fileRotated(path);
    emit statusMessageRequested(successText);
    if (m_currentPath == path)
        refreshSource(path);
}

bool ImageViewer::rotateCW()
{
    return rotateImage(90);
}

bool ImageViewer::rotateCCW()
{
    return rotateImage(-90);
}

bool ImageViewer::rotateImage(int angle)
{
    if (m_currentPath.isEmpty())
        return false;
    int normAngle = angle % 360;
    if (normAngle < 0)
        normAngle += 360;
    if (normAngle == 0)
        return true;
    if (property("mviewerFileTransformBusy").toBool())
        return true;

    QString path;
    if (!startFileTransform(this, &path))
        return true;
    emit statusMessageRequested(tr("正在旋转…"));
    const std::string utf8 = path.toUtf8().toStdString();
    submitFileTransform(this, path, tr("旋转失败"), tr("已旋转并覆盖原文件 (%1°)").arg(normAngle),
                        [utf8, normAngle]()
                        { return mviewer::core::rotateImageFile(utf8, normAngle); });
    return true;
}

bool ImageViewer::flipHorizontal()
{
    return flipImage(true);
}

bool ImageViewer::flipVertical()
{
    return flipImage(false);
}

bool ImageViewer::flipImage(bool horizontal)
{
    if (m_currentPath.isEmpty())
        return false;
    if (property("mviewerFileTransformBusy").toBool())
        return true;

    QString path;
    if (!startFileTransform(this, &path))
        return true;
    emit statusMessageRequested(tr("正在翻转…"));
    const std::string utf8 = path.toUtf8().toStdString();
    const QString success =
        horizontal ? tr("已水平翻转并覆盖原文件") : tr("已垂直翻转并覆盖原文件");
    submitFileTransform(this, path, tr("翻转失败"), success, [utf8, horizontal]()
                        { return mviewer::core::flipImageFile(utf8, horizontal); });
    return true;
}

bool ImageViewer::browsePathEquals(const QString &left, const QString &right)
{
    const auto key = [](QString path)
    {
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return QDir::cleanPath(path);
    };
    const QString a = key(left);
    const QString b = key(right);
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

int ImageViewer::indexInBrowseSequence(const QString &path) const
{
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        if (browsePathEquals(m_fileList.at(i), path))
            return i;
    }
    return -1;
}
