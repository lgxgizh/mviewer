#include "imageviewer.h"

#include "application/ImageLoadingService.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMimeData>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <algorithm>

namespace
{

constexpr int kPlaybackTimerIntervalMs = 10;
constexpr int kPrefetchCount = 2;
constexpr int kPrefetchMaxEdge = 512;

QString sequenceLabel(const mviewer::core::FrameSequenceInfo &sequence)
{
    return sequence.kind == mviewer::core::FrameSequenceKind::Pages ? QStringLiteral("Page")
                                                                      : QStringLiteral("Frame");
}

} // namespace

void ImageViewer::ensurePlaybackTimer()
{
    if (m_playbackTimer)
        return;
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setTimerType(Qt::PreciseTimer);
    m_playbackTimer->setInterval(kPlaybackTimerIntervalMs);
    connect(m_playbackTimer, &QTimer::timeout, this, &ImageViewer::onPlaybackTick);
}

void ImageViewer::play()
{
    if (!m_sequence.animated || m_sequence.frameCount <= 1)
        return;
    ensurePlaybackTimer();
    if (!m_playback.playing())
    {
        m_playback.start();
        emit playbackStateChanged(true);
    }
    m_playbackTimer->start();
    updateFramePresentationStatus();
    emit frameChanged(m_frameIndex, frameCount(), true);
}

void ImageViewer::pause()
{
    if (!m_playback.playing())
        return;
    m_playback.pause();
    if (m_playbackTimer)
        m_playbackTimer->stop();
    emit playbackStateChanged(false);
    updateFramePresentationStatus();
    emit frameChanged(m_frameIndex, frameCount(), false);
}

void ImageViewer::restart()
{
    if (frameCount() <= 1)
        return;
    const bool shouldPlay = m_sequence.animated;
    m_playback.setCurrentFrame(0);
    setFrameIndex(0);
    if (shouldPlay)
        play();
    else
        pause();
}

void ImageViewer::previousFrame()
{
    if (frameCount() <= 1)
        return;
    const int next = m_sequence.animated
                         ? (m_frameIndex + frameCount() - 1) % frameCount()
                         : std::max(0, m_frameIndex - 1);
    setFrameIndex(next);
}

void ImageViewer::nextFrame()
{
    if (frameCount() <= 1)
        return;
    const int next = m_sequence.animated ? (m_frameIndex + 1) % frameCount()
                                         : std::min(frameCount() - 1, m_frameIndex + 1);
    setFrameIndex(next);
}

void ImageViewer::setFrameIndex(int index)
{
    if (frameCount() <= 1 || m_currentPath.isEmpty())
        return;
    const int target = std::clamp(index, 0, frameCount() - 1);
    m_playback.setCurrentFrame(target);
    requestFrame(target);
}

void ImageViewer::requestFrame(int index)
{
    if (m_currentPath.isEmpty() || frameCount() <= 1)
        return;
    const int target = std::clamp(index, 0, frameCount() - 1);
    if (target == m_frameIndex && m_requestedFrame < 0)
    {
        updateFramePresentationStatus();
        emit frameChanged(m_frameIndex, frameCount(), isPlaying());
        return;
    }

    mviewer::application::ImageLoadingService::instance().cancelAsync(m_frameRequest);
    const uint64_t generation = ++m_frameGeneration;
    m_requestedFrame = target;
    const QString path = m_currentPath;
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    ImageLoadOptions options;
    options.useDiskCache = true;
    options.generateHistogram = true;
    options.frameMaxEdge = 0;
    m_frameRequest = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        path.toUtf8().toStdString(), target,
        [guard, path, target, generation](const ImageLoadResult &result)
        {
            if (!qApp || !guard)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, path, target, generation, result]()
                {
                    ImageViewer *viewer = guard->data();
                    if (!viewer || viewer->m_currentPath != path ||
                        viewer->m_frameGeneration != generation)
                        return;
                    viewer->applyLoadedFrame(result, target, generation);
                },
                Qt::QueuedConnection);
        },
        options, m_lifetime);
}

