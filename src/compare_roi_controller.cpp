#include "compare_roi_controller.h"

#include <QSize>
#include <algorithm>

namespace mviewer::ui
{

CompareRoiController::CompareRoiController(QObject *parent) : QObject(parent)
{
}

void CompareRoiController::setROI(const mviewer::domain::Selection &sel)
{
    if (m_roi.x == sel.x && m_roi.y == sel.y && m_roi.width == sel.width &&
        m_roi.height == sel.height)
    {
        return;
    }
    m_roi = sel;
    bumpGeneration();
    emit roiChanged(m_roi);
}

void CompareRoiController::clearROI()
{
    if (m_roi.isEmpty())
        return;
    m_roi = mviewer::domain::Selection();
    bumpGeneration();
    emit roiCleared();
    emit roiChanged(m_roi);
}

void CompareRoiController::nudgeROI(int dx, int dy, int maxWidth, int maxHeight)
{
    if (m_roi.isEmpty())
        return;

    int newX = m_roi.x + dx;
    int newY = m_roi.y + dy;

    if (maxWidth > 0)
    {
        newX = std::clamp(newX, 0, std::max(0, maxWidth - m_roi.width));
    }
    if (maxHeight > 0)
    {
        newY = std::clamp(newY, 0, std::max(0, maxHeight - m_roi.height));
    }

    if (newX != m_roi.x || newY != m_roi.y)
    {
        m_roi.x = newX;
        m_roi.y = newY;
        bumpGeneration();
        emit roiChanged(m_roi);
    }
}

#include "core/analysis/PixelInspector.h"
#include "core/image/ExifOrientation.h"
#include "core/image/SourceImage.h"

void CompareRoiController::setMeasurementState(ROIMeasurementState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit measurementStateChanged(m_state);
}

ROIPaneMeasurement CompareRoiController::computeSourceROI(const ROIInput &input,
                                                          const mviewer::domain::Selection &roi,
                                                          const TaskScheduler::TaskContext &context)
{
    ROIPaneMeasurement result;
    const auto cancelled = [&context]() { return context.isCancelled(); };

    const int srcW = input.metadata.width > 0 ? input.metadata.width : input.pixels.width;
    const int srcH = input.metadata.height > 0 ? input.metadata.height : input.pixels.height;
    const auto sourceRoi =
        mviewer::core::mapDisplaySelectionToSource(roi, input.adjustment, srcW, srcH);

    if (!input.pixels.isNull())
    {
        result.stats = mviewer::core::computeROIChannelStats(input.pixels, sourceRoi, cancelled);
        if (result.stats.cancelled)
        {
            result.state = ROIPaneState::Cancelled;
            return result;
        }
        result.state = result.stats.valid ? ROIPaneState::Ready : ROIPaneState::Failed;
        if (!result.stats.valid)
            result.reason = "ROI 与源图像素没有交集";
        return result;
    }
    if (input.path.empty() || sourceRoi.isEmpty())
    {
        result.reason = "源图像素不可用";
        return result;
    }

    try
    {
        if (context.isCancelled())
        {
            result.state = ROIPaneState::Cancelled;
            return result;
        }
        const auto source = mviewer::core::SourceImage::open(input.path);
        if (!source)
        {
            result.reason = "无法打开源图";
            return result;
        }
        const QSize displaySize(source->metadata().width, source->metadata().height);
        const long long right = static_cast<long long>(sourceRoi.x) + sourceRoi.width;
        const long long bottom = static_cast<long long>(sourceRoi.y) + sourceRoi.height;
        if (!displaySize.isValid() || sourceRoi.x < 0 || sourceRoi.y < 0 ||
            right > displaySize.width() || bottom > displaySize.height())
        {
            result.reason = "ROI 超出源图范围";
            return result;
        }
        const mviewer::core::SourceRect displayed{sourceRoi.x, sourceRoi.y, sourceRoi.width,
                                                  sourceRoi.height};
        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
            displayed, source->rawWidth(), source->rawHeight(), source->orientation());
        result.decodePath = source->regionDecodePath();
        if (result.decodePath == mviewer::core::SourceDecodePath::FullDecodeCrop)
        {
            result.state = ROIPaneState::Unsupported;
            result.reason = "无法按源图精确解码该区域";
            return result;
        }
        const auto decoded = source->decodeRegion(raw, std::max(1, raw.w), std::max(1, raw.h));
        result.decodePath = decoded.decodePath;
        if (context.isCancelled())
        {
            result.state = ROIPaneState::Cancelled;
            return result;
        }
        if (!decoded.ok || decoded.pixels.isNull())
        {
            result.reason = "源图区域解码失败";
            return result;
        }
        const mviewer::domain::Selection decodedRegion{0, 0, decoded.pixels.width,
                                                       decoded.pixels.height};
        result.stats =
            mviewer::core::computeROIChannelStats(decoded.pixels, decodedRegion, cancelled);
        if (result.stats.cancelled)
        {
            result.state = ROIPaneState::Cancelled;
            return result;
        }
        result.state = result.stats.valid ? ROIPaneState::Ready : ROIPaneState::Failed;
        if (!result.stats.valid)
            result.reason = "解码区域没有源图像素";
    }
    catch (const std::exception &error)
    {
        result.reason = error.what();
    }
    catch (...)
    {
        result.reason = "未知的源图测量失败";
    }
    return result;
}

} // namespace mviewer::ui
