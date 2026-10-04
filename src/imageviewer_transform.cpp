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
#include <QMessageBox>
#include <QPointer>

#include <functional>
#include <memory>
#include <string>

namespace
{

using RotateResult = mviewer::core::ImageFileRotateResult;

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
        if (result.error.empty())
            return tr("未知错误");
        return QString::fromStdString(result.error);
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
        QMessageBox::warning(this, failureTitle,
                             tr("无法改写图片：%1\n%2").arg(path, rotateFailureUserMessage(result)));
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
    submitFileTransform(this, path, tr("旋转失败"),
                        tr("已旋转并覆盖原文件 (%1°)").arg(normAngle),
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
    const QString success = horizontal ? tr("已水平翻转并覆盖原文件") : tr("已垂直翻转并覆盖原文件");
    submitFileTransform(this, path, tr("翻转失败"), success,
                        [utf8, horizontal]()
                        { return mviewer::core::flipImageFile(utf8, horizontal); });
    return true;
}