void ImageViewer::applyLoadedFrame(const ImageLoadResult &result, int requestedFrame,
                                   uint64_t generation)
{
    if (generation != m_frameGeneration || requestedFrame != m_requestedFrame)
        return;
    m_frameRequest.reset();
    m_requestedFrame = -1;
    if (!result.success() || !result.frame || result.frame->pixels().isNull())
    {
        if (m_playback.playing())
            pause();
        setWindowTitle(QStringLiteral("%1 %2 %3失败 - %4 - MViewer")
                           .arg(sequenceLabel(m_sequence))
                           .arg(requestedFrame + 1)
                           .arg(sequenceLabel(m_sequence) == QStringLiteral("Page")
                                    ? QStringLiteral("页")
                                    : QStringLiteral("帧"))
                           .arg(QFileInfo(m_currentPath).fileName()));
        emit loadFailed(m_currentPath);
        return;
    }

    // A frame/page change is an image-generation boundary for every derived
    // surface. Cancel old tile/overlay/ROI work before publishing the new
    // frame; the viewport itself deliberately remains unchanged.
    beginImageGeneration();
    m_frame = result.frame;
    m_sequence = m_frame->sequenceInfo();
    m_frameIndex = m_frame->frameIndex();
    m_playback.setFrameInfo({m_frameIndex, m_frame->metadata().frameDurationMs > 0
                                                  ? m_frame->metadata().frameDurationMs
                                                  : 100,
                             m_frame->width(), m_frame->height()});
    computeHistogram();
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    m_overlayCache.clear();
    clearLoadedGpu();
    updateFramePresentationStatus();
    update();
    emit imageReady(m_frame);
    emit frameChanged(m_frameIndex, frameCount(), isPlaying());
    prefetchFrames(m_frameIndex);
}

void ImageViewer::onPlaybackTick()
{
    if (!m_playback.playing())
        return;
    const auto decision = m_playback.tick();
    if (decision.due)
        requestFrame(decision.frameIndex);
}

void ImageViewer::cancelFrameRequests()
{
    ++m_frameGeneration;
    m_requestedFrame = -1;
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_frameRequest);
    for (auto &request : m_framePrefetchRequests)
        mviewer::application::ImageLoadingService::instance().cancelAsync(request);
    m_framePrefetchRequests.clear();
    if (m_playbackTimer)
        m_playbackTimer->stop();
}

void ImageViewer::prefetchFrames(int currentIndex)
{
    for (auto &request : m_framePrefetchRequests)
        mviewer::application::ImageLoadingService::instance().cancelAsync(request);
    m_framePrefetchRequests.clear();
    if (m_currentPath.isEmpty() || frameCount() <= 1)
        return;

    ImageLoadOptions options;
    options.useDiskCache = true;
    options.generateHistogram = false;
    options.frameMaxEdge = kPrefetchMaxEdge;
    const QString path = m_currentPath;
    for (int offset = 1; offset <= kPrefetchCount; ++offset)
    {
        int target = currentIndex + offset;
        if (m_sequence.animated)
            target %= frameCount();
        else if (target >= frameCount())
            break;
        if (target == m_requestedFrame)
            continue;
        auto request = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
            path.toUtf8().toStdString(), target, [](const ImageLoadResult &) {}, options,
            m_lifetime);
        if (request)
            m_framePrefetchRequests.push_back(std::move(request));
    }
}

