// Included by imageviewer_lod.cpp to handle LOD ROI stats scheduling
// and overlay derivations without exceeding source line caps.

void ImageViewer::scheduleRasterRoiStats(const QRect &selection)
{
    if (selection.isEmpty() || m_raster.image.isNull())
        return;
    const QRect src = selection.intersected(m_raster.sourceRect);
    if (src.isEmpty() || m_raster.sourceRect.width() <= 0 || m_raster.sourceRect.height() <= 0)
        return;
    const QRect &covered = m_raster.sourceRect;
    const int rw = m_raster.image.width();
    const int rh = m_raster.image.height();
    if (rw <= 0 || rh <= 0)
        return;
    const int x0 =
        (std::max)(0, static_cast<int>((static_cast<qint64>(src.x() - covered.x()) * rw) /
                                       covered.width()));
    const int y0 =
        (std::max)(0, static_cast<int>((static_cast<qint64>(src.y() - covered.y()) * rh) /
                                       covered.height()));
    const int x1 =
        (std::min)(rw, static_cast<int>(((static_cast<qint64>(src.right() - covered.x()) + 1) * rw +
                                         covered.width() - 1) /
                                        covered.width()));
    const int y1 =
        (std::min)(rh,
                   static_cast<int>(((static_cast<qint64>(src.bottom() - covered.y()) + 1) * rh +
                                     covered.height() - 1) /
                                    covered.height()));
    if (x1 <= x0 || y1 <= y0)
        return;
    const QImage crop = m_raster.image.copy(x0, y0, x1 - x0, y1 - y0);
    const mviewer::domain::Selection region{src.x(), src.y(), src.width(), src.height()};
    const auto result = std::make_shared<mviewer::core::PreviewStats>();
    const uint64_t revision = m_roiRevision;
    const QString path = m_currentPath;
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    m_roiStatsRequest = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [crop, result](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return;
            const ImageData data = mvcore::fromQImage(crop);
            if (!data.isNull())
                *result = mviewer::core::computePreviewStats(data);
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, path, region, revision, result]()
        {
            if (!guard || !qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, path, region, revision, result]()
                {
                    ImageViewer *viewer = guard->data();
                    if (!viewer || viewer->m_roiRevision != revision ||
                        viewer->m_currentPath != path || !result->valid)
                        return;
                    const QString text = ImageViewer::roiStatsText(
                        region.x, region.y, region.width, region.height, result->lumMean,
                        result->vMean, result->rMean, result->gMean, result->bMean);
                    emit viewer->regionStats(text);
                },
                Qt::QueuedConnection);
        });
}

void ImageViewer::scheduleLodOverlayDerivation() const
{
    if (m_lodOverlayPending || m_raster.image.isNull() ||
        m_overlayMode == mviewer::OverlayMode::None)
        return;
    m_lodOverlayPending = true;
    const uint64_t request = ++m_lodOverlayRequest;
    // COW copy: the worker reads pixels that the UI thread may replace later,
    // without copying the payload.
    const QImage raster = m_raster.image;
    const qint64 key = raster.cacheKey();
    const mviewer::OverlayMode mode = m_overlayMode;
    const int threshold = m_zebraThreshold;
    QPointer<ImageViewer> guard(const_cast<ImageViewer *>(this));
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [guard, request, raster, key, mode, threshold](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return;
            ImageData data = mvcore::fromQImage(raster);
            QImage derived;
            if (!data.isNull())
            {
                mviewer::applyOverlay(data, mode, threshold);
                derived = mvcore::toQImageFast(data);
            }
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, request, derived, key, mode, threshold]()
                {
                    ImageViewer *viewer = guard.data();
                    if (!viewer || request != viewer->m_lodOverlayRequest)
                        return; // superseded, or the viewer is gone
                    viewer->m_lodOverlayPending = false;
                    viewer->m_lodOverlayImage = derived;
                    viewer->m_lodOverlayKey = key;
                    viewer->m_lodOverlayMode = mode;
                    viewer->m_lodOverlayThreshold = threshold;
                    viewer->update();
                },
                Qt::QueuedConnection);
        });
}
