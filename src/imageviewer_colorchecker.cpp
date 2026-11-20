// ColorChecker measurement entry point (report feature 6). The chart is
// located by the viewer's current selection; with no selection the whole
// frame is used, which is the usual "the chart fills the frame" capture.
#include "imageviewer.h"

#include "core/analysis/ColorChecker.h"

#include <QMessageBox>

void ImageViewer::measureColorChecker()
{
    if (!m_frame || !m_frame->isValid())
    {
        emit statusMessageRequested(tr("Color checker: no image is loaded"), 4000);
        return;
    }
    QRect rect = m_selectionImage;
    if (rect.width() <= 0 || rect.height() <= 0)
        rect = QRect(0, 0, m_frame->width(), m_frame->height());

    const mviewer::core::ColorCheckerResult result = mviewer::core::measureColorChecker(
        m_frame->pixels(), rect.x(), rect.y(), rect.width(), rect.height());
    if (result.validCount == 0)
    {
        emit statusMessageRequested(tr("Color checker: the rectangle is outside the image"), 5000);
        return;
    }
    const QString report = QString::fromStdString(mviewer::core::colorCheckerReportText(result));
    emit statusMessageRequested(tr("Color checker: mean dE00 %1, max %2 (%3/24 patches)")
                                    .arg(result.meanDeltaE, 0, 'f', 2)
                                    .arg(result.maxDeltaE, 0, 'f', 2)
                                    .arg(result.validCount),
                                6000);
    QMessageBox::information(this, tr("Color Checker dE00 (selection)"), report);
}