void ImageViewer::updateFramePresentationStatus()
{
    if (!isMultiFrame() || m_currentPath.isEmpty())
    {
        m_frameStatusText.clear();
        setAccessibleDescription({});
        return;
    }
    const QFileInfo info(m_currentPath);
    const QSize size = displaySize();
    const QString position = m_currentIndex >= 0
                                 ? QStringLiteral(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                 : QString();
    const QString state = isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
    m_frameStatusText = QStringLiteral("%1 %2/%3")
                            .arg(sequenceLabel(m_sequence))
                            .arg(m_frameIndex + 1)
                            .arg(frameCount());
    if (m_sequence.animated)
        m_frameStatusText += QStringLiteral(" · %1").arg(state);
    const QString shortcuts =
        m_sequence.animated
            ? QStringLiteral("Space: Play/Pause; comma/period: Previous/Next frame")
            : QStringLiteral("Comma/period: Previous/Next page");
    setAccessibleDescription(QStringLiteral("%1. %2").arg(m_frameStatusText, shortcuts));
    setWindowTitle(QStringLiteral("%1 (%2x%3)%4 · %5 - MViewer")
                       .arg(info.fileName())
                       .arg(size.width())
                       .arg(size.height())
                       .arg(position)
                       .arg(m_frameStatusText));
}

bool ImageViewer::handleFrameKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers != Qt::NoModifier && modifiers != Qt::KeypadModifier)
        return false;
    if (key == Qt::Key_Comma)
        previousFrame();
    else if (key == Qt::Key_Period)
        nextFrame();
    else if (key == Qt::Key_Space && m_sequence.animated)
    {
        if (isPlaying())
            pause();
        else
            play();
    }
    else
        return false;
    return true;
}

namespace
{

QScreen *screenOverlapping(const QRect &frame, int *areaOut)
{
    QScreen *best = nullptr;
    int bestArea = 0;
    for (QScreen *screen : QGuiApplication::screens())
    {
        if (!screen)
            continue;
        const QRect overlap = screen->availableGeometry().intersected(frame);
        const int area = overlap.isValid() ? overlap.width() * overlap.height() : 0;
        if (area > bestArea)
        {
            bestArea = area;
            best = screen;
        }
    }
    if (areaOut)
        *areaOut = bestArea;
    return best ? best : QGuiApplication::primaryScreen();
}

void placeWindowed(QWidget *window, const QRect &avail, bool offScreen)
{
    QSize size = window->size();
    if (size.width() < 200)
        size.setWidth(200);
    if (size.height() < 150)
        size.setHeight(150);
    size.setWidth((std::min)(size.width(), avail.width()));
    size.setHeight((std::min)(size.height(), avail.height()));
    if (offScreen)
    {
        const int widthCap = avail.width() > 320 ? avail.width() * 3 / 4 : avail.width();
        const int heightCap = avail.height() > 240 ? avail.height() * 3 / 4 : avail.height();
        size.setWidth((std::min)(size.width(), widthCap));
        size.setHeight((std::min)(size.height(), heightCap));
    }
    window->resize(size);
    int x = offScreen ? avail.center().x() - size.width() / 2 : window->x();
    int y = offScreen ? avail.center().y() - size.height() / 2 : window->y();
    if (x < avail.left())
        x = avail.left();
    if (y < avail.top())
        y = avail.top();
    if (x + size.width() > avail.right() + 1)
        x = avail.right() - size.width() + 1;
    if (y + size.height() > avail.bottom() + 1)
        y = avail.bottom() - size.height() + 1;
    window->move(x, y);
}

bool mimeHasLocalFile(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return false;
    for (const QUrl &url : mime->urls())
    {
        if (!url.toLocalFile().isEmpty())
            return true;
    }
    return false;
}

} // namespace

void ImageViewer::clampWidgetToAvailableScreens(QWidget *window)
{
    if (!window)
        return;
    int overlapArea = 0;
    QScreen *screen = screenOverlapping(window->frameGeometry(), &overlapArea);
    if (!screen || !screen->availableGeometry().isValid())
        return;
    const QRect avail = screen->availableGeometry();
    const Qt::WindowStates state = window->windowState();
    const bool fullscreen = state.testFlag(Qt::WindowFullScreen) || window->isFullScreen();
    const bool maximized = state.testFlag(Qt::WindowMaximized) || window->isMaximized();
    if ((fullscreen || maximized) && overlapArea > 0)
    {
        if (QWindow *handle = window->windowHandle())
        {
            if (handle->screen() != screen)
                handle->setScreen(screen);
        }
        return;
    }
    if (fullscreen || maximized)
        window->setWindowState(state & ~(Qt::WindowFullScreen | Qt::WindowMaximized));
    const QRect frame = window->frameGeometry();
    if (!fullscreen && !maximized && overlapArea > 0 && avail.contains(frame))
        return;
    placeWindowed(window, avail, overlapArea == 0);
}

void ImageViewer::restoreWindowGeometry()
{
    QSettings settings;
    const QByteArray geom = settings.value(QStringLiteral("viewerGeometry")).toByteArray();
    if (geom.isEmpty())
    {
        resize(900, 700);
        clampWidgetToAvailableScreens(this);
        return;
    }
    restoreGeometry(geom);
    clampWidgetToAvailableScreens(this);
}

void ImageViewer::persistWindowGeometry()
{
    QSettings settings;
    const bool fullscreen = property("mviewerFullscreenRequested").toBool();
    const QByteArray geom =
        (fullscreen && !m_windowedGeometry.isEmpty()) ? m_windowedGeometry : saveGeometry();
    settings.setValue(QStringLiteral("viewerGeometry"), geom);
    if (QScreen *screen = this->screen())
        settings.setValue(QStringLiteral("viewerScreen"), screen->name());
    settings.sync();
    if (settings.status() != QSettings::NoError)
        emit statusMessageRequested(QStringLiteral("查看器窗口位置未能保存"), 5000);
}

void ImageViewer::setFullscreenRequested(bool requested)
{
    // The property is authoritative when offscreen Qt cannot report fullscreen.
    // Keep the windowed frame so leave/close does not store the monitor rect.
    if (requested && !property("mviewerFullscreenRequested").toBool())
        m_windowedGeometry = saveGeometry();
    setProperty("mviewerFullscreenRequested", requested);
    if (requested)
    {
        setWindowState(windowState() | Qt::WindowFullScreen);
        showFullScreen();
    }
    else
    {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
        showNormal();
        if (!m_windowedGeometry.isEmpty())
            restoreGeometry(m_windowedGeometry);
        clampWidgetToAvailableScreens(this);
    }

    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    QTimer::singleShot(0, this,
                       [guard, requested]()
                       {
                           ImageViewer *viewer = guard ? guard->data() : nullptr;
                           if (!viewer)
                               return;
                           if (!viewer->m_fitMode)
                           {
                               viewer->update();
                               return;
                           }
                           if (viewer->m_frame && viewer->m_frame->isValid())
                               viewer->fitToWidget();
                           else if (!viewer->m_provisionalImage.isNull())
                           {
                               viewer->m_view.screenW = viewer->width();
                               viewer->m_view.screenH = viewer->height();
                               const QSize source = viewer->m_provisionalSourceSize.isValid()
                                                        ? viewer->m_provisionalSourceSize
                                                        : viewer->m_provisionalImage.size();
                               viewer->m_view.fit(source.width(), source.height(),
                                                  requested ? FitPolicy::MaximizeClient
                                                            : FitPolicy::Comfortable);
                               viewer->advanceViewportRevision();
                               viewer->emitZoom();
                           }
                           viewer->update();
                       });
}

void ImageViewer::dragEnterEvent(QDragEnterEvent *event)
{
    if (mimeHasLocalFile(event->mimeData()))
        event->acceptProposedAction();
    else
        QOpenGLWidget::dragEnterEvent(event);
}

void ImageViewer::dragMoveEvent(QDragMoveEvent *event)
{
    if (mimeHasLocalFile(event->mimeData()))
        event->acceptProposedAction();
    else
        QOpenGLWidget::dragMoveEvent(event);
}

void ImageViewer::dropEvent(QDropEvent *event)
{
    QStringList paths;
    if (const QMimeData *mime = event->mimeData())
    {
        for (const QUrl &url : mime->urls())
        {
            const QString local = url.toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
    }
    if (paths.isEmpty())
    {
        QOpenGLWidget::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    emit filesDropped(paths);
}
